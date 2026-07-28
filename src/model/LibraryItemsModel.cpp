#include "model/LibraryItemsModel.h"
#include "net/ApiClient.h"

#include <QJsonObject>

LibraryItemsModel::LibraryItemsModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int LibraryItemsModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_items.size();
}

QVariant LibraryItemsModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_items.size())
        return {};
    const QJsonObject o = m_items.at(index.row()).toObject();
    switch (role) {
    case ItemIdRole:    return o.value(QStringLiteral("id")).toString();
    case TitleRole:     return o.value(QStringLiteral("title")).toString();
    case AuthorRole:    return o.value(QStringLiteral("author")).toString();
    case SubtitleRole:  return o.value(QStringLiteral("subtitle")).toString();
    case MediaTypeRole: return o.value(QStringLiteral("mediaType")).toString();
    case ProgressRole:  return o.value(QStringLiteral("progress")).toDouble();
    case DurationRole:  return o.value(QStringLiteral("duration")).toDouble();
    case FinishedRole:  return o.value(QStringLiteral("finished")).toBool();
    default:            return {};
    }
}

QHash<int, QByteArray> LibraryItemsModel::roleNames() const
{
    return {{ItemIdRole, "itemId"},
            {TitleRole, "title"},
            {AuthorRole, "author"},
            {SubtitleRole, "subtitle"},
            {MediaTypeRole, "mediaType"},
            {ProgressRole, "progress"},
            {DurationRole, "duration"},
            {FinishedRole, "finished"}};
}

QJsonObject LibraryItemsModel::normalise(const QJsonObject &raw)
{
    // Flatten the ABS libraryItem shape into the flat fields the grid needs.
    const QJsonObject media = raw.value(QStringLiteral("media")).toObject();
    const QJsonObject meta = media.value(QStringLiteral("metadata")).toObject();
    const QJsonObject progress = raw.value(QStringLiteral("userMediaProgress")).toObject();

    QJsonObject o;
    o[QStringLiteral("id")] = raw.value(QStringLiteral("id")).toString();
    o[QStringLiteral("title")] = meta.value(QStringLiteral("title")).toString(
        raw.value(QStringLiteral("title")).toString());
    o[QStringLiteral("subtitle")] = meta.value(QStringLiteral("subtitle")).toString();
    o[QStringLiteral("author")] = meta.value(QStringLiteral("authorName")).toString(
        meta.value(QStringLiteral("author")).toString());
    o[QStringLiteral("mediaType")] = raw.value(QStringLiteral("mediaType")).toString();
    o[QStringLiteral("duration")] = media.value(QStringLiteral("duration")).toDouble();
    o[QStringLiteral("progress")] = progress.value(QStringLiteral("progress")).toDouble();
    o[QStringLiteral("finished")] = progress.value(QStringLiteral("isFinished")).toBool();
    return o;
}

void LibraryItemsModel::loadLibrary(const QString &libraryId, const QString &sort,
                                    bool desc, const QString &filter)
{
    m_libraryId = libraryId;
    m_baseQuery.clear();
    m_baseQuery.addQueryItem(QStringLiteral("minified"), QStringLiteral("1"));
    if (!sort.isEmpty()) {
        m_baseQuery.addQueryItem(QStringLiteral("sort"), sort);
        m_baseQuery.addQueryItem(QStringLiteral("desc"), desc ? QStringLiteral("1")
                                                              : QStringLiteral("0"));
    }
    if (!filter.isEmpty())
        m_baseQuery.addQueryItem(QStringLiteral("filter"), filter);

    // A new load invalidates any page request still in flight from a prior
    // library/sort/filter so its late response cannot append into these results.
    ++m_generation;
    m_page = 0;
    m_hasMore = false;
    beginResetModel();
    m_items = {};
    endResetModel();
    emit countChanged();
    fetchPage(0);
}

void LibraryItemsModel::loadMore()
{
    if (m_hasMore && !m_loading)
        fetchPage(m_page + 1);
}

void LibraryItemsModel::fetchPage(int page)
{
    if (!m_api || m_libraryId.isEmpty())
        return;
    m_loading = true;
    emit loadingChanged();

    QUrlQuery q = m_baseQuery;
    q.addQueryItem(QStringLiteral("limit"), QString::number(m_limit));
    q.addQueryItem(QStringLiteral("page"), QString::number(page));

    const quint64 gen = m_generation;
    m_api->get(m_api->endpoints().libraryItems(m_libraryId, q), [this, page, gen](const ApiResponse &res) {
        // Drop a response from a superseded load, or one from a server we have
        // since switched away from: touching m_items/m_loading here would corrupt
        // the current (newer) results.
        if (res.stale || gen != m_generation)
            return;
        m_loading = false;
        emit loadingChanged();
        if (!res.ok)
            return;
        const QJsonObject obj = res.json();
        const QJsonArray results = obj.value(QStringLiteral("results")).toArray();
        const int total = obj.value(QStringLiteral("total")).toInt();

        if (!results.isEmpty()) {
            const int first = m_items.size();
            beginInsertRows({}, first, first + results.size() - 1);
            for (const auto &v : results)
                m_items.append(normalise(v.toObject()));
            endInsertRows();
        }

        m_page = page;
        m_hasMore = m_items.size() < total;
        emit hasMoreChanged();
        emit countChanged();
    });
}

void LibraryItemsModel::setItems(const QJsonArray &items)
{
    // Invalidate any in-flight page load so it cannot append into this content.
    ++m_generation;
    beginResetModel();
    m_items = {};
    for (const auto &v : items) {
        const QJsonObject o = v.toObject();
        // Items may already be flat (shelf entries) or nested (library items).
        m_items.append(o.contains(QStringLiteral("media")) ? normalise(o) : o);
    }
    m_hasMore = false;
    endResetModel();
    emit hasMoreChanged();
    emit countChanged();
}
