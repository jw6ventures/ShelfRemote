#include "storage/Database.h"
#include "app/AppConfig.h"

#include <QDebug>
#include <QJsonDocument>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

namespace {
// Runs a prepared query and logs (rather than silently swallowing) any failure.
bool execOrWarn(QSqlQuery &q, const char *what)
{
    if (q.exec())
        return true;
    qWarning() << "Database" << what << "failed:" << q.lastError().text();
    return false;
}
} // namespace

Database &Database::instance()
{
    static Database db;
    return db;
}

bool Database::open()
{
    if (m_open)
        return true;
    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connName);
    db.setDatabaseName(AppConfig::databasePath());
    if (!db.open()) {
        qWarning() << "Database open failed:" << db.lastError().text();
        return false;
    }
    QSqlQuery pragma(db);
    pragma.exec(QStringLiteral("PRAGMA journal_mode=WAL"));
    pragma.exec(QStringLiteral("PRAGMA foreign_keys=ON"));
    m_open = true;
    migrate();
    return true;
}

void Database::close()
{
    if (!m_open)
        return;
    QSqlDatabase::database(m_connName).close();
    QSqlDatabase::removeDatabase(m_connName);
    m_open = false;
}

void Database::migrate()
{
    QSqlDatabase db = QSqlDatabase::database(m_connName);
    QSqlQuery q(db);
    q.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS secrets ("
        "  key TEXT PRIMARY KEY, blob BLOB NOT NULL)"));
    q.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS settings ("
        "  key TEXT PRIMARY KEY, value TEXT)"));
    q.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS servers ("
        "  id TEXT PRIMARY KEY, name TEXT, base_url TEXT NOT NULL,"
        "  last_user_id TEXT, last_used INTEGER DEFAULT 0)"));
    q.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS item_cache ("
        "  item_id TEXT PRIMARY KEY, json TEXT NOT NULL,"
        "  updated INTEGER DEFAULT (strftime('%s','now')))"));
}

void Database::putSecret(const QString &key, const QByteArray &blob)
{
    QSqlQuery q(QSqlDatabase::database(m_connName));
    q.prepare(QStringLiteral(
        "INSERT INTO secrets(key, blob) VALUES(?, ?) "
        "ON CONFLICT(key) DO UPDATE SET blob=excluded.blob"));
    q.addBindValue(key);
    q.addBindValue(blob);
    execOrWarn(q, "putSecret");
}

QByteArray Database::getSecret(const QString &key) const
{
    QSqlQuery q(QSqlDatabase::database(m_connName));
    q.prepare(QStringLiteral("SELECT blob FROM secrets WHERE key=?"));
    q.addBindValue(key);
    if (q.exec() && q.next())
        return q.value(0).toByteArray();
    return {};
}

void Database::removeSecret(const QString &key)
{
    QSqlQuery q(QSqlDatabase::database(m_connName));
    q.prepare(QStringLiteral("DELETE FROM secrets WHERE key=?"));
    q.addBindValue(key);
    execOrWarn(q, "removeSecret");
}

void Database::putSetting(const QString &key, const QString &value)
{
    QSqlQuery q(QSqlDatabase::database(m_connName));
    q.prepare(QStringLiteral(
        "INSERT INTO settings(key, value) VALUES(?, ?) "
        "ON CONFLICT(key) DO UPDATE SET value=excluded.value"));
    q.addBindValue(key);
    q.addBindValue(value);
    execOrWarn(q, "putSetting");
}

QString Database::getSetting(const QString &key, const QString &def) const
{
    QSqlQuery q(QSqlDatabase::database(m_connName));
    q.prepare(QStringLiteral("SELECT value FROM settings WHERE key=?"));
    q.addBindValue(key);
    if (q.exec() && q.next())
        return q.value(0).toString();
    return def;
}

void Database::removeSetting(const QString &key)
{
    QSqlQuery q(QSqlDatabase::database(m_connName));
    q.prepare(QStringLiteral("DELETE FROM settings WHERE key=?"));
    q.addBindValue(key);
    execOrWarn(q, "removeSetting");
}

void Database::upsertServer(const ServerRow &row)
{
    QSqlQuery q(QSqlDatabase::database(m_connName));
    q.prepare(QStringLiteral(
        "INSERT INTO servers(id, name, base_url, last_user_id, last_used) "
        "VALUES(?, ?, ?, ?, ?) "
        "ON CONFLICT(id) DO UPDATE SET name=excluded.name, base_url=excluded.base_url, "
        "last_user_id=excluded.last_user_id, last_used=excluded.last_used"));
    q.addBindValue(row.id);
    q.addBindValue(row.name);
    q.addBindValue(row.baseUrl);
    q.addBindValue(row.lastUserId);
    q.addBindValue(row.lastUsed);
    execOrWarn(q, "upsertServer");
}

QVector<Database::ServerRow> Database::servers() const
{
    QVector<ServerRow> out;
    QSqlQuery q(QSqlDatabase::database(m_connName));
    q.exec(QStringLiteral(
        "SELECT id, name, base_url, last_user_id, last_used FROM servers "
        "ORDER BY last_used DESC"));
    while (q.next()) {
        ServerRow r;
        r.id = q.value(0).toString();
        r.name = q.value(1).toString();
        r.baseUrl = q.value(2).toString();
        r.lastUserId = q.value(3).toString();
        r.lastUsed = q.value(4).toLongLong();
        out.push_back(r);
    }
    return out;
}

void Database::removeServer(const QString &id)
{
    QSqlQuery q(QSqlDatabase::database(m_connName));
    q.prepare(QStringLiteral("DELETE FROM servers WHERE id=?"));
    q.addBindValue(id);
    execOrWarn(q, "removeServer");
}

void Database::pruneItemCache(int maxRows)
{
    if (maxRows < 0)
        return;
    QSqlQuery q(QSqlDatabase::database(m_connName));
    // rowid tie-breaks entries written within the same second, so the ordering is
    // total and the newest `maxRows` are always the ones kept.
    q.prepare(QStringLiteral(
        "DELETE FROM item_cache WHERE item_id NOT IN ("
        "  SELECT item_id FROM item_cache ORDER BY updated DESC, rowid DESC LIMIT ?)"));
    q.addBindValue(maxRows);
    execOrWarn(q, "pruneItemCache");
}

void Database::cacheItem(const QString &itemId, const QJsonObject &json)
{
    QSqlQuery q(QSqlDatabase::database(m_connName));
    q.prepare(QStringLiteral(
        "INSERT INTO item_cache(item_id, json) VALUES(?, ?) "
        "ON CONFLICT(item_id) DO UPDATE SET json=excluded.json, "
        "updated=strftime('%s','now')"));
    q.addBindValue(itemId);
    q.addBindValue(QString::fromUtf8(QJsonDocument(json).toJson(QJsonDocument::Compact)));
    execOrWarn(q, "cacheItem");
}

QJsonObject Database::cachedItem(const QString &itemId) const
{
    QSqlQuery q(QSqlDatabase::database(m_connName));
    q.prepare(QStringLiteral("SELECT json FROM item_cache WHERE item_id=?"));
    q.addBindValue(itemId);
    if (q.exec() && q.next())
        return QJsonDocument::fromJson(q.value(0).toString().toUtf8()).object();
    return {};
}

void Database::removeCachedItemsForServer(const QString &serverId)
{
    QSqlQuery q(QSqlDatabase::database(m_connName));
    // Keys are "<serverId>/<itemId>". serverId is a fixed hex hash, so it contains
    // no LIKE metacharacters and the prefix match is unambiguous.
    q.prepare(QStringLiteral("DELETE FROM item_cache WHERE item_id LIKE ?"));
    q.addBindValue(serverId + QStringLiteral("/%"));
    execOrWarn(q, "removeCachedItemsForServer");
}
