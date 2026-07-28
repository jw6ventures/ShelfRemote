#pragma once

#include <QDateTime>
#include <QObject>
#include <QString>
#include <functional>
#include <vector>

class ApiClient;
class SecureStore;

// Holds the Audiobookshelf access + refresh tokens for the active server,
// persists them encrypted, decodes the JWT expiry for proactive refresh, and
// performs POST /auth/refresh with x-refresh-token, rotating both tokens.
class TokenStore : public QObject
{
    Q_OBJECT
public:
    TokenStore(ApiClient *api, SecureStore *secure, QObject *parent = nullptr);

    void setServerKey(const QString &serverKey); // namespaces persisted tokens
    // The account (server user id) this session belongs to. Bound into the secret's
    // AAD, so it MUST be set before setTokens()/load() and match across persist and
    // restore for the same server.
    void setAccount(const QString &accountId);
    void setTokens(const QString &access, const QString &refresh);
    void clear();

    // Drops the in-memory tokens (and the bearer the ApiClient would attach) WITHOUT
    // deleting the persisted secret. Used when pointing at a different server so
    // one server's bearer is never sent to another; the previous server's saved
    // tokens remain intact for a later restore.
    void resetMemory();

    QString accessToken() const { return m_access; }
    QString refreshToken() const { return m_refresh; }
    bool hasTokens() const { return !m_access.isEmpty() && !m_refresh.isEmpty(); }

    // True when now is within `skewSeconds` of the access-token expiry.
    bool needsRefresh(int skewSeconds = 60) const;
    QDateTime accessExpiry() const { return m_accessExpiry; }

    // Loads persisted tokens for the current server key. Returns hasTokens().
    bool load();
    void persist() const;

    // Performs POST /auth/refresh. On success updates + persists tokens and calls
    // cb(true). On failure cb(false); the caller should route the user to login.
    void refresh(std::function<void(bool)> cb);

    // Parses the "exp" claim from a JWT without verifying the signature (the
    // server is the authority; this is only used to schedule proactive refresh).
    static QDateTime expiryFromJwt(const QString &jwt);

signals:
    void tokensChanged();
    void refreshFailed();
    // Persisted tokens exist for this server but could not be unlocked (a legacy
    // blob, an AAD/context mismatch, or the sticky secret provider was unavailable).
    // The caller should route the user to a one-time re-login.
    void secretsUnreadable();

private:
    ApiClient   *m_api;
    SecureStore *m_secure;
    QString      m_serverKey;
    QString      m_accountId;
    QString      m_access;
    QString      m_refresh;
    QDateTime    m_accessExpiry;
    bool         m_refreshing = false;
    std::vector<std::function<void(bool)>> m_pending; // waiters for an in-flight refresh
};
