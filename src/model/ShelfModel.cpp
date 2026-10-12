#include "model/ShelfModel.h"

#include <QJsonObject>

ShelfModel::ShelfModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

namespace {
const QString kNextUpType = QStringLiteral("playlistNextUp");
} // namespace

int ShelfModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return m_shelves.size() + (m_nextUp.isEmpty() ? 0 : 1);
}

int ShelfModel::nextUpRow() const
{
    if (m_nextUp.isEmpty())
        return -1;
    for (int i = 0; i < m_shelves.size(); ++i) {
        if (m_shelves.at(i).toObject().value(QStringLiteral("id")).toString() ==
            QLatin1String("continue-listening"))
            return i + 1;
    }
    return 0;
}

QVariant ShelfModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= rowCount())
        return {};
    const int nextUp = nextUpRow();
    if (index.row() == nextUp) {
        switch (role) {
        case LabelRole:     return QStringLiteral("Playlist Next Up");
        case ShelfTypeRole: return kNextUpType;
        case ItemsRole:     return m_nextUp;
        default:            return {};
        }
    }
    const int shelfRow = nextUp >= 0 && index.row() > nextUp ? index.row() - 1 : index.row();
    const QJsonObject shelf = m_shelves.at(shelfRow).toObject();
    switch (role) {
    case LabelRole:     return shelf.value(QStringLiteral("label")).toString();
    case ShelfTypeRole: return shelf.value(QStringLiteral("type")).toString();
    case ItemsRole:     return flattenEntities(shelf.value(QStringLiteral("entities")).toArray(),
                                               shelf.value(QStringLiteral("type")).toString());
    default:            return {};
    }
}

QHash<int, QByteArray> ShelfModel::roleNames() const
{
    return {{LabelRole, "label"}, {ShelfTypeRole, "shelfType"}, {ItemsRole, "items"}};
}

QVariantList ShelfModel::flattenEntities(const QJsonArray &entities, const QString &shelfType)
{
    QVariantList out;
    out.reserve(entities.size());
    for (const auto &v : entities) {
        const QJsonObject e = v.toObject();
        QVariantMap m;
        // Common defaults so every card has a full, uniform shape.
        m[QStringLiteral("kind")]      = QStringLiteral("book");
        m[QStringLiteral("itemId")]    = e.value(QStringLiteral("id")).toString();
        m[QStringLiteral("episodeId")] = QString();
        m[QStringLiteral("coverId")]   = e.value(QStringLiteral("id")).toString();
        m[QStringLiteral("coverKind")] = QStringLiteral("item");
        m[QStringLiteral("author")]    = QString();

        if (shelfType == QLatin1String("series")) {
            // { id, name, books: [libraryItem, ...] } — cover comes from a book.
            const QJsonArray books = e.value(QStringLiteral("books")).toArray();
            m[QStringLiteral("kind")]  = QStringLiteral("series");
            m[QStringLiteral("title")] = e.value(QStringLiteral("name")).toString();
            m[QStringLiteral("coverId")] = books.isEmpty()
                ? QString()
                : books.first().toObject().value(QStringLiteral("id")).toString();
            if (!books.isEmpty())
                m[QStringLiteral("author")] =
                    QStringLiteral("%1 books").arg(books.size());
        } else if (shelfType == QLatin1String("authors")) {
            // { id, name, imagePath } — cover is the author image endpoint.
            m[QStringLiteral("kind")]      = QStringLiteral("author");
            m[QStringLiteral("title")]     = e.value(QStringLiteral("name")).toString();
            m[QStringLiteral("coverKind")] = QStringLiteral("author");
        } else {
            const QJsonObject media = e.value(QStringLiteral("media")).toObject();
            const QJsonObject meta = media.value(QStringLiteral("metadata")).toObject();
            // The "episode" shelf carries podcast library items with a
            // recentEpisode; the card must show/play the episode, not the podcast.
            const QJsonObject recentEpisode =
                e.value(QStringLiteral("recentEpisode")).toObject();
            const bool isEpisode =
                shelfType == QLatin1String("episode") || !recentEpisode.isEmpty();
            if (isEpisode) {
                m[QStringLiteral("kind")]      = QStringLiteral("episode");
                m[QStringLiteral("episodeId")] = recentEpisode.value(QStringLiteral("id")).toString();
                m[QStringLiteral("title")]     = recentEpisode.value(QStringLiteral("title")).toString(
                    meta.value(QStringLiteral("title")).toString());
                m[QStringLiteral("author")]    = meta.value(QStringLiteral("title")).toString();
            } else {
                m[QStringLiteral("kind")]   = e.value(QStringLiteral("mediaType"))
                                                  .toString(QStringLiteral("book"));
                m[QStringLiteral("title")]  = meta.value(QStringLiteral("title")).toString(
                    e.value(QStringLiteral("name")).toString());
                m[QStringLiteral("author")] = meta.value(QStringLiteral("authorName")).toString(
                    meta.value(QStringLiteral("author")).toString());
            }
        }
        out.push_back(m);
    }
    return out;
}

void ShelfModel::setShelves(const QJsonArray &shelves)
{
    beginResetModel();
    m_shelves = shelves;
    endResetModel();
}

void ShelfModel::setPlaylistNextUp(const QVariantList &items)
{
    if (items == m_nextUp)
        return;
    const int oldRow = nextUpRow();
    if (items.isEmpty()) {
        beginRemoveRows({}, oldRow, oldRow);
        m_nextUp.clear();
        endRemoveRows();
        return;
    }
    if (oldRow < 0) {
        // Work out where it lands before it holds anything: nextUpRow() reports -1
        // while the list is empty.
        m_nextUp = items;
        const int row = nextUpRow();
        m_nextUp.clear();
        beginInsertRows({}, row, row);
        m_nextUp = items;
        endInsertRows();
        return;
    }
    m_nextUp = items;
    const QModelIndex idx = index(oldRow, 0);
    emit dataChanged(idx, idx, {ItemsRole});
}
