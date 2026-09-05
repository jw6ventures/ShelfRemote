#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QVariantList>
#include <QVariantMap>

// Full definitions required (not forward decls): these types appear as pointer
// Q_PROPERTY types, so moc must see them complete to register their metatypes.
#include "model/LibraryItemsModel.h"
#include "model/PlaylistModel.h"
#include "model/ShelfModel.h"

class ApiClient;
class ProgressStore;

// Content-facing controller for the browse/home/search/detail screens. Holds the
// list of accessible libraries, the active library, the Home shelf model, the
// library grid model, and the search-results model, and issues the REST calls
// that populate them.
class Backend : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList libraries READ libraries NOTIFY librariesChanged)
    Q_PROPERTY(QString currentLibraryId READ currentLibraryId NOTIFY currentLibraryChanged)
    Q_PROPERTY(ShelfModel *homeShelves READ homeShelves CONSTANT)
    Q_PROPERTY(PlaylistModel *playlists READ playlists CONSTANT)
    Q_PROPERTY(LibraryItemsModel *libraryItems READ libraryItems CONSTANT)
    Q_PROPERTY(LibraryItemsModel *searchResults READ searchResults CONSTANT)

public:
    explicit Backend(ApiClient *api, QObject *parent = nullptr);

    // Optional: lets markFinished() reflect a change locally so browse cards update
    // without a round-trip.
    void setProgressStore(ProgressStore *progress) { m_progress = progress; }

    QVariantList libraries() const { return m_libraries; }
    QString currentLibraryId() const { return m_currentLibraryId; }
    ShelfModel *homeShelves() const { return m_homeShelves; }
    PlaylistModel *playlists() const { return m_playlists; }
    LibraryItemsModel *libraryItems() const { return m_libraryItems; }
    LibraryItemsModel *searchResults() const { return m_searchResults; }

    Q_INVOKABLE void loadLibraries();
    Q_INVOKABLE void selectLibrary(const QString &libraryId);
    Q_INVOKABLE void refreshHome();
    Q_INVOKABLE void refreshPlaylists();
    Q_INVOKABLE void browse(const QString &sort, bool desc, const QString &filter);
    Q_INVOKABLE void search(const QString &query);
    // Drops any current/in-flight search results (box emptied or query too short).
    Q_INVOKABLE void clearSearch();
    // Show the library grid filtered to a series / author (Home shelf cards for
    // these entities have no item detail page of their own).
    Q_INVOKABLE void browseSeries(const QString &seriesId);
    Q_INVOKABLE void browseAuthor(const QString &authorId);
    Q_INVOKABLE void loadItem(const QString &itemId);
    Q_INVOKABLE void markFinished(const QString &itemId, bool finished);

    // Clears all content state and invalidates in-flight responses. Called on
    // logout / server change so one server's data never bleeds into another's.
    Q_INVOKABLE void reset();

signals:
    void librariesChanged();
    void currentLibraryChanged();
    void itemLoaded(const QVariantMap &item);
    void errorOccurred(const QString &message);
    // Emitted when a Home series/author card should switch the shell to the
    // (now filtered) library grid.
    void navigateToLibrary();

private:
    // Folds an item payload's userMediaProgress into the shared ProgressStore.
    void applyItemProgress(const QString &itemId, const QJsonObject &item);

    ApiClient         *m_api;
    ProgressStore     *m_progress = nullptr;
    ShelfModel        *m_homeShelves;
    PlaylistModel     *m_playlists;
    LibraryItemsModel *m_libraryItems;
    LibraryItemsModel *m_searchResults;
    QVariantList       m_libraries;
    QString            m_currentLibraryId;
    // Guards against an older /search response overwriting a newer query's results.
    quint64            m_searchGeneration = 0;
    // Bumped on reset() (logout / server change); every network callback drops its
    // result if this no longer matches, so a slow response from a previous server
    // can never populate the current session's models.
    quint64            m_serverGeneration = 0;
};
