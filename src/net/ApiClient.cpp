#include "net/ApiClient.h"
#include "app/AppConfig.h"

#include <QNetworkAccessManager>
#include <QNetworkCookieJar>
#include <QNetworkReply>

#include <chrono>

QJsonDocument ApiResponse::jsonDoc() const
{
    return QJsonDocument::fromJson(body);
}

QJsonObject ApiResponse::json() const
{
    return jsonDoc().object();
}

ApiClient::ApiClient(QObject *parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
    , m_jar(new QNetworkCookieJar(this))
{
    m_nam->setCookieJar(m_jar);
    // We manage redirect policy per-request; keep the manager neutral.
    m_nam->setRedirectPolicy(QNetworkRequest::ManualRedirectPolicy);
}

ApiClient::~ApiClient() = default;

void ApiClient::setAccessToken(const QString &token)
{
    if (m_accessToken == token)
        return;
    m_accessToken = token;
    emit accessTokenChanged();
}

void ApiClient::setBaseUrl(const QUrl &base)
{
    if (m_endpoints.base() == base)
        return; // same origin: nothing to invalidate
    m_endpoints.setBase(base);
    // Origin changed. Bump the epoch so every request already in flight is marked
    // stale on completion (and, crucially, a 401 from the old server is never
    // refreshed/retried with the *new* server's bearer token). Abort those replies
    // so a late login/refresh body can't be persisted under the new server's key.
    ++m_epoch;
    const QSet<QNetworkReply *> pending = m_active; // copy: abort() re-enters finished
    for (QNetworkReply *r : pending)
        if (r)
            r->abort();
    m_active.clear();
}

void ApiClient::clearCookies()
{
    // Replacing the jar is the simplest reliable way to drop all cookies.
    m_jar = new QNetworkCookieJar(this);
    m_nam->setCookieJar(m_jar);
}

QNetworkRequest ApiClient::makeRequest(const QUrl &url) const
{
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader,
                  QStringLiteral("ShelfRemote/%1").arg(AppConfig::version()));
    if (!m_accessToken.isEmpty())
        req.setRawHeader("Authorization", "Bearer " + m_accessToken.toUtf8());
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::ManualRedirectPolicy);
    // Bound every request so auth/sync/close (and cover fetches) can never hang
    // indefinitely on a stalled connection.
    req.setTransferTimeout(std::chrono::seconds(30));
    return req;
}

void ApiClient::send(const QByteArray &verb, QNetworkRequest req, const QByteArray &body,
                     ApiCallback cb, bool followRedirects)
{
    sendImpl(verb, std::move(req), body, std::move(cb), followRedirects, /*allowRetry*/ true);
}

ApiResponse ApiClient::buildResponse(QNetworkReply *reply)
{
    ApiResponse res;
    res.reply = reply;
    res.status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    res.body = reply->readAll();
    res.redirectLocation =
        reply->attribute(QNetworkRequest::RedirectionTargetAttribute).toUrl();
    if (res.redirectLocation.isEmpty())
        res.redirectLocation = QUrl::fromEncoded(reply->rawHeader("Location"));
    if (reply->error() != QNetworkReply::NoError)
        res.errorString = reply->errorString();
    // Success requires BOTH a 2xx status AND no transport error: a reply can reach
    // 2xx and then fail mid-body (timeout, dropped connection), leaving a truncated
    // body that must not be accepted as a complete, successful response.
    res.ok = (reply->error() == QNetworkReply::NoError) &&
             res.status >= 200 && res.status < 300;
    return res;
}

void ApiClient::sendImpl(const QByteArray &verb, QNetworkRequest req, const QByteArray &body,
                         ApiCallback cb, bool followRedirects, bool allowRetry)
{
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     followRedirects ? QNetworkRequest::NoLessSafeRedirectPolicy
                                     : QNetworkRequest::ManualRedirectPolicy);

    // Snapshot the origin epoch so we can tell, on completion, whether the client
    // was pointed at a different server while this request was in flight.
    const quint64 epoch = m_epoch;
    QNetworkReply *reply = m_nam->sendCustomRequest(req, verb, body);
    m_active.insert(reply);
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, cb, verb, req, body, followRedirects, allowRetry, epoch]() mutable {
        m_active.remove(reply);
        ApiResponse res = buildResponse(reply);
        res.stale = (epoch != m_epoch);
        reply->deleteLater();

        // Reactive refresh: on a 401, attempt one token refresh and replay the
        // request with the new bearer token. Guard against retry loops (the
        // refresh call itself, and a second 401 after refreshing) AND against a
        // stale reply: refreshing/retrying a request that belongs to a previous
        // server would leak the current server's token onto the old origin.
        const bool noRetry = req.attribute(NoAuthRetryAttribute).toBool();
        if (res.status == 401 && allowRetry && !noRetry && !res.stale && m_onUnauthorized) {
            res.reply = nullptr; // the reply is being torn down; don't expose it
            m_onUnauthorized([this, verb, req, body, cb, followRedirects, epoch, res](bool refreshed) mutable {
                // The origin may have changed while the refresh was in flight; if so
                // this request is now stale and must not be replayed with the new
                // server's token.
                if (!refreshed || epoch != m_epoch) {
                    res.stale = res.stale || (epoch != m_epoch);
                    if (cb) cb(res); // deliver the original 401
                    return;
                }
                req.setRawHeader("Authorization", "Bearer " + m_accessToken.toUtf8());
                sendImpl(verb, req, body, cb, followRedirects, /*allowRetry*/ false);
            });
            return;
        }
        if (cb)
            cb(res);
    });
}

void ApiClient::get(const QUrl &url, ApiCallback cb, bool followRedirects)
{
    send("GET", makeRequest(url), {}, std::move(cb), followRedirects);
}

void ApiClient::getRaw(const QUrl &url, ApiCallback cb)
{
    send("GET", makeRequest(url), {}, std::move(cb), true);
}

void ApiClient::post(const QUrl &url, const QJsonObject &body, ApiCallback cb, bool followRedirects)
{
    post(url, QJsonDocument(body).toJson(QJsonDocument::Compact),
         "application/json", std::move(cb), followRedirects);
}

void ApiClient::post(const QUrl &url, const QByteArray &rawBody, const QByteArray &contentType,
                     ApiCallback cb, bool followRedirects)
{
    QNetworkRequest req = makeRequest(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, contentType);
    send("POST", req, rawBody, std::move(cb), followRedirects);
}

void ApiClient::patch(const QUrl &url, const QJsonObject &body, ApiCallback cb)
{
    QNetworkRequest req = makeRequest(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    send("PATCH", req, QJsonDocument(body).toJson(QJsonDocument::Compact),
         std::move(cb), false);
}

void ApiClient::del(const QUrl &url, ApiCallback cb)
{
    send("DELETE", makeRequest(url), {}, std::move(cb), false);
}
