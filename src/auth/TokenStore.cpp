#include "auth/TokenStore.h"
#include "net/ApiClient.h"
#include "storage/SecureStore.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QTimeZone>

TokenStore::TokenStore(ApiClient *api, SecureStore *secure, QObject *parent)
    : QObject(parent)
    , m_api(api)
    , m_secure(secure)
{
}

void TokenStore::setServerKey(const QString &serverKey)
{
    m_serverKey = serverKey;
}

void TokenStore::setAccount(const QString &accountId)
{
    m_accountId = accountId;
}

void TokenStore::setTokens(const QString &access, const QString &refresh)
{
    m_access = access;
    m_refresh = refresh;
    m_accessExpiry = expiryFromJwt(access);
    if (m_api)
        m_api->setAccessToken(access);
    persist();
    emit tokensChanged();
}

void TokenStore::clear()
{
    m_access.clear();
    m_refresh.clear();
    m_accessExpiry = QDateTime();
    if (m_api)
        m_api->setAccessToken(QString());
    if (m_secure && !m_serverKey.isEmpty())
        m_secure->remove(m_serverKey + QStringLiteral("/tokens"));
    emit tokensChanged();
}

void TokenStore::resetMemory()
{
    m_access.clear();
    m_refresh.clear();
    m_accessExpiry = QDateTime();
    if (m_api)
        m_api->setAccessToken(QString());
    // Intentionally does NOT touch the secrets store: the previous server's
    // persisted tokens stay put so restoreSession() can recover them later.
    emit tokensChanged();
}

bool TokenStore::needsRefresh(int skewSeconds) const
{
    if (!m_accessExpiry.isValid())
        return false; // unknown expiry: refresh reactively on 401 instead
    return QDateTime::currentDateTimeUtc().addSecs(skewSeconds) >= m_accessExpiry;
}

QDateTime TokenStore::expiryFromJwt(const QString &jwt)
{
    const QStringList parts = jwt.split(QLatin1Char('.'));
    if (parts.size() < 2)
        return {};
    QByteArray payload = parts.at(1).toUtf8();
    // JWT uses base64url without padding.
    const QByteArray decoded =
        QByteArray::fromBase64(payload, QByteArray::Base64UrlEncoding);
    const QJsonObject obj = QJsonDocument::fromJson(decoded).object();
    if (!obj.contains(QStringLiteral("exp")))
        return {};
    return QDateTime::fromSecsSinceEpoch(qint64(obj.value(QStringLiteral("exp")).toDouble()),
                                         QTimeZone::UTC);
}

bool TokenStore::load()
{
    if (!m_secure || m_serverKey.isEmpty())
        return false;
    const SecretContext ctx{m_serverKey, m_accountId, QStringLiteral("tokens"), 1};
    SecureStore::RetrieveStatus status = SecureStore::RetrieveStatus::Missing;
    const QByteArray blob =
        m_secure->retrieve(m_serverKey + QStringLiteral("/tokens"), ctx, &status);
    if (blob.isEmpty()) {
        // A present-but-undecryptable blob (legacy/AAD mismatch) is permanently
        // dead: purge it so we don't retry every launch, then ask for a re-login.
        // A transient provider outage keeps the blob for a later successful unlock.
        if (status == SecureStore::RetrieveStatus::Undecryptable) {
            m_secure->remove(m_serverKey + QStringLiteral("/tokens"));
            emit secretsUnreadable();
        } else if (status == SecureStore::RetrieveStatus::ProviderUnavailable) {
            emit secretsUnreadable();
        }
        return false;
    }
    const QJsonObject obj = QJsonDocument::fromJson(blob).object();
    m_access = obj.value(QStringLiteral("access")).toString();
    m_refresh = obj.value(QStringLiteral("refresh")).toString();
    m_accessExpiry = expiryFromJwt(m_access);
    if (m_api && !m_access.isEmpty())
        m_api->setAccessToken(m_access);
    return hasTokens();
}

void TokenStore::persist() const
{
    if (!m_secure || m_serverKey.isEmpty())
        return;
    QJsonObject obj;
    obj[QStringLiteral("access")] = m_access;
    obj[QStringLiteral("refresh")] = m_refresh;
    const SecretContext ctx{m_serverKey, m_accountId, QStringLiteral("tokens"), 1};
    m_secure->store(m_serverKey + QStringLiteral("/tokens"),
                    QJsonDocument(obj).toJson(QJsonDocument::Compact), ctx);
}

void TokenStore::refresh(std::function<void(bool)> cb)
{
    if (m_refresh.isEmpty()) {
        if (cb) cb(false);
        return;
    }
    // Single-flight: if a refresh is already running, wait for its result rather
    // than firing a second /auth/refresh (which would race and rotate twice).
    if (cb)
        m_pending.push_back(std::move(cb));
    if (m_refreshing)
        return;
    m_refreshing = true;

    QNetworkRequest req = m_api->makeRequest(m_api->endpoints().authRefresh());
    req.setRawHeader("x-refresh-token", m_refresh.toUtf8());
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    // This IS the refresh request; a 401 here must route to login, not recurse.
    req.setAttribute(ApiClient::NoAuthRetryAttribute, true);

    auto settle = [this](bool ok) {
        m_refreshing = false;
        std::vector<std::function<void(bool)>> waiters;
        waiters.swap(m_pending);
        for (auto &w : waiters)
            if (w) w(ok);
    };

    // Bind this refresh to the server it was started for. If the client is pointed
    // at a different server (res.stale) or our key changes before the response
    // arrives, discard it: rotating another server's tokens into this key would
    // corrupt its stored session. Settle the waiters false WITHOUT refreshFailed so
    // we don't kick the now-current server to the login screen.
    const QString key = m_serverKey;
    m_api->send("POST", req, QByteArray("{}"), [this, settle, key](const ApiResponse &res) {
        if (res.stale || key != m_serverKey) {
            settle(false);
            return;
        }
        if (!res.ok) {
            emit refreshFailed();
            settle(false);
            return;
        }
        const QJsonObject obj = res.json();
        // The server returns the user payload shape; tokens may be at the root
        // or nested under "user".
        const QJsonObject user = obj.value(QStringLiteral("user")).toObject();
        const QString access =
            obj.value(QStringLiteral("accessToken")).toString(
                user.value(QStringLiteral("accessToken")).toString());
        QString rotatedRefresh =
            obj.value(QStringLiteral("refreshToken")).toString(
                user.value(QStringLiteral("refreshToken")).toString());
        if (rotatedRefresh.isEmpty())
            rotatedRefresh = m_refresh; // keep existing if server did not rotate
        if (access.isEmpty()) {
            emit refreshFailed();
            settle(false);
            return;
        }
        setTokens(access, rotatedRefresh);
        settle(true);
    }, /*followRedirects*/ false);
}
