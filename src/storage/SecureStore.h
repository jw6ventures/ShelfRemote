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
// The chosen provider is persisted (setting "secretProvider") and never changes on
// its own after credentials exist; only an explicit setStorage() moves it. The
// portal's opaque continuation token (if any) is persisted (setting
// "secretPortalToken") and replayed on later acquisitions.
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

    // Where the master secret lives, as the user sees it.
    enum class Storage {
        Keyring, // "portal-v1": the system keyring, via the Secret portal
        Device   // "local-v1": master.key (0600) in the app's data directory
    };
    enum class SwitchResult {
        Unchanged, // already there
        Moved,     // same master secret, new home: every saved secret still opens
        Reset,     // a new master secret: secrets saved under the old one are gone
        Failed     // the target could not be set up; nothing changed
    };
    Storage storage() const;
    // Whether the keyring is an option at all (only through the Flatpak portal).
    static bool keyringSupported() { return runningUnderFlatpak(); }
    // Moves the master secret to `target`. Keyring -> Device writes the current
    // master secret to master.key, so nothing needs re-encrypting; if the keyring
    // can't hand it over (locked), a new one is generated and the old secrets are
    // removed (Reset). Device -> Keyring adopts the portal's secret and deletes
    // master.key; secrets kept under a different key are removed (Reset). Callers
    // re-save whatever credentials they hold in memory after a Reset.
    SwitchResult setStorage(Storage target);

private:
    enum class Provider { Unset, Portal, Local };

    QByteArray masterSecret();            // cached; acquires on first use
    QByteArray acquireMasterSecret();     // sticky provider → portal or local
    QByteArray acquirePortalSecret();     // QtDBus Secret portal; empty on failure
    QByteArray acquireLocalSecret();      // 0600 master.key (generated if absent)
    static bool writeLocalSecret(const QByteArray &secret); // replaces master.key
    static bool runningUnderFlatpak();

    QByteArray deriveKey(const QByteArray &context) const;
    QByteArray encrypt(const QByteArray &plaintext, const QByteArray &aad) const;
    QByteArray decrypt(const QByteArray &blob, const QByteArray &aad) const;

    QByteArray m_master;
    Provider   m_provider = Provider::Unset;
};
