#pragma once

#include <QAbstractListModel>
#include <QJsonArray>

// Models the personalized Home shelves (/api/libraries/:id/personalized). Each
// row is a shelf (label + type) whose items are exposed as a QVariantList of
// flat maps for a horizontal Repeater/ListView in QML.
class ShelfModel : public QAbstractListModel
{
    Q_OBJECT
public:
    enum Roles { LabelRole = Qt::UserRole + 1, ShelfTypeRole, ItemsRole };
    explicit ShelfModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    // Replace contents from a personalized-endpoint JSON array of shelves.
    void setShelves(const QJsonArray &shelves);

private:
    // Personalized shelves hold heterogeneous entities: book/podcast library
    // items, series, authors, and recent episodes. The shelf `type` decides how
    // each entity is unpacked and how a card activation is routed in QML.
    static QVariantList flattenEntities(const QJsonArray &entities, const QString &shelfType);
    QJsonArray m_shelves;
};
