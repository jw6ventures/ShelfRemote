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

    // Ceiling for the live log file. Two files (the log and one .1 backup) are
    // kept, so the log never occupies more than about twice this on disk.
    static constexpr qint64 kMaxLogBytes = 1 * 1024 * 1024;

    // Moves the current log aside to a single .1 backup, replacing any previous
    // one. The message handler recreates the log on its next write.
    static void rotate();
    // Rotates only if the log has passed kMaxLogBytes. The handler calls this
    // after every write: an HTPC instance can stay up for weeks, so rotating only
    // at startup let a single run grow the file without limit.
    static void rotateIfLarge();
};
