#pragma once

#include <QAbstractListModel>
#include <QUrl>
#include <QVector>

#include "server/ServerProfile.h"

// QML-facing list model of saved servers.
class ServerManager : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
    enum Roles { IdRole = Qt::UserRole + 1, NameRole, BaseUrlRole, LastUserRole };
    explicit ServerManager(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE void reload();
    Q_INVOKABLE void saveServer(const QUrl &baseUrl, const QString &name, const QString &userId);
    Q_INVOKABLE void removeServer(const QString &id);
    Q_INVOKABLE QUrl baseUrlFor(const QString &id) const;

signals:
    void countChanged();

private:
    QVector<ServerProfile> m_servers;
};
