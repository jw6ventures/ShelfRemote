#include "server/ServerManager.h"
#include "storage/Database.h"

#include <QDateTime>
#include <QHash>

#include <array>

namespace {
// Parses "2.17.5" (ignoring any "-beta"-style suffix) into a comparable triple.
std::array<int, 3> parseVersion(const QString &v)
{
    std::array<int, 3> out{0, 0, 0};
    const QString core = v.section(QLatin1Char('-'), 0, 0); // drop pre-release tag
    const QStringList parts = core.split(QLatin1Char('.'));
    for (int i = 0; i < 3 && i < parts.size(); ++i)
        out[size_t(i)] = parts.at(i).toInt();
    return out;
}

// True when `have` is >= `want` (major, then minor, then patch).
bool versionAtLeast(const QString &have, const std::array<int, 3> &want)
{
    const auto h = parseVersion(have);
    if (h[0] != want[0]) return h[0] > want[0];
    if (h[1] != want[1]) return h[1] > want[1];
    return h[2] >= want[2];
}
} // namespace

ServerManager::ServerManager(QObject *parent)
    : QAbstractListModel(parent)
{
    reload();
}

int ServerManager::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_servers.size();
}

QVariant ServerManager::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_servers.size())
        return {};
    const ServerProfile &s = m_servers.at(index.row());
    switch (role) {
    case IdRole:       return s.id;
    case NameRole:     return s.name;
    case BaseUrlRole:  return s.baseUrl.toString();
    case LastUserRole: return s.lastUserId;
    default:           return {};
    }
}

QHash<int, QByteArray> ServerManager::roleNames() const
{
    return {{IdRole, "serverId"},
            {NameRole, "name"},
            {BaseUrlRole, "baseUrl"},
            {LastUserRole, "lastUserId"}};
}

void ServerManager::reload()
{
    beginResetModel();
    m_servers.clear();
    for (const auto &row : Database::instance().servers()) {
        ServerProfile p;
        p.id = row.id;
        p.name = row.name;
        p.baseUrl = QUrl(row.baseUrl);
        p.lastUserId = row.lastUserId;
        m_servers.push_back(p);
    }
    endResetModel();
    emit countChanged();
}

void ServerManager::saveServer(const QUrl &baseUrl, const QString &name, const QString &userId)
{
    Database::ServerRow row;
    row.id = ServerProfile::idForUrl(baseUrl);
    row.name = name.isEmpty() ? baseUrl.host() : name;
    row.baseUrl = baseUrl.toString();
    row.lastUserId = userId;
    row.lastUsed = QDateTime::currentSecsSinceEpoch();
    Database::instance().upsertServer(row);
    reload();
}

void ServerManager::removeServer(const QString &id)
{
    // Forgetting a server must not leave its secrets and cached data orphaned in
    // the database. The id here is the same stable key TokenStore / the item cache
    // namespace their entries under, so we can purge them directly.
    Database::instance().removeServer(id);
    Database::instance().removeSecret(id + QStringLiteral("/tokens"));
    Database::instance().removeSetting(id + QStringLiteral("/lastLibraryId"));
    Database::instance().removeCachedItemsForServer(id);
    reload();
}

QUrl ServerManager::baseUrlFor(const QString &id) const
{
    for (const auto &s : m_servers)
        if (s.id == id)
            return s.baseUrl;
    return {};
}

bool ServerManager::hasCapability(const QString &feature) const
{
    // Minimum server versions for features that only exist on newer releases.
    // Thresholds are best-effort against the Audiobookshelf changelog and should
    // be refined as capabilities are added; an unknown feature (or unknown/blank
    // server version) is assumed available so we never hide functionality on a
    // server we simply have not classified.
    static const QHash<QString, std::array<int, 3>> kMinVersion = {
        {QStringLiteral("openidLogoutUrl"), {2, 2, 23}},
    };
    if (m_version.isEmpty())
        return true;
    const auto it = kMinVersion.constFind(feature);
    if (it == kMinVersion.constEnd())
        return true;
    return versionAtLeast(m_version, *it);
}
