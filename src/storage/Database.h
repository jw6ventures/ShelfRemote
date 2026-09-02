#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <QVector>

// Thin process-wide SQLite wrapper. Holds server profiles, encrypted secrets,
// simple key/value settings, and cached item metadata. A singleton because the
// whole app shares one database file and one connection.
class Database
{
public:
    static Database &instance();

    bool open();       // opens/creates the DB and applies migrations
    void close();

    // --- Secrets (opaque encrypted blobs, keyed by string) ---
    void putSecret(const QString &key, const QByteArray &blob);
    QByteArray getSecret(const QString &key) const;
    void removeSecret(const QString &key);

    // --- Settings (string key/value) ---
    void putSetting(const QString &key, const QString &value);
    QString getSetting(const QString &key, const QString &def = {}) const;
    void removeSetting(const QString &key);

    // --- Server profiles ---
    struct ServerRow {
        QString id;        // stable key, hash of the base URL
        QString name;
        QString baseUrl;
        QString lastUserId;
        qint64  lastUsed = 0;
    };
    void upsertServer(const ServerRow &row);
    QVector<ServerRow> servers() const;
    void removeServer(const QString &id);

    // --- Cached item metadata (JSON blob per item, for offline-ish browsing) ---
    void cacheItem(const QString &itemId, const QJsonObject &json);
    QJsonObject cachedItem(const QString &itemId) const;
    // Removes every cached item whose key is namespaced under `serverId` (keys are
    // "<serverId>/<itemId>"). Used when a server is forgotten.
    void removeCachedItemsForServer(const QString &serverId);
    // Keeps only the `maxRows` most recently written cache entries. Nothing else
    // ever deletes from this table, so without a periodic prune it grows by one
    // row for every item detail ever opened, for the life of the installation.
    void pruneItemCache(int maxRows);

private:
    Database() = default;
    void migrate();
    QString m_connName = QStringLiteral("shelfremote");
    bool m_open = false;
};
