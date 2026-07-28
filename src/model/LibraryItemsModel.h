#pragma once

#include <QAbstractListModel>
#include <QJsonArray>
#include <QUrlQuery>

class ApiClient;

// Paginated grid model for /api/libraries/:id/items (also reused for arbitrary
// item arrays such as a shelf's contents or search results). Exposes the fields
// the MediaGrid needs; covers are resolved separately through CoverCache.
class LibraryItemsModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(bool hasMore READ hasMore NOTIFY hasMoreChanged)
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
    enum Roles {
        ItemIdRole = Qt::UserRole + 1,
        TitleRole, AuthorRole, SubtitleRole,
        MediaTypeRole, ProgressRole, DurationRole, FinishedRole
    };
    explicit LibraryItemsModel(QObject *parent = nullptr);
    void setApi(ApiClient *api) { m_api = api; }

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    bool loading() const { return m_loading; }
    bool hasMore() const { return m_hasMore; }

    // Fresh load of a library page 0 with optional sort/filter query params.
    Q_INVOKABLE void loadLibrary(const QString &libraryId, const QString &sort = {},
                                 bool desc = false, const QString &filter = {});
    Q_INVOKABLE void loadMore();

    // Populate directly from an already-fetched JSON array (shelves / search).
    void setItems(const QJsonArray &items);

signals:
    void loadingChanged();
    void hasMoreChanged();
    void countChanged();

private:
    void fetchPage(int page);
    static QJsonObject normalise(const QJsonObject &raw);

    ApiClient *m_api = nullptr;
    QJsonArray m_items;
    QString m_libraryId;
    QUrlQuery m_baseQuery;
    int m_page = 0;
    int m_limit = 50;
    bool m_loading = false;
    bool m_hasMore = false;
    // Bumped on every fresh load; a page response whose generation no longer
    // matches is stale (a newer library/sort/filter load superseded it) and is
    // dropped so it can never append into or reset the current results.
    quint64 m_generation = 0;
};
