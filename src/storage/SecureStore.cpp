#include "storage/SecureStore.h"
#include "app/AppConfig.h"
#include "storage/Database.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusUnixFileDescriptor>
#include <QDebug>
#include <QEventLoop>
#include <QFile>
#include <QFileDevice>
#include <QFileInfo>
#include <QTimer>
#include <QUuid>

#include <cerrno>
#include <fcntl.h>
#include <unistd.h>

#include <openssl/core_names.h>
#include <openssl/evp.h>
#include <openssl/kdf.h>
#include <openssl/params.h>
#include <openssl/rand.h>

namespace {
constexpr int kNonceLen = 12;
constexpr int kTagLen = 16;
constexpr int kKeyLen = 32;
constexpr int kPortalCallTimeoutMs = 5000;
constexpr int kPortalResponseTimeoutMs = 15000;
// 0x02 == AAD-bound format. 0x01 (the pre-AAD format) is treated as legacy and
// rejected on decrypt so it surfaces as a one-time re-login rather than silently
// decrypting without the context binding.
constexpr char kBlobVersion = 0x02;

QString localSecretPath()
{
    return AppConfig::dataDir() + QStringLiteral("/master.key");
}

bool hasExistingLocalSecret()
{
    const QFileInfo key(localSecretPath());
    return key.isFile() && key.size() >= kKeyLen && key.isReadable();
}

// Receives the XDG portal Request "Response" signal so acquirePortalSecret() can
// wait on it with a local event loop. Kept in the .cpp (moc'd via SecureStore.moc).
class PortalWaiter : public QObject
{
    Q_OBJECT
public:
    quint32     response = 1; // non-zero == error/cancelled unless the portal says 0
    QVariantMap results;
    bool        got = false;

public slots:
    void onResponse(uint code, const QVariantMap &res)
    {
        response = code;
        results = res;
        got = true;
        emit finished();
    }

signals:
    void finished();
};
} // namespace

QByteArray SecretContext::aad() const
{
    QByteArray a;
    a.append(serverId.toUtf8());
    a.append('\0');
    a.append(accountId.toUtf8());
    a.append('\0');
    a.append(credType.toUtf8());
    a.append('\0');
    a.append(QByteArray::number(schemaVersion));
    return a;
}

SecureStore::SecureStore(QObject *parent)
    : QObject(parent)
{
}

bool SecureStore::runningUnderFlatpak()
{
    return QFileInfo::exists(QStringLiteral("/.flatpak-info"))
           || qEnvironmentVariableIsSet("FLATPAK_ID");
}

QByteArray SecureStore::masterSecret()
{
    if (m_master.isEmpty())
        m_master = acquireMasterSecret();
    return m_master;
}

QByteArray SecureStore::acquireMasterSecret()
{
    const QString marker = Database::instance().getSetting(QStringLiteral("secretProvider"));

    // Provider already chosen: it is sticky. Never switch after credentials exist.
    if (marker == QLatin1String("portal-v1")) {
        m_provider = Provider::Portal;
        return acquirePortalSecret(); // MUST succeed; empty => fail closed (no fallback)
    }
    if (marker == QLatin1String("local-v1")) {
        m_provider = Provider::Local;
        return acquireLocalSecret();
    }

    // Builds predating the provider marker always used master.key. Preserve that
    // key before treating an unmarked installation as new: selecting the portal
    // here would discard the established key identity and put every startup through
    // a portal request. A user who killed the app during that request would repeat
    // the apparent startup hang forever because no marker was saved.
    if (hasExistingLocalSecret()) {
        m_provider = Provider::Local;
        Database::instance().putSetting(QStringLiteral("secretProvider"),
                                        QStringLiteral("local-v1"));
        return acquireLocalSecret();
    }

    // Genuinely fresh setup: choose the provider now and persist it. This is the
    // ONLY place a fallback to the local key is allowed.
    if (runningUnderFlatpak()) {
        const QByteArray portal = acquirePortalSecret();
        if (!portal.isEmpty()) {
            m_provider = Provider::Portal;
            Database::instance().putSetting(QStringLiteral("secretProvider"),
                                            QStringLiteral("portal-v1"));
            return portal;
        }
        qWarning() << "SecureStore: Secret portal unavailable at setup; using local key";
    }
    m_provider = Provider::Local;
    Database::instance().putSetting(QStringLiteral("secretProvider"),
                                    QStringLiteral("local-v1"));
    return acquireLocalSecret();
}

QByteArray SecureStore::acquireLocalSecret()
{
    const QString path = localSecretPath();
    QFile f(path);
    if (f.exists() && f.open(QIODevice::ReadOnly)) {
        const QByteArray k = f.readAll();
        // Close before any possible reopen below: a QFile already open read-only
        // cannot be reopened for writing, which would leave a fresh key unpersisted
        // and make every secret undecryptable on the next launch.
        f.close();
        if (k.size() >= kKeyLen)
            return k;
    }
    QByteArray secret(kKeyLen, Qt::Uninitialized);
    if (RAND_bytes(reinterpret_cast<unsigned char *>(secret.data()), kKeyLen) != 1) {
        // Refuse to hand back a non-random "key": returning empty makes deriveKey()
        // and thus encrypt()/decrypt() fail closed rather than encrypt under a
        // predictable key.
        qWarning() << "SecureStore: RAND_bytes failed generating master key";
        return {};
    }
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
        f.write(secret);
        f.close();
    } else {
        qWarning() << "SecureStore: could not persist master key to" << path;
    }
    return secret;
}

QByteArray SecureStore::acquirePortalSecret()
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected())
        return {};

    // The portal writes the secret to the write end of this pipe and closes it.
    int fds[2];
    if (::pipe2(fds, O_CLOEXEC) != 0)
        return {};
    const int readFd = fds[0];
    QDBusUnixFileDescriptor writeFd(fds[1]);
    ::close(fds[1]); // QDBusUnixFileDescriptor dup'd it; drop our copy

    QVariantMap options;
    const QString prevToken =
        Database::instance().getSetting(QStringLiteral("secretPortalToken"));
    if (!prevToken.isEmpty())
        options.insert(QStringLiteral("token"), prevToken);

    // Portal requests can finish immediately. Supply a unique handle token and
    // subscribe to the predictable Request path BEFORE making the method call;
    // subscribing only to the returned path loses an early Response signal and
    // leaves the UI sitting in the timeout loop.
    QString handleToken =
        QStringLiteral("shelfremote") + QUuid::createUuid().toString(QUuid::Id128);
    options.insert(QStringLiteral("handle_token"), handleToken);
    QString sender = bus.baseService();
    if (sender.startsWith(QLatin1Char(':')))
        sender.remove(0, 1);
    sender.replace(QLatin1Char('.'), QLatin1Char('_'));
    const QString expectedPath =
        QStringLiteral("/org/freedesktop/portal/desktop/request/%1/%2")
            .arg(sender, handleToken);

    PortalWaiter waiter;
    const QString portalService = QStringLiteral("org.freedesktop.portal.Desktop");
    const QString requestInterface = QStringLiteral("org.freedesktop.portal.Request");
    if (!bus.connect(portalService, expectedPath, requestInterface,
                     QStringLiteral("Response"), &waiter,
                     SLOT(onResponse(uint, QVariantMap)))) {
        ::close(readFd);
        qWarning() << "SecureStore: could not subscribe to Secret portal response";
        return {};
    }

    QDBusMessage call = QDBusMessage::createMethodCall(
        portalService,
        QStringLiteral("/org/freedesktop/portal/desktop"),
        QStringLiteral("org.freedesktop.portal.Secret"),
        QStringLiteral("RetrieveSecret"));
    call << QVariant::fromValue(writeFd) << options;

    // Keep processing window/input events while the portal activates instead of
    // blocking the GUI thread in QDBusConnection::call().
    QDBusPendingCall pending = bus.asyncCall(call, kPortalCallTimeoutMs);
    QDBusPendingCallWatcher callWatcher(pending);
    if (!callWatcher.isFinished()) {
        QEventLoop callLoop;
        QObject::connect(&callWatcher, &QDBusPendingCallWatcher::finished,
                         &callLoop, &QEventLoop::quit);
        QTimer callTimeout;
        callTimeout.setSingleShot(true);
        QObject::connect(&callTimeout, &QTimer::timeout, &callLoop, &QEventLoop::quit);
        callTimeout.start(kPortalCallTimeoutMs);
        callLoop.exec();
    }

    QDBusPendingReply<QDBusObjectPath> reply = pending;
    if (!reply.isFinished() || reply.isError()) {
        ::close(readFd);
        qWarning() << "SecureStore: Secret portal RetrieveSecret failed:"
                   << (reply.isError() ? reply.error().message()
                                       : QStringLiteral("timed out"));
        return {};
    }
    const QDBusObjectPath reqPath = reply.value();
    if (reqPath.path().isEmpty()) {
        ::close(readFd);
        return {};
    }

    // Current portals return the path derived from handle_token. Accommodate an
    // older implementation returning another path, though such an implementation
    // can still race because that path is unknowable before the method reply.
    if (reqPath.path() != expectedPath) {
        bus.disconnect(portalService, expectedPath, requestInterface,
                       QStringLiteral("Response"), &waiter,
                       SLOT(onResponse(uint, QVariantMap)));
        bus.connect(portalService, reqPath.path(), requestInterface,
                    QStringLiteral("Response"), &waiter,
                    SLOT(onResponse(uint, QVariantMap)));
    }

    if (!waiter.got) {
        QEventLoop loop;
        QObject::connect(&waiter, &PortalWaiter::finished, &loop, &QEventLoop::quit);
        QTimer timeout;
        timeout.setSingleShot(true);
        QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
        timeout.start(kPortalResponseTimeoutMs);
        loop.exec();
    }

    if (!waiter.got || waiter.response != 0) {
        ::close(readFd);
        qWarning() << "SecureStore: Secret portal returned no secret (response"
                   << waiter.response << ")";
        return {};
    }

    // Persist a rotated continuation token if the backend provided one.
    const QString newToken = waiter.results.value(QStringLiteral("token")).toString();
    if (!newToken.isEmpty())
        Database::instance().putSetting(QStringLiteral("secretPortalToken"), newToken);

    // Read the secret from the pipe. Response success means the portal has already
    // written it, but do not trust a broken backend to have closed every duplicate
    // writer: make our read non-blocking so startup can never hang here.
    const int readFlags = ::fcntl(readFd, F_GETFL, 0);
    if (readFlags >= 0)
        ::fcntl(readFd, F_SETFL, readFlags | O_NONBLOCK);
    QByteArray secret;
    char buf[256];
    ssize_t n;
    while ((n = ::read(readFd, buf, sizeof(buf))) > 0)
        secret.append(buf, int(n));
    const int readError = errno;
    ::close(readFd);

    if (n < 0 && readError != EAGAIN && readError != EWOULDBLOCK) {
        qWarning() << "SecureStore: failed reading secret from portal";
        return {};
    }
    if (secret.size() < kKeyLen) {
        qWarning() << "SecureStore: Secret portal returned too few bytes";
        return {};
    }
    return secret;
}

QByteArray SecureStore::deriveKey(const QByteArray &context) const
{
    // HKDF-SHA256 via the OpenSSL 3.x EVP_KDF one-shot interface.
    QByteArray key(kKeyLen, Qt::Uninitialized);
    QByteArray salt = AppConfig::appId().toUtf8() + QByteArrayLiteral(".hkdf.v1");
    QByteArray info = context;

    EVP_KDF *kdf = EVP_KDF_fetch(nullptr, "HKDF", nullptr);
    if (!kdf)
        return {};
    EVP_KDF_CTX *kctx = EVP_KDF_CTX_new(kdf);
    EVP_KDF_free(kdf);
    if (!kctx)
        return {};

    char digest[] = "SHA256";
    OSSL_PARAM params[] = {
        OSSL_PARAM_construct_utf8_string(OSSL_KDF_PARAM_DIGEST, digest, 0),
        OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_KEY,
                                          const_cast<char *>(m_master.constData()),
                                          size_t(m_master.size())),
        OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_SALT, salt.data(),
                                          size_t(salt.size())),
        OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_INFO, info.data(),
                                          size_t(info.size())),
        OSSL_PARAM_construct_end()
    };

    const int rc = EVP_KDF_derive(kctx, reinterpret_cast<unsigned char *>(key.data()),
                                  kKeyLen, params);
    EVP_KDF_CTX_free(kctx);
    if (rc != 1)
        return {};
    return key;
}

QByteArray SecureStore::encrypt(const QByteArray &plaintext, const QByteArray &aad) const
{
    QByteArray nonce(kNonceLen, Qt::Uninitialized);
    if (RAND_bytes(reinterpret_cast<unsigned char *>(nonce.data()), kNonceLen) != 1) {
        qWarning() << "SecureStore: RAND_bytes failed generating nonce";
        return {};
    }
    const QByteArray key = deriveKey(QByteArrayLiteral("token-encryption"));
    if (key.isEmpty())
        return {}; // deriveKey failed (e.g. missing master secret); do not store

    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    if (!ctx)
        return {};

    QByteArray ct(plaintext.size(), Qt::Uninitialized);
    QByteArray tag(kTagLen, Qt::Uninitialized);
    int len = 0;
    int ctLen = 0;
    bool ok =
        EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) == 1 &&
        EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, kNonceLen, nullptr) == 1 &&
        EVP_EncryptInit_ex(ctx, nullptr, nullptr,
                           reinterpret_cast<const unsigned char *>(key.constData()),
                           reinterpret_cast<const unsigned char *>(nonce.constData())) == 1;
    // Authenticate the AAD (bound but not encrypted) before the plaintext.
    if (ok && !aad.isEmpty()) {
        int aadLen = 0;
        ok = EVP_EncryptUpdate(ctx, nullptr, &aadLen,
                               reinterpret_cast<const unsigned char *>(aad.constData()),
                               aad.size()) == 1;
    }
    ok = ok &&
         EVP_EncryptUpdate(ctx, reinterpret_cast<unsigned char *>(ct.data()), &len,
                           reinterpret_cast<const unsigned char *>(plaintext.constData()),
                           plaintext.size()) == 1;
    ctLen = len;
    ok = ok && EVP_EncryptFinal_ex(ctx, reinterpret_cast<unsigned char *>(ct.data()) + ctLen,
                                   &len) == 1;
    ctLen += len;
    ok = ok && EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, kTagLen, tag.data()) == 1;
    EVP_CIPHER_CTX_free(ctx);
    if (!ok)
        return {};
    ct.truncate(ctLen);

    QByteArray blob;
    blob.append(kBlobVersion);
    blob.append(nonce);
    blob.append(tag);
    blob.append(ct);
    return blob;
}

QByteArray SecureStore::decrypt(const QByteArray &blob, const QByteArray &aad) const
{
    if (blob.size() < 1 + kNonceLen + kTagLen || blob.at(0) != kBlobVersion)
        return {}; // too short, or a legacy 0x01 blob — treat as undecryptable
    const QByteArray nonce = blob.mid(1, kNonceLen);
    const QByteArray tag = blob.mid(1 + kNonceLen, kTagLen);
    const QByteArray ct = blob.mid(1 + kNonceLen + kTagLen);
    const QByteArray key = deriveKey(QByteArrayLiteral("token-encryption"));
    if (key.isEmpty())
        return {};

    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    if (!ctx)
        return {};

    QByteArray pt(ct.size(), Qt::Uninitialized);
    int len = 0;
    int ptLen = 0;
    bool ok =
        EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) == 1 &&
        EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, kNonceLen, nullptr) == 1 &&
        EVP_DecryptInit_ex(ctx, nullptr, nullptr,
                           reinterpret_cast<const unsigned char *>(key.constData()),
                           reinterpret_cast<const unsigned char *>(nonce.constData())) == 1;
    // Feed the same AAD before the ciphertext; a mismatch fails the tag check below.
    if (ok && !aad.isEmpty()) {
        int aadLen = 0;
        ok = EVP_DecryptUpdate(ctx, nullptr, &aadLen,
                               reinterpret_cast<const unsigned char *>(aad.constData()),
                               aad.size()) == 1;
    }
    ok = ok &&
         EVP_DecryptUpdate(ctx, reinterpret_cast<unsigned char *>(pt.data()), &len,
                           reinterpret_cast<const unsigned char *>(ct.constData()),
                           ct.size()) == 1;
    ptLen = len;
    ok = ok && EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, kTagLen,
                                   const_cast<char *>(tag.constData())) == 1;
    ok = ok && EVP_DecryptFinal_ex(ctx, reinterpret_cast<unsigned char *>(pt.data()) + ptLen,
                                   &len) == 1;
    ptLen += len;
    EVP_CIPHER_CTX_free(ctx);
    if (!ok)
        return {};
    pt.truncate(ptLen);
    return pt;
}

void SecureStore::store(const QString &key, const QByteArray &plaintext,
                        const SecretContext &ctx)
{
    masterSecret();
    const QByteArray blob = encrypt(plaintext, ctx.aad());
    if (blob.isEmpty()) {
        // Encryption failed (bad RNG / no master key): storing an empty blob would
        // masquerade as a stored secret and silently lose the real value.
        qWarning() << "SecureStore: encryption failed; not persisting secret for" << key;
        return;
    }
    Database::instance().putSecret(key, blob);
}

QByteArray SecureStore::retrieve(const QString &key, const SecretContext &ctx,
                                 RetrieveStatus *status)
{
    const QByteArray blob = Database::instance().getSecret(key);
    if (blob.isEmpty()) {
        if (status)
            *status = RetrieveStatus::Missing;
        return {};
    }
    // Do not initialize a key provider merely to discover that there is no saved
    // credential. In particular, a saved server without tokens must not make a
    // portal request on every launch.
    const QByteArray master = masterSecret();
    if (master.isEmpty()) {
        // The sticky provider (portal) could not be reached this run: the blob may
        // still be perfectly valid, so this is transient — do not treat as corrupt.
        if (status)
            *status = RetrieveStatus::ProviderUnavailable;
        return {};
    }
    const QByteArray pt = decrypt(blob, ctx.aad());
    if (pt.isEmpty()) {
        if (status)
            *status = RetrieveStatus::Undecryptable;
        return {};
    }
    if (status)
        *status = RetrieveStatus::Ok;
    return pt;
}

void SecureStore::remove(const QString &key)
{
    Database::instance().removeSecret(key);
}

#include "SecureStore.moc"
