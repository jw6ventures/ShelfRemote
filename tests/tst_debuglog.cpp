#include <QTest>

#include <QFile>
#include <QStandardPaths>

#include "app/AppConfig.h"
#include "app/DebugLog.h"

// The log file is written by the message handler in main() for the whole life of
// the process. An HTPC instance can stay up for weeks, so the size ceiling has to
// hold during a run, not just at startup.
class TstDebugLog : public QObject
{
    Q_OBJECT

    static QString logPath()  { return AppConfig::logFilePath(); }
    static QString backupPath() { return logPath() + QStringLiteral(".1"); }

    static void writeLog(qint64 bytes)
    {
        QFile f(logPath());
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        QCOMPARE(f.write(QByteArray(int(bytes), 'x')), bytes);
        f.close();
    }

private slots:
    void init()
    {
        QStandardPaths::setTestModeEnabled(true);
        QFile::remove(logPath());
        QFile::remove(backupPath());
    }

    void rotateMovesTheLogAsideAndReplacesAnyBackup()
    {
        QFile old(backupPath());
        QVERIFY(old.open(QIODevice::WriteOnly | QIODevice::Truncate));
        old.write(QByteArrayLiteral("previous backup"));
        old.close();

        writeLog(64);
        DebugLog::rotate();

        // The live log is gone (the handler recreates it on the next write) and
        // the single backup slot now holds what the log used to hold.
        QVERIFY(!QFile::exists(logPath()));
        QFile backup(backupPath());
        QVERIFY(backup.open(QIODevice::ReadOnly));
        QCOMPARE(backup.size(), qint64(64));
    }

    void rotateIfLargeLeavesASmallLogAlone()
    {
        writeLog(1024);
        DebugLog::rotateIfLarge();

        QVERIFY(QFile::exists(logPath()));
        QVERIFY(!QFile::exists(backupPath()));
    }

    void rotateIfLargeRotatesPastTheCeiling()
    {
        writeLog(DebugLog::kMaxLogBytes + 1);
        DebugLog::rotateIfLarge();

        QVERIFY(!QFile::exists(logPath()));
        QVERIFY(QFile::exists(backupPath()));
    }

    void rotateIfLargeToleratesAMissingLog()
    {
        QVERIFY(!QFile::exists(logPath()));
        DebugLog::rotateIfLarge(); // must not create anything
        QVERIFY(!QFile::exists(backupPath()));
    }
};

QTEST_GUILESS_MAIN(TstDebugLog)
#include "tst_debuglog.moc"
