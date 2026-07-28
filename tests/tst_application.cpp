#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFileInfo>
#include <QLocalSocket>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>

#include <atomic>

#include "app/AppConfig.h"
#include "app/Application.h"

class TstApplication : public QObject
{
    Q_OBJECT

private slots:
    void flatpakSocketUsesSharedRuntimeDirectory()
    {
        const QString runtime =
            QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
        const QString flatpakId = qEnvironmentVariable("FLATPAK_ID");
        if (runtime.isEmpty() || flatpakId.isEmpty())
            QSKIP("Test runner is not inside Flatpak");

        const QString expectedDir =
            QDir(runtime).filePath(QStringLiteral("app/") + flatpakId);
        QCOMPARE(QFileInfo(AppConfig::instanceSocketPath()).absolutePath(), expectedDir);
    }

    void secondaryForwardsCallbackAndExits()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const QString socketPath = QDir(temp.path()).filePath(QStringLiteral("instance"));
        const QString callback =
            QStringLiteral("jw6-shelfremote://oauth?code=test-code&state=test-state");

        SingleInstanceGuard primary(socketPath, this);
        QVERIFY(primary.acquire(QString()));
        QVERIFY2(QFileInfo::exists(socketPath), qPrintable(socketPath));
        {
            QLocalSocket probe;
            probe.connectToServer(socketPath);
            QVERIFY2(probe.waitForConnected(1000),
                     qPrintable(probe.errorString()));
            probe.disconnectFromServer();
        }
        QSignalSpy received(&primary, &SingleInstanceGuard::uriReceived);

        std::atomic_bool secondaryWasPrimary{true};
        std::atomic_bool secondaryDone{false};
        QThread *secondary = QThread::create([&]() {
            SingleInstanceGuard guard(socketPath, nullptr);
            secondaryWasPrimary.store(guard.acquire(callback));
            secondaryDone.store(true);
        });
        secondary->start();

        QElapsedTimer timeout;
        timeout.start();
        while (!secondaryDone.load() && timeout.elapsed() < 5000) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
            QTest::qWait(10);
        }
        const bool stopped = secondary->wait(1000);
        delete secondary;

        QVERIFY(stopped);
        QVERIFY(secondaryDone.load());
        QVERIFY(!secondaryWasPrimary.load());
        QCOMPARE(received.count(), 1);
        QCOMPARE(received.constFirst().constFirst().toString(), callback);
    }
};

QTEST_GUILESS_MAIN(TstApplication)
#include "tst_application.moc"
