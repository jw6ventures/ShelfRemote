#include "model/ShelfModel.h"

#include <QJsonObject>

ShelfModel::ShelfModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int ShelfModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_shelves.size();
}

QVariant ShelfModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_shelves.size())
        return {};
    const QJsonObject shelf = m_shelves.at(index.row()).toObject();
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
