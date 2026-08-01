#include "app/Backend.h"
#include "model/LibraryItemsModel.h"
#include "model/PlaylistModel.h"
#include "model/ProgressStore.h"
#include "model/ShelfModel.h"
#include "net/ApiClient.h"
#include "server/ServerProfile.h"
#include "storage/Database.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QUrlQuery>

namespace {
// Namespaces cached item metadata by server so two servers that happen to share
// an item id do not overwrite each other's cached JSON.
QString itemCacheKey(const QUrl &base, const QString &itemId)
{
    return ServerProfile::idForUrl(base) + QLatin1Char('/') + itemId;
}

// The last-selected library is remembered per server (library ids are unique to
// a server, so a global key would mismatch after switching servers).
QString lastLibraryKey(const QUrl &base)
{
    return ServerProfile::idForUrl(base) + QStringLiteral("/lastLibraryId");
}
} // namespace

namespace {
// Recursively converts a QJsonValue into QVariant so QML sees plain maps/lists.
QVariant jsonToVariant(const QJsonValue &v)
{
    switch (v.type()) {
    case QJsonValue::Object: {
        QVariantMap m;
        const QJsonObject o = v.toObject();
        for (auto it = o.begin(); it != o.end(); ++it)
            m.insert(it.key(), jsonToVariant(it.value()));
        return m;
    }
    case QJsonValue::Array: {
        QVariantList l;
        for (const auto &e : v.toArray())
            l.push_back(jsonToVariant(e));
        return l;
    }
    case QJsonValue::Bool:   return v.toBool();
    case QJsonValue::Double: return v.toDouble();
    case QJsonValue::String: return v.toString();
    default:                 return {};
    }
}
} // namespace

Backend::Backend(ApiClient *api, QObject *parent)
    : QObject(parent)
    , m_api(api)
    , m_homeShelves(new ShelfModel(this))
    , m_playlists(new PlaylistModel(this))
    , m_libraryItems(new LibraryItemsModel(this))
    , m_searchResults(new LibraryItemsModel(this))
{
    m_libraryItems->setApi(api);
    m_searchResults->setApi(api);
}

void Backend::loadLibraries()
{
    const quint64 gen = m_serverGeneration;
    m_api->get(m_api->endpoints().libraries(), [this, gen](const ApiResponse &res) {
        if (res.stale || gen != m_serverGeneration)
            return; // response from a server we have since logged out of / switched
        if (!res.ok) {
            emit errorOccurred(tr("Failed to load libraries (HTTP %1)").arg(res.status));
            return;
        }
        m_libraries.clear();
        const QJsonArray libs = res.json().value(QStringLiteral("libraries")).toArray();
        for (const auto &v : libs) {
            const QJsonObject o = v.toObject();
            QVariantMap m;
            m[QStringLiteral("id")] = o.value(QStringLiteral("id")).toString();
            m[QStringLiteral("name")] = o.value(QStringLiteral("name")).toString();
            m[QStringLiteral("mediaType")] = o.value(QStringLiteral("mediaType")).toString();
            m_libraries.push_back(m);
        }
        emit librariesChanged();
        if (!m_libraries.isEmpty() && m_currentLibraryId.isEmpty()) {
            // Restore the library the user last browsed on this server; fall back to
            // the first one if it is gone (or this server was never used before).
            const QString last =
                Database::instance().getSetting(lastLibraryKey(m_api->baseUrl()));
            QString toSelect = m_libraries.first().toMap().value(QStringLiteral("id")).toString();
            if (!last.isEmpty()) {
                for (const auto &libV : std::as_const(m_libraries)) {
                    if (libV.toMap().value(QStringLiteral("id")).toString() == last) {
                        toSelect = last;
                        break;
                    }
                }
            }
            selectLibrary(toSelect);
        }
    });
}

void Backend::selectLibrary(const QString &libraryId)
{
    if (libraryId.isEmpty())
        return;
    m_currentLibraryId = libraryId;
    Database::instance().putSetting(lastLibraryKey(m_api->baseUrl()), libraryId);
    emit currentLibraryChanged();
    refreshHome();
    refreshPlaylists();
    browse(QStringLiteral("media.metadata.title"), false, {});
}

void Backend::refreshHome()
{
    if (m_currentLibraryId.isEmpty())
        return;
    const quint64 gen = m_serverGeneration;
    m_api->get(m_api->endpoints().personalized(m_currentLibraryId), [this, gen](const ApiResponse &res) {
        if (res.stale || gen != m_serverGeneration)
            return;
        if (res.ok)
            m_homeShelves->setShelves(res.jsonDoc().array());
    });
}

void Backend::refreshPlaylists()
{
    if (m_currentLibraryId.isEmpty())
        return;

    const QString libraryId = m_currentLibraryId;
    const quint64 gen = m_serverGeneration;
    m_api->get(m_api->endpoints().libraryPlaylists(libraryId),
               [this, libraryId, gen](const ApiResponse &res) {
        // The selected library may change without changing server generation.
        // Never show a late playlist response under the new library.
        if (res.stale || gen != m_serverGeneration ||
            libraryId != m_currentLibraryId) {
            return;
        }
        if (!res.ok) {
            emit errorOccurred(tr("Failed to load playlists (HTTP %1)")
                                   .arg(res.status));
            return;
        }
        m_playlists->setPlaylists(
            res.json().value(QStringLiteral("results")).toArray());
    });
}

void Backend::browse(const QString &sort, bool desc, const QString &filter)
{
    if (m_currentLibraryId.isEmpty())
        return;
    m_libraryItems->loadLibrary(m_currentLibraryId, sort, desc, filter);
}

void Backend::clearSearch()
{
    // Cancel any in-flight query and drop stale hits so emptying/shortening the
    // search box doesn't leave the previous results frozen on screen.
    ++m_searchGeneration;
    m_searchResults->setItems(QJsonArray());
}

void Backend::search(const QString &query)
{
    if (m_currentLibraryId.isEmpty())
        return;
    if (query.trimmed().isEmpty()) {
        clearSearch();
        return;
    }
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("q"), query.trimmed());
    q.addQueryItem(QStringLiteral("limit"), QStringLiteral("50"));
    const quint64 gen = ++m_searchGeneration;
    const quint64 serverGen = m_serverGeneration;
    m_api->get(m_api->endpoints().search(m_currentLibraryId, q), [this, gen, serverGen](const ApiResponse &res) {
        // Ignore a response the user has already typed past, or one from a server
        // we have since switched away from.
        if (res.stale || gen != m_searchGeneration || serverGen != m_serverGeneration || !res.ok)
            return;
        // The search endpoint returns grouped results (book, series, authors,
        // podcast, episodes). Flatten the book/podcast libraryItem hits.
        const QJsonObject obj = res.json();
        QJsonArray flat;
        for (const QString &group : {QStringLiteral("book"), QStringLiteral("podcast")}) {
            for (const auto &v : obj.value(group).toArray()) {
                const QJsonObject hit = v.toObject();
                flat.append(hit.value(QStringLiteral("libraryItem")).toObject());
            }
        }
        m_searchResults->setItems(flat);
    });
}

void Backend::browseSeries(const QString &seriesId)
{
    if (m_currentLibraryId.isEmpty() || seriesId.isEmpty())
        return;
    // ABS item filters are "<group>.<base64(value)>".
    const QString filter = QStringLiteral("series.") +
        QString::fromLatin1(seriesId.toUtf8().toBase64());
    m_libraryItems->loadLibrary(m_currentLibraryId,
                                QStringLiteral("media.metadata.title"), false, filter);
    emit navigateToLibrary();
}

void Backend::browseAuthor(const QString &authorId)
{
    if (m_currentLibraryId.isEmpty() || authorId.isEmpty())
        return;
    const QString filter = QStringLiteral("authors.") +
        QString::fromLatin1(authorId.toUtf8().toBase64());
    m_libraryItems->loadLibrary(m_currentLibraryId,
                                QStringLiteral("media.metadata.title"), false, filter);
    emit navigateToLibrary();
}

void Backend::loadItem(const QString &itemId)
{
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("expanded"), QStringLiteral("1"));
    q.addQueryItem(QStringLiteral("include"), QStringLiteral("progress"));
    const quint64 gen = m_serverGeneration;
    const QUrl base = m_api->baseUrl();
    m_api->get(m_api->endpoints().item(itemId, q), [this, itemId, gen, base](const ApiResponse &res) {
        if (res.stale || gen != m_serverGeneration)
            return;
        if (!res.ok) {
            emit errorOccurred(tr("Failed to load item"));
            return;
        }
        const QJsonObject obj = res.json();
        Database::instance().cacheItem(itemCacheKey(base, itemId), obj);
        emit itemLoaded(jsonToVariant(obj).toMap());
    });
}

void Backend::markFinished(const QString &itemId, bool finished)
{
    QJsonObject body{{QStringLiteral("isFinished"), finished}};
    const quint64 gen = m_serverGeneration;
    m_api->patch(m_api->endpoints().progress(itemId), body, [this, itemId, finished, gen](const ApiResponse &res) {
        if (res.stale || gen != m_serverGeneration)
            return;
        if (!res.ok) {
            emit errorOccurred(tr("Failed to update progress"));
            return;
        }
        // Reflect the change locally so browse cards update immediately, and reload
        // the item so its detail view (progress panel, button label) refreshes.
        if (m_progress)
            m_progress->update(itemId, m_progress->currentTime(itemId), 0.0, finished);
        loadItem(itemId);
    });
}

void Backend::reset()
{
    ++m_serverGeneration;
    ++m_searchGeneration; // invalidate any in-flight search too
    m_currentLibraryId.clear();
    m_libraries.clear();
    m_homeShelves->setShelves(QJsonArray());
    m_playlists->setPlaylists(QJsonArray());
    m_libraryItems->setItems(QJsonArray());
    m_searchResults->setItems(QJsonArray());
    emit librariesChanged();
    emit currentLibraryChanged();
}
