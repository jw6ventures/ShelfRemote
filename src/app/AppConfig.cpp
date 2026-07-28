#include "app/AppConfig.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QUuid>

namespace AppConfig {

QString dataDir()
{
    QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (base.isEmpty())
        base = QDir::homePath() + QStringLiteral("/.local/share/") + appId();
    QDir().mkpath(base);
    return base;
}

QString cacheDir()
{
    QString base = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    if (base.isEmpty())
        base = QDir::homePath() + QStringLiteral("/.cache/") + appId();
    QDir().mkpath(base);
    return base;
}

QString databasePath()
{
    return dataDir() + QStringLiteral("/shelfremote.db");
}

QString coverCacheDir()
{
    const QString dir = cacheDir() + QStringLiteral("/covers");
    QDir().mkpath(dir);
    return dir;
}

QString instanceSocketPath()
{
    QString dir = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);

    // Flatpak exposes this per-app directory to every sandbox instance of the
    // same application, whereas each instance receives a different private /tmp.
    const QString flatpakId = qEnvironmentVariable("FLATPAK_ID");
    if (!dir.isEmpty() && !flatpakId.isEmpty()) {
        const QString shared =
            QDir(dir).filePath(QStringLiteral("app/") + flatpakId);
        if (QDir().mkpath(shared) && QFileInfo(shared).isWritable())
            dir = shared;
        else
            dir.clear();
    }

    // RuntimeLocation should be available on a desktop session. Keep a shared,
    // app-private fallback for unusual/headless environments.
    if (dir.isEmpty()
        || (!QDir().mkpath(dir) && !QFileInfo(dir).isDir())
        || !QFileInfo(dir).isWritable()) {
        dir = dataDir();
    }

    return QDir(dir).filePath(appId() + QStringLiteral(".instance"));
}

QString logFilePath()
{
    const QString dir = cacheDir() + QStringLiteral("/logs");
    QDir().mkpath(dir);
    return dir + QStringLiteral("/shelfremote.log");
}

QString deviceId()
{
    const QString path = dataDir() + QStringLiteral("/device-id");
    QFile f(path);
    if (f.exists() && f.open(QIODevice::ReadOnly)) {
        const QString id = QString::fromUtf8(f.readAll()).trimmed();
        if (!id.isEmpty())
            return id;
    }
    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        f.write(id.toUtf8());
    }
    return id;
}

} // namespace AppConfig
