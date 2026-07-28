#pragma once

#include <QAbstractListModel>
#include <QUrl>
#include <QVector>

#include "server/ServerProfile.h"

// QML-facing list model of saved servers plus a small version-capability layer.
// The capability layer answers feature questions keyed on the server version so
// version checks do not get scattered through QML.
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

    // Capability layer: e.g. hasCapability("openidLogoutUrl") given server version.
    void setServerVersion(const QString &version) { m_version = version; }
    Q_INVOKABLE bool hasCapability(const QString &feature) const;

signals:
    void countChanged();

private:
    QVector<ServerProfile> m_servers;
    QString m_version;
};
