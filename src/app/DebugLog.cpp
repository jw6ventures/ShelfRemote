#include "app/DebugLog.h"
#include "app/AppConfig.h"

#include <QDebug>
#include <QFile>
#include <QFileInfo>

void DebugLog::rotate()
{
    const QString path = AppConfig::logFilePath();
    const QString backup = path + QStringLiteral(".1");
    QFile::remove(backup);
    QFile::rename(path, backup);
}

void DebugLog::rotateIfLarge()
{
    const QFileInfo fi(AppConfig::logFilePath());
    if (fi.exists() && fi.size() > kMaxLogBytes)
        rotate();
}

DebugLog::DebugLog(QObject *parent)
    : QObject(parent)
{
}

QString DebugLog::logFilePath() const
{
    return AppConfig::logFilePath();
}

bool DebugLog::saveTo(const QUrl &dest) const
{
    const QString destPath = dest.isLocalFile() ? dest.toLocalFile() : dest.path();
    if (destPath.isEmpty())
        return false;

    QFile out(destPath);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qWarning() << "DebugLog: cannot open destination for writing:" << destPath;
        return false;
    }

    QFile in(AppConfig::logFilePath());
    if (in.exists() && in.open(QIODevice::ReadOnly)) {
        out.write(in.readAll());
        in.close();
    }
    out.close();
    return true;
}
