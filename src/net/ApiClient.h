#pragma once

#include <QByteArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkRequest>
#include <QObject>
#include <QSet>
#include <QUrl>
#include <functional>

#include "net/Endpoints.h"

class QNetworkAccessManager;
class QNetworkReply;
class QNetworkCookieJar;

// Result of a single HTTP call.
struct ApiResponse {
    int         status = 0;         // HTTP status code (0 == transport error)
    bool        ok = false;         // status in [200,299] and no transport error
    // True when the ApiClient's base URL (origin) changed between this request
    // being sent and it completing. A stale body may belong to a *previous*
    // server: callers MUST NOT use it to mutate current-session state or
    // persistence, and the client itself never auto-retries a stale 401.
    bool        stale = false;
    QByteArray  body;
    QString     errorString;
    QUrl        redirectLocation;   // raw Location header (redirects not followed)
    QNetworkReply *reply = nullptr; // valid only inside the callback

    QJsonObject json() const;
    QJsonDocument jsonDoc() const;
};

using ApiCallback = std::function<void(const ApiResponse &)>;

// Central HTTP client. Every request carries the bearer token as a header (never
// in the URL) and shares one cookie jar so the Audiobookshelf session cookie set
// during OIDC start is reused for the code exchange. Owns the Endpoints builder.
class ApiClient : public QObject
{
    Q_OBJECT
public:
    explicit ApiClient(QObject *parent = nullptr);
    ~ApiClient() override;

    void setBaseUrl(const QUrl &base);
    QUrl baseUrl() const { return m_endpoints.base(); }
    const Endpoints &endpoints() const { return m_endpoints; }

    // Monotonic counter bumped whenever the base URL (origin) changes. Callers can
    // capture it before an async flow and compare afterwards to detect that the
    // client was pointed at a different server underneath them.
    quint64 epoch() const { return m_epoch; }

    void setAccessToken(const QString &token);
    QString accessToken() const { return m_accessToken; }

    // Installs a handler invoked when a request comes back 401. It should attempt
    // a token refresh and call its argument with the outcome; on success the
    // original request is retried once with the refreshed bearer token. Wired to
    // TokenStore::refresh so any long-running session recovers from expiry.
    using RefreshHandler = std::function<void(std::function<void(bool)>)>;
    void setUnauthorizedHandler(RefreshHandler handler) { m_onUnauthorized = std::move(handler); }

    // Set on a request that must NOT trigger the 401-refresh retry (the refresh
    // request itself, which would otherwise recurse into the refresh handler).
    static constexpr QNetworkRequest::Attribute NoAuthRetryAttribute =
        static_cast<QNetworkRequest::Attribute>(QNetworkRequest::User + 1);

    // Clears the shared cookie jar (used between distinct login attempts).
    void clearCookies();

    // JSON verbs. `followRedirects` defaults true; the OIDC start disables it.
    void get(const QUrl &url, ApiCallback cb, bool followRedirects = true);
    void post(const QUrl &url, const QJsonObject &body, ApiCallback cb, bool followRedirects = true);
    void post(const QUrl &url, const QByteArray &rawBody, const QByteArray &contentType,
              ApiCallback cb, bool followRedirects = true);
    void patch(const QUrl &url, const QJsonObject &body, ApiCallback cb);
    void del(const QUrl &url, ApiCallback cb);

    // Adds an arbitrary header to the NEXT request only (e.g. x-refresh-token).
    // Returns *this for chaining is avoided; use the per-call overloads instead.

    // Raw binary GET (covers). Still authenticated via the bearer header.
    void getRaw(const QUrl &url, ApiCallback cb);

    // Access the underlying request template so callers can add one-off headers.
    QNetworkRequest makeRequest(const QUrl &url) const;
    void send(const QByteArray &verb, QNetworkRequest req, const QByteArray &body,
              ApiCallback cb, bool followRedirects);

signals:
    void accessTokenChanged();

private:
    void sendImpl(const QByteArray &verb, QNetworkRequest req, const QByteArray &body,
                  ApiCallback cb, bool followRedirects, bool allowRetry);
    static ApiResponse buildResponse(QNetworkReply *reply);

    QNetworkAccessManager *m_nam = nullptr;
    QNetworkCookieJar     *m_jar = nullptr;
    Endpoints              m_endpoints;
    QString                m_accessToken;
    RefreshHandler         m_onUnauthorized;
    quint64                m_epoch = 0;   // bumped on every origin change
    QSet<QNetworkReply *>  m_active;      // in-flight replies, aborted on origin change
};
