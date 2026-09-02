#pragma once

#include <QHash>
#include <QObject>
#include <QSet>
#include <QString>

class ApiClient;

// Fetches item cover images through the authenticated ApiClient (so the bearer
// token is sent as a header, never in the URL) and persists them to the on-disk
// cache. QML binds an Image.source to localUrl(); when a cover is not yet on
// disk it returns an empty string and starts a fetch, emitting coverReady() with
// the file:// URL once written.
class CoverCache : public QObject
{
    Q_OBJECT
public:
    explicit CoverCache(ApiClient *api, QObject *parent = nullptr);

    // Returns a file:// URL if cached, otherwise "" and kicks off a fetch.
    Q_INVOKABLE QString localUrl(const QString &itemId, int width = 400, int height = 640);
    // Same contract for an author's portrait (different ABS endpoint). Keyed by
    // authorId, so coverReady() fires with the authorId.
    Q_INVOKABLE QString authorImage(const QString &authorId, int width = 400, int height = 640);

    // Total size of the cover cache directory in bytes.
    Q_INVOKABLE qint64 cacheSizeBytes() const;
    Q_INVOKABLE void clearCache();

    // Default ceiling for the on-disk cover cache.
    static constexpr qint64 kDefaultMaxCacheBytes = 256LL * 1024 * 1024;

    // Deletes the oldest cover files until the directory fits in `maxBytes`.
    // Nothing else bounds this directory: every distinct item, author, and size
    // ever displayed leaves a file behind, so a large library grows it without
    // limit. Eviction is by write time — a cover is written once and never
    // rewritten, so that is first-fetched-first-evicted; an evicted cover is
    // simply re-fetched the next time its card is shown.
    void pruneToLimit(qint64 maxBytes = kDefaultMaxCacheBytes);

signals:
    void coverReady(const QString &itemId, const QString &fileUrl);

private:
    // Server-scoped, hashed disk path. `id` is server-controlled, so it is never
    // placed in the path directly (a "/" or ".." would escape the cache dir); it is
    // folded into a hash together with the server identity and the cover/author
    // kind so ids from different servers never collide.
    QString diskPath(const QString &id, int width, int height, bool author) const;
    void fetch(const QString &id, int width, int height, bool author);

    ApiClient        *m_api;
    QSet<QString>     m_inFlight; // keyed by disk path to dedupe fetches
    // Disk paths the server has no image for. Grid delegates are recycled, so a
    // card without cover art asks again every time it scrolls back into view;
    // without this, that is one doomed request per appearance, forever. Held in
    // memory only, so a restart (or Clear cache) retries everything.
    QSet<QString>     m_missing;
};
