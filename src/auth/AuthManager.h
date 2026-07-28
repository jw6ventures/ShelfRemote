#pragma once

#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QUrl>

#include "auth/Pkce.h"

class ApiClient;
class TokenStore;
class UriHandler;

// Orchestrates the full auth lifecycle exposed to QML: server discovery via
// /status, local username/password login, and the exact desktop OIDC/PKCE flow
// (app-initiated /auth/openid with the client's own cookie jar, browser handoff,
// custom-scheme callback, code exchange, /api/authorize verification).
class AuthManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(State state READ state NOTIFY stateChanged)
    Q_PROPERTY(bool isAuthenticated READ isAuthenticated NOTIFY stateChanged)
    Q_PROPERTY(bool isBusy READ isBusy NOTIFY stateChanged)
    Q_PROPERTY(bool needsLogin READ needsLogin NOTIFY stateChanged)
    Q_PROPERTY(bool hasError READ hasError NOTIFY stateChanged)
    Q_PROPERTY(bool supportsLocal READ supportsLocal NOTIFY authMethodsChanged)
    Q_PROPERTY(bool supportsOidc READ supportsOidc NOTIFY authMethodsChanged)
    Q_PROPERTY(QString oidcButtonText READ oidcButtonText NOTIFY authMethodsChanged)
    Q_PROPERTY(QString serverVersion READ serverVersion NOTIFY statusChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY errorChanged)

public:
    enum class State { Disconnected, Checking, NeedsLogin, Authenticating, Authenticated, Error };
    Q_ENUM(State)

    AuthManager(ApiClient *api, TokenStore *tokens, UriHandler *uris, QObject *parent = nullptr);

    State state() const { return m_state; }
    bool isAuthenticated() const { return m_state == State::Authenticated; }
    bool isBusy() const { return m_state == State::Checking || m_state == State::Authenticating; }
    bool needsLogin() const { return m_state == State::NeedsLogin; }
    bool hasError() const { return m_state == State::Error; }
    bool supportsLocal() const { return m_authMethods.contains(QStringLiteral("local")); }
    bool supportsOidc() const { return m_authMethods.contains(QStringLiteral("openid")); }
    QString oidcButtonText() const { return m_oidcButtonText; }
    QString serverVersion() const { return m_serverVersion; }
    QString lastError() const { return m_lastError; }
    QJsonObject user() const { return m_user; }

    // Reachability + /status discovery. Sets base URL on the ApiClient.
    Q_INVOKABLE void checkServer(const QUrl &baseUrl);

    // POST /login with username/password.
    Q_INVOKABLE void loginLocal(const QString &username, const QString &password);

    // Step 2 of the OIDC flow: app-initiated /auth/openid, then browser handoff.
    Q_INVOKABLE void beginOidc();

    // POST /logout with x-refresh-token; opens IdP logout URL when returned.
    Q_INVOKABLE void logout();

    // Attempts to restore a previously authenticated session for a saved server.
    Q_INVOKABLE bool restoreSession(const QUrl &baseUrl, const QString &serverKey);

    // Aborts an in-progress OIDC/authenticating attempt and returns to the login
    // form (e.g. the user abandoned the browser handoff).
    Q_INVOKABLE void cancelAuth();

signals:
    void stateChanged();
    void authMethodsChanged();
    void statusChanged();
    void errorChanged();
    void authenticated(const QJsonObject &user);
    void loginFailed(const QString &reason);
    // Emitted at the very start of logout(), while the access token is still valid,
    // so listeners can close the playback session (final /close), stop mpv, and
    // reset per-session UI state BEFORE credentials are cleared. Centralizes the
    // "end of session" transition the same way authenticated() drives its start.
    void sessionEnding();

private slots:
    void onOauthCallback(const QString &code, const QString &state, const QString &error);

private:
    void setState(State s);
    void setError(const QString &msg);
    // Sets/clears lastError WITHOUT forcing the Error state, so a failed login can
    // show a message while the login form stays usable for a retry.
    void setLastError(const QString &msg);
    void clearError();
    void exchangeCode(const QString &code, const QString &state);
    void authorizeAndFinish(); // POST /api/authorize after tokens acquired
    void applyLoginPayload(const QJsonObject &payload);

    ApiClient   *m_api;
    TokenStore  *m_tokens;
    UriHandler  *m_uris;

    State        m_state = State::Disconnected;
    QStringList  m_authMethods;
    QString      m_oidcButtonText = QStringLiteral("Login with OpenID");
    QString      m_serverVersion;
    QString      m_lastError;
    QJsonObject  m_user;

    Pkce         m_pkce;             // live only during an OIDC attempt
    bool         m_oidcInProgress = false;
    QTimer       m_oidcTimeout;      // caps how long we wait for the browser callback
};
