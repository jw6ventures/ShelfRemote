#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>

// Authenticated associated data (AAD) bound into every encrypted secret so a blob
// can only be decrypted in the exact context it was written: same server, same
// account, same credential type, same schema version. A mismatch (or a legacy
// blob) fails decryption, which the caller turns into a targeted one-time re-login.
struct SecretContext {
    QString serverId;
    QString accountId;
    QString credType;        // e.g. "tokens"
    int     schemaVersion = 1;

    // serverId \0 accountId \0 credType \0 schemaVersion
    QByteArray aad() const;
};

// Encrypts small secrets (tokens) at rest with AES-256-GCM + AAD binding. The key
// is derived via HKDF-SHA256 from a per-application master secret.
//
// The master-secret provider is chosen ONCE, at initial setup, and made sticky:
//   * "portal-v1": the XDG Secret portal (a stable per-app secret from the user's
//     keyring), used when running under Flatpak. Once chosen, the app never
//     silently falls back to a local key — a portal failure fails closed.
//   * "local-v1": a random secret generated once and stored 0600 in the app data
//     dir, for sessions without a portal.
// An existing master.key from a release predating this marker is migrated to
// "local-v1" so an upgrade never strands credentials under a different key.
// The chosen provider is persisted (setting "secretProvider") and never changed
// after credentials exist. The portal's opaque continuation token (if any) is
// persisted (setting "secretPortalToken") and replayed on later acquisitions.
//
// Ciphertext layout in the DB: [1 byte version=0x02][12 byte nonce][16 byte tag][ct].
class SecureStore : public QObject
{
    Q_OBJECT
public:
    explicit SecureStore(QObject *parent = nullptr);

    // Outcome of a retrieve(), so a caller can tell a missing secret from one that
    // exists but cannot be decrypted, and a permanent failure (legacy/AAD mismatch)
    // from a transient one (the sticky portal provider was momentarily unavailable).
    enum class RetrieveStatus {
        Ok,
        Missing,             // no row for this key
        Undecryptable,       // row exists but decrypt failed under a valid master key
        ProviderUnavailable  // master secret could not be acquired (transient)
    };

    // Encrypts and persists `plaintext` under `key`, bound to `ctx`.
    void store(const QString &key, const QByteArray &plaintext, const SecretContext &ctx);

    // Returns decrypted plaintext, or empty on any non-Ok status (reported via
    // `status` when provided).
    QByteArray retrieve(const QString &key, const SecretContext &ctx,
                        RetrieveStatus *status = nullptr);

    void remove(const QString &key);

private:
    enum class Provider { Unset, Portal, Local };

    QByteArray masterSecret();            // cached; acquires on first use
    QByteArray acquireMasterSecret();     // sticky provider → portal or local
    QByteArray acquirePortalSecret();     // QtDBus Secret portal; empty on failure
    QByteArray acquireLocalSecret();      // 0600 master.key (generated if absent)
    static bool runningUnderFlatpak();

    QByteArray deriveKey(const QByteArray &context) const;
    QByteArray encrypt(const QByteArray &plaintext, const QByteArray &aad) const;
    QByteArray decrypt(const QByteArray &blob, const QByteArray &aad) const;

    QByteArray m_master;
    Provider   m_provider = Provider::Unset;
};
