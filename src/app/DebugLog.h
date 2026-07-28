#pragma once

#include <QObject>
#include <QString>
#include <QUrl>

// Small QML-facing helper for the Settings "Save debug log" action. The log file
// itself is written by the message handler installed in main(); this exposes its
// path and copies it to a user-chosen destination (picked via the file portal, so
// no extra Flatpak filesystem permission is required).
class DebugLog : public QObject
{
    Q_OBJECT
public:
    explicit DebugLog(QObject *parent = nullptr);

    Q_INVOKABLE QString logFilePath() const;
    // Copies the current log to `dest` (a file:// URL from the Save dialog).
    // Returns true on success.
    Q_INVOKABLE bool saveTo(const QUrl &dest) const;
};
