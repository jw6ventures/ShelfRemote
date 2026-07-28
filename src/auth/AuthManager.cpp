#include "auth/AuthManager.h"
#include "app/AppConfig.h"
#include "app/UriHandler.h"
#include "auth/TokenStore.h"
#include "net/ApiClient.h"
#include "server/ServerProfile.h"
#include "storage/Database.h"

#include <QDesktopServices>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrlQuery>

AuthManager::AuthManager(ApiClient *api, TokenStore *tokens, UriHandler *uris, QObject *parent)
    : QObject(parent)
    , m_api(api)
    , m_tokens(tokens)
    , m_uris(uris)
{
    connect(m_uris, &UriHandler::oauthCallback, this, &AuthManager::onOauthCallback);
    connect(m_tokens, &TokenStore::refreshFailed, this, [this]() {
        setState(State::NeedsLogin);
    });
    // Persisted tokens exist but couldn't be unlocked (legacy blob, context change,
    // or the keyring provider was unavailable). Surface a distinct one-time message
    // and send the user to login; only the tokens were dropped.
    connect(m_tokens, &TokenStore::secretsUnreadable, this, [this]() {
        setLastError(tr("Your saved session couldn't be unlocked; please sign in again"));
        setState(State::NeedsLogin);
    });

    // If the browser handoff never comes back (user closed the tab, IdP error page,
    // etc.) don't sit in Authenticating forever.
    m_oidcTimeout.setSingleShot(true);
    m_oidcTimeout.setInterval(120000); // 2 minutes
    connect(&m_oidcTimeout, &QTimer::timeout, this, [this]() {
        if (!m_oidcInProgress)
            return;
        m_oidcInProgress = false;
        setLastError(tr("OpenID login timed out; please try again"));
        setState(State::NeedsLogin);
    });
}

void AuthManager::setState(State s)
{
    if (m_state == s)
        return;
    m_state = s;
    emit stateChanged();
}

void AuthManager::setError(const QString &msg)
{
    m_lastError = msg;
    emit errorChanged();
    setState(State::Error);
}

void AuthManager::setLastError(const QString &msg)
{
    m_lastError = msg;
    emit errorChanged();
}

void AuthManager::clearError()
{
    if (m_lastError.isEmpty())
        return;
    m_lastError.clear();
    emit errorChanged();
}

void AuthManager::cancelAuth()
{
    m_oidcTimeout.stop();
    m_oidcInProgress = false;
    if (m_state == State::Authenticating || m_state == State::Checking)
        setState(State::NeedsLogin);
}

void AuthManager::checkServer(const QUrl &baseUrl)
{
    m_api->setBaseUrl(baseUrl);
    // Changing origin: drop any prior server's in-memory bearer and cookies so the
    // public /status, /login, and OIDC requests below never carry another server's
    // credentials. The prior server's *persisted* tokens are left untouched.
    m_api->clearCookies();
    m_tokens->resetMemory();
    // Namespace persisted tokens/secrets by this server so they survive relaunch.
    m_tokens->setServerKey(ServerProfile::idForUrl(baseUrl));
    // Drop any prior server's account binding; the real one is set once we know the
    // logged-in user (applyLoginPayload), before any token is persisted.
    m_tokens->setAccount(QString());
    m_lastError.clear();
    emit errorChanged();
    setState(State::Checking);
    m_api->get(m_api->endpoints().status(), [this](const ApiResponse &res) {
        if (res.stale)
            return; // the user pointed us at a different server since this /status
        if (!res.ok) {
            setError(res.errorString.isEmpty()
                         ? tr("Server unreachable (HTTP %1)").arg(res.status)
                         : res.errorString);
            return;
        }
        const QJsonObject obj = res.json();
        m_serverVersion = obj.value(QStringLiteral("serverVersion")).toString();
        m_authMethods.clear();
        for (const auto &v : obj.value(QStringLiteral("authMethods")).toArray())
            m_authMethods << v.toString();
        if (m_authMethods.isEmpty())
            m_authMethods << QStringLiteral("local"); // sane default
        m_oidcButtonText = obj.value(QStringLiteral("authFormData")).toObject()
                               .value(QStringLiteral("authOpenIDButtonText"))
                               .toString(m_oidcButtonText);
        emit statusChanged();
        emit authMethodsChanged();
        setState(State::NeedsLogin);
    });
}

void AuthManager::loginLocal(const QString &username, const QString &password)
{
    clearError();
    setState(State::Authenticating);
    QJsonObject body{{QStringLiteral("username"), username},
                     {QStringLiteral("password"), password}};
    // Audiobookshelf only returns user.refreshToken when the caller opts in with
    // x-return-tokens; without it we could never restore or refresh the session.
    QNetworkRequest req = m_api->makeRequest(m_api->endpoints().login());
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    req.setRawHeader("x-return-tokens", "true");
    m_api->send("POST", req, QJsonDocument(body).toJson(QJsonDocument::Compact),
                [this](const ApiResponse &res) {
        if (res.stale)
            return; // switched servers mid-login; do not persist under the new key
        if (!res.ok) {
            // Surface it: loginFailed alone was not consumed anywhere, so a rejected
            // login otherwise looked like nothing happened. lastError is shown by
            // the login screen while the form stays usable for a retry.
            setLastError(tr("Invalid username or password"));
            emit loginFailed(m_lastError);
            setState(State::NeedsLogin);
            return;
        }
        applyLoginPayload(res.json());
    }, /*followRedirects*/ false);
}

void AuthManager::beginOidc()
{
    // OIDC requires HTTPS; refuse to start against an insecure server. Keep the
    // login form usable (NeedsLogin) rather than dropping into the Error state.
    if (m_api->baseUrl().scheme() != QStringLiteral("https")) {
        setLastError(tr("OpenID login requires an https server URL"));
        return;
    }

    clearError();
    setState(State::Authenticating);
    m_pkce = Pkce::generate();
    m_oidcInProgress = true;

    QUrlQuery q;
    q.addQueryItem(QStringLiteral("code_challenge"), m_pkce.codeChallenge);
    q.addQueryItem(QStringLiteral("code_challenge_method"), QStringLiteral("S256"));
    q.addQueryItem(QStringLiteral("redirect_uri"), AppConfig::oauthRedirectUri());
    q.addQueryItem(QStringLiteral("client_id"), AppConfig::clientId());
    q.addQueryItem(QStringLiteral("response_type"), QStringLiteral("code"));
    // Audiobookshelf stores our state in an auth_state cookie and echoes it back
    // on the callback, so it MUST be sent here or the returned state is empty.
    q.addQueryItem(QStringLiteral("state"), m_pkce.state);

    // The APPLICATION makes this request (not the browser) with redirects
    // DISABLED so the Audiobookshelf session cookie lands in our own cookie jar.
    m_api->get(m_api->endpoints().openidStart(q), [this](const ApiResponse &res) {
        if (res.stale) {
            m_oidcInProgress = false; // switched servers; abandon this handoff
            return;
        }
        // Expect a 3xx with a Location pointing at the identity provider.
        const QUrl idpUrl = res.redirectLocation;
        if (idpUrl.isEmpty()) {
            m_oidcInProgress = false;
            setLastError(tr("Server did not return an OpenID redirect"));
            setState(State::NeedsLogin);
            return;
        }
        // The Location is server-controlled. Only ever hand a web URL to the
        // browser: a non-http(s) scheme (file:, custom app schemes, ...) could be
        // abused to launch something unexpected via the desktop portal. HTTPS is
        // strongly preferred; plain http is tolerated for self-hosted IdPs.
        const QString idpScheme = idpUrl.scheme().toLower();
        if (idpScheme != QStringLiteral("https") && idpScheme != QStringLiteral("http")) {
            m_oidcInProgress = false;
            setLastError(tr("Server returned an unsupported OpenID redirect"));
            setState(State::NeedsLogin);
            return;
        }
        // Hand off to the user's browser via the desktop portal. If that fails,
        // there is no way to complete the flow, so report it instead of hanging.
        if (!QDesktopServices::openUrl(idpUrl)) {
            m_oidcInProgress = false;
            setLastError(tr("Could not open a browser for OpenID login"));
            setState(State::NeedsLogin);
            return;
        }
        // Now we wait for the jw6-shelfremote://oauth callback (onOauthCallback),
        // bounded by m_oidcTimeout so an abandoned login recovers on its own.
        m_oidcTimeout.start();
    }, /*followRedirects*/ false);
}

void AuthManager::onOauthCallback(const QString &code, const QString &state, const QString &error)
{
    if (!m_oidcInProgress)
        return;
    m_oidcTimeout.stop(); // the callback arrived; cancel the abandon timer
    if (!error.isEmpty()) {
        m_oidcInProgress = false;
        setError(tr("OpenID login failed: %1").arg(error));
        return;
    }
    if (state != m_pkce.state) {
        m_oidcInProgress = false;
        setError(tr("OpenID state mismatch; aborting for safety"));
        return;
    }
    exchangeCode(code, state);
}

void AuthManager::exchangeCode(const QString &code, const QString &state)
{
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("state"), state);
    q.addQueryItem(QStringLiteral("code"), code);
    q.addQueryItem(QStringLiteral("code_verifier"), m_pkce.codeVerifier);

    // Reuse the SAME cookie jar from beginOidc() for the code exchange.
    m_api->get(m_api->endpoints().openidCallback(q), [this](const ApiResponse &res) {
        m_oidcInProgress = false;
        if (res.stale)
            return; // switched servers mid-exchange; do not persist under the new key
        if (!res.ok) {
            setError(tr("Code exchange failed (HTTP %1)").arg(res.status));
            return;
        }
        applyLoginPayload(res.json());
    });
}

void AuthManager::applyLoginPayload(const QJsonObject &payload)
{
    // /login and the OIDC callback return the same shape: a "user" object plus
    // accessToken / refreshToken (either at root or nested under user).
    const QJsonObject user = payload.value(QStringLiteral("user")).toObject();
    const QString access = payload.value(QStringLiteral("accessToken")).toString(
        user.value(QStringLiteral("accessToken")).toString());
    const QString refresh = payload.value(QStringLiteral("refreshToken")).toString(
        user.value(QStringLiteral("refreshToken")).toString());
    if (access.isEmpty()) {
        setError(tr("Login response did not include an access token"));
        return;
    }
    // Bind the persisted tokens to this account (AAD) BEFORE setTokens() persists
    // them; the same id is saved on the server row for a matching restore later.
    m_tokens->setAccount(user.value(QStringLiteral("id")).toString());
    m_tokens->setTokens(access, refresh);
    m_user = user.isEmpty() ? payload : user;
    authorizeAndFinish();
}

void AuthManager::authorizeAndFinish()
{
    // POST /api/authorize verifies the token and returns full user + server
    // context (accessible libraries, settings, default library).
    m_api->post(m_api->endpoints().authorize(), QJsonObject{}, [this](const ApiResponse &res) {
        if (res.stale)
            return; // switched servers; the new flow drives its own authorize
        if (!res.ok) {
            // The token is revoked/expired (401/403) or the server is unreachable.
            // Either way we must NOT open the authenticated UI or save the server.
            if (res.status == 401 || res.status == 403)
                m_tokens->clear();
            setLastError(tr("Your session could not be verified; please sign in again"));
            emit loginFailed(m_lastError);
            setState(State::NeedsLogin);
            return;
        }
        const QJsonObject obj = res.json();
        const QJsonObject user = obj.value(QStringLiteral("user")).toObject();
        if (!user.isEmpty())
            m_user = user;
        // Re-bind persisted tokens to the authoritative account id from /authorize
        // and rewrite them, so the AAD matches the id we save on the server row
        // (lastUserId) and decrypts on the next restore — even if the login payload
        // carried no user object. Idempotent when the ids already agree.
        const QString authAccount = m_user.value(QStringLiteral("id")).toString();
        if (!authAccount.isEmpty()) {
            m_tokens->setAccount(authAccount);
            m_tokens->persist();
        }
        clearError();
        setState(State::Authenticated);
        emit authenticated(m_user);
    });
}

void AuthManager::logout()
{
    // Tear the session down while the access token is STILL valid: this lets the
    // playback layer flush a final authenticated /close and stop mpv, and lets the
    // content/progress layers drop this server's state, before we revoke tokens.
    // Doing this first prevents a lingering session from later syncing or resolving
    // its track URLs against whatever server is selected next.
    emit sessionEnding();

    QNetworkRequest req = m_api->makeRequest(m_api->endpoints().logout());
    req.setRawHeader("x-refresh-token", m_tokens->refreshToken().toUtf8());
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    m_api->send("POST", req, QByteArray("{}"), [this](const ApiResponse &res) {
        // The server may return an identity-provider logout URL. Ignore a stale
        // reply (origin changed under us) rather than clearing the new session.
        if (res.stale)
            return;
        const QString idpLogout = res.json().value(QStringLiteral("logoutUrl")).toString();
        if (!idpLogout.isEmpty())
            QDesktopServices::openUrl(QUrl(idpLogout));
        m_tokens->clear();
        m_user = QJsonObject();
        setState(State::NeedsLogin);
    }, /*followRedirects*/ false);
}

bool AuthManager::restoreSession(const QUrl &baseUrl, const QString &serverKey)
{
    m_api->setBaseUrl(baseUrl);
    m_tokens->setServerKey(serverKey);
    // Bind the AAD to the saved account for this server so the persisted tokens
    // decrypt in the same context they were written (see applyLoginPayload).
    QString accountId;
    for (const auto &row : Database::instance().servers()) {
        if (row.id == serverKey) {
            accountId = row.lastUserId;
            break;
        }
    }
    m_tokens->setAccount(accountId);
    if (!m_tokens->load())
        return false;

    setState(State::Authenticating);
    auto proceed = [this]() { authorizeAndFinish(); };
    if (m_tokens->needsRefresh())
        m_tokens->refresh([this, proceed](bool ok) {
            if (ok) proceed(); else setState(State::NeedsLogin);
        });
    else
        proceed();
    return true;
}
