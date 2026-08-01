#include "model/PlaylistModel.h"

#include <QJsonObject>

PlaylistModel::PlaylistModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int PlaylistModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_playlists.size();
}

QVariant PlaylistModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_playlists.size())
        return {};

    const QJsonObject playlist = m_playlists.at(index.row()).toObject();
    const QJsonArray items = playlist.value(QStringLiteral("items")).toArray();
    switch (role) {
    case PlaylistIdRole:
        return playlist.value(QStringLiteral("id")).toString();
    case NameRole:
        return playlist.value(QStringLiteral("name")).toString();
    case DescriptionRole:
        return playlist.value(QStringLiteral("description")).toString();
    case ItemCountRole:
        return items.size();
    case EntriesRole:
        return flattenItems(items);
    default:
        return {};
    }
}

QHash<int, QByteArray> PlaylistModel::roleNames() const
{
    return {
        {PlaylistIdRole, "playlistId"},
        {NameRole, "name"},
        {DescriptionRole, "description"},
        {ItemCountRole, "itemCount"},
        {EntriesRole, "entries"},
    };
}

QVariantList PlaylistModel::flattenItems(const QJsonArray &items)
{
    QVariantList out;
    out.reserve(items.size());

    for (const auto &value : items) {
        const QJsonObject item = value.toObject();
        const QJsonObject libraryItem =
            item.value(QStringLiteral("libraryItem")).toObject();
        const QString itemId =
            item.value(QStringLiteral("libraryItemId")).toString(
                libraryItem.value(QStringLiteral("id")).toString());
        if (itemId.isEmpty())
            continue;

        const QJsonObject media =
            libraryItem.value(QStringLiteral("media")).toObject();
        const QJsonObject metadata =
            media.value(QStringLiteral("metadata")).toObject();
        const QString episodeId =
            item.value(QStringLiteral("episodeId")).toString();
        const QJsonObject episode =
            item.value(QStringLiteral("episode")).toObject();
        const bool isEpisode = !episodeId.isEmpty();

        QVariantMap entry;
        entry[QStringLiteral("kind")] =
            isEpisode ? QStringLiteral("episode") : QStringLiteral("book");
        entry[QStringLiteral("itemId")] = itemId;
        entry[QStringLiteral("episodeId")] = episodeId;
        entry[QStringLiteral("coverId")] = itemId;
        entry[QStringLiteral("coverKind")] = QStringLiteral("item");

        if (isEpisode) {
            entry[QStringLiteral("title")] =
                episode.value(QStringLiteral("title")).toString(
                    metadata.value(QStringLiteral("title")).toString());
            // The enclosing podcast title makes the most useful subtitle for an
            // episode row.
            entry[QStringLiteral("author")] =
                metadata.value(QStringLiteral("title")).toString();
        } else {
            entry[QStringLiteral("title")] =
                metadata.value(QStringLiteral("title")).toString(
                    libraryItem.value(QStringLiteral("title")).toString());
            entry[QStringLiteral("author")] =
                metadata.value(QStringLiteral("authorName")).toString(
                    metadata.value(QStringLiteral("author")).toString());
        }

        out.push_back(entry);
    }
    return out;
}

void PlaylistModel::setPlaylists(const QJsonArray &playlists)
{
    beginResetModel();
    m_playlists = playlists;
    endResetModel();
    emit countChanged();
}
