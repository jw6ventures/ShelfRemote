#include "storage/CoverCache.h"
#include "app/AppConfig.h"
#include "net/ApiClient.h"
#include "server/ServerProfile.h"

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QUrl>
#include <QUrlQuery>

CoverCache::CoverCache(ApiClient *api, QObject *parent)
    : QObject(parent)
    , m_api(api)
{
}

QString CoverCache::diskPath(const QString &id, int width, int height, bool author) const
{
    // Compose a key from server identity + kind + id + size, then hash it: the
    // filename is a fixed hex string with no path separators, so a hostile id can
    // neither escape the cache directory nor collide across servers.
    const QString server = ServerProfile::idForUrl(m_api->baseUrl());
    const QString key = server + QLatin1Char('|') + (author ? QLatin1Char('a') : QLatin1Char('i'))
                        + QLatin1Char('|') + id
                        + QStringLiteral("|%1x%2").arg(width).arg(height);
    const QString name = QString::fromLatin1(
        QCryptographicHash::hash(key.toUtf8(), QCryptographicHash::Sha256).toHex());
    return AppConfig::coverCacheDir() + QLatin1Char('/') + name + QStringLiteral(".webp");
}

QString CoverCache::localUrl(const QString &itemId, int width, int height)
{
    const QString path = diskPath(itemId, width, height, /*author*/ false);
    if (QFile::exists(path))
        return QUrl::fromLocalFile(path).toString();
    fetch(itemId, width, height, /*author*/ false);
    return {};
}

QString CoverCache::authorImage(const QString &authorId, int width, int height)
{
    const QString path = diskPath(authorId, width, height, /*author*/ true);
    if (QFile::exists(path))
        return QUrl::fromLocalFile(path).toString();
    fetch(authorId, width, height, /*author*/ true);
    return {};
}

void CoverCache::fetch(const QString &id, int width, int height, bool author)
{
    const QString path = diskPath(id, width, height, author);
    if (m_inFlight.contains(path))
        return;
    m_inFlight.insert(path);

    QUrlQuery q;
    q.addQueryItem(QStringLiteral("width"), QString::number(width));
    q.addQueryItem(QStringLiteral("height"), QString::number(height));
    q.addQueryItem(QStringLiteral("format"), QStringLiteral("webp"));

    const QUrl url = author ? m_api->endpoints().authorImage(id, q)
                            : m_api->endpoints().cover(id, q);
    m_api->getRaw(url, [this, itemId = id, path](const ApiResponse &res) {
        m_inFlight.remove(path);
        // `path` was hashed against the server that was current when the fetch
        // started. If the client has since been pointed at a different server
        // (res.stale), emitting coverReady would let a same-id card on the NEW
        // server adopt this (old server's) image file — drop it instead.
        if (res.stale)
            return;
        if (!res.ok || res.body.isEmpty())
            return;
        QFile f(path);
        if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            f.write(res.body);
            f.close();
            emit coverReady(itemId, QUrl::fromLocalFile(path).toString());
        }
    });
}

qint64 CoverCache::cacheSizeBytes() const
{
    qint64 total = 0;
    QDirIterator it(AppConfig::coverCacheDir(), QDir::Files);
    while (it.hasNext()) {
        it.next();
        total += it.fileInfo().size();
    }
    return total;
}

void CoverCache::clearCache()
{
    QDir dir(AppConfig::coverCacheDir());
    const auto entries = dir.entryList(QDir::Files);
    for (const QString &e : entries)
        dir.remove(e);
}
