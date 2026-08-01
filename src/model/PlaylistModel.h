#pragma once

#include <QAbstractListModel>
#include <QJsonArray>

// Models the expanded playlist payload returned by
// /api/libraries/:id/playlists. Each row is a playlist and exposes its ordered
// books/podcast episodes as a flat QVariantList suitable for a QML ListView.
class PlaylistModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
    enum Roles {
        PlaylistIdRole = Qt::UserRole + 1,
        NameRole,
        DescriptionRole,
        ItemCountRole,
        EntriesRole
    };

    explicit PlaylistModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setPlaylists(const QJsonArray &playlists);

signals:
    void countChanged();

private:
    static QVariantList flattenItems(const QJsonArray &items);

    QJsonArray m_playlists;
};
