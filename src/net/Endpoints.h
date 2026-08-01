#pragma once

#include <QString>
#include <QUrl>
#include <QUrlQuery>

// Builds Audiobookshelf URLs relative to a configured server base URL. The base
// may include a sub-path (e.g. https://host/audiobookshelf), so paths MUST be
// resolved against the base rather than the domain root. All builders go through
// Endpoints::build to guarantee that invariant.
class Endpoints
{
public:
    explicit Endpoints(const QUrl &base = {});

    void setBase(const QUrl &base);
    QUrl base() const { return m_base; }

    // Joins the base path with `path` (a leading-slash absolute API path) and
    // attaches an optional query, without ever discarding the base sub-path.
    QUrl build(const QString &path, const QUrlQuery &query = {}) const;

    // --- Discovery / auth ---
    QUrl ping() const              { return build(QStringLiteral("/ping")); }
    QUrl status() const            { return build(QStringLiteral("/status")); }
    QUrl login() const             { return build(QStringLiteral("/login")); }
    QUrl logout() const            { return build(QStringLiteral("/logout")); }
    QUrl authRefresh() const       { return build(QStringLiteral("/auth/refresh")); }
    QUrl authorize() const         { return build(QStringLiteral("/api/authorize")); }
    QUrl openidStart(const QUrlQuery &q) const { return build(QStringLiteral("/auth/openid"), q); }
    QUrl openidCallback(const QUrlQuery &q) const { return build(QStringLiteral("/auth/openid/callback"), q); }

    // --- Libraries / browse ---
    QUrl libraries() const         { return build(QStringLiteral("/api/libraries")); }
    QUrl library(const QString &id, const QUrlQuery &q = {}) const
        { return build(QStringLiteral("/api/libraries/") + id, q); }
    QUrl personalized(const QString &id) const
        { return build(QStringLiteral("/api/libraries/") + id + QStringLiteral("/personalized")); }
    QUrl libraryPlaylists(const QString &id, const QUrlQuery &q = {}) const
        { return build(QStringLiteral("/api/libraries/") + id + QStringLiteral("/playlists"), q); }
    QUrl libraryItems(const QString &id, const QUrlQuery &q) const
        { return build(QStringLiteral("/api/libraries/") + id + QStringLiteral("/items"), q); }
    QUrl search(const QString &id, const QUrlQuery &q) const
        { return build(QStringLiteral("/api/libraries/") + id + QStringLiteral("/search"), q); }

    // --- Item / cover ---
    QUrl item(const QString &id, const QUrlQuery &q = {}) const
        { return build(QStringLiteral("/api/items/") + id, q); }
    QUrl cover(const QString &id, const QUrlQuery &q = {}) const
        { return build(QStringLiteral("/api/items/") + id + QStringLiteral("/cover"), q); }
    QUrl authorImage(const QString &id, const QUrlQuery &q = {}) const
        { return build(QStringLiteral("/api/authors/") + id + QStringLiteral("/image"), q); }

    // --- Playback session ---
    QUrl play(const QString &itemId) const
        { return build(QStringLiteral("/api/items/") + itemId + QStringLiteral("/play")); }
    QUrl playEpisode(const QString &itemId, const QString &episodeId) const
        { return build(QStringLiteral("/api/items/") + itemId + QStringLiteral("/play/") + episodeId); }
    QUrl session(const QString &sessionId) const
        { return build(QStringLiteral("/api/session/") + sessionId); }
    QUrl sessionSync(const QString &sessionId) const
        { return build(QStringLiteral("/api/session/") + sessionId + QStringLiteral("/sync")); }
    QUrl sessionClose(const QString &sessionId) const
        { return build(QStringLiteral("/api/session/") + sessionId + QStringLiteral("/close")); }

    // --- Progress / bookmarks ---
    QUrl progress(const QString &itemId) const
        { return build(QStringLiteral("/api/me/progress/") + itemId); }
    QUrl episodeProgress(const QString &itemId, const QString &episodeId) const
        { return build(QStringLiteral("/api/me/progress/") + itemId + QStringLiteral("/") + episodeId); }
    QUrl bookmark(const QString &itemId) const
        { return build(QStringLiteral("/api/me/item/") + itemId + QStringLiteral("/bookmark")); }
    QUrl bookmarkAt(const QString &itemId, double time) const
        { return build(QStringLiteral("/api/me/item/") + itemId + QStringLiteral("/bookmark/")
                       + QString::number(qint64(time))); }

    // Turns a server-relative contentUrl (e.g. "/api/items/.../file/..") into an
    // absolute URL against the base. Absolute inputs are returned unchanged.
    QUrl resolveContentUrl(const QString &contentUrl) const;

private:
    QUrl m_base;
};
