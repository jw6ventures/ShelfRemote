#include <QTest>

#include <QStandardPaths>

#include "app/AppSettings.h"
#include "storage/Database.h"

class TstAppSettings : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        QVERIFY(Database::instance().open());
    }

    // Now Playing and Settings used to step differently (one wrapped after 3.0,
    // the other after 2.0), and a rate set from elsewhere — 1.1 over MPRIS, say —
    // was stepped from verbatim into 1.35, 1.6, … Both now share one ladder.
    void nextRateWalksOneLadder()
    {
        AppSettings settings;
        QCOMPARE(settings.nextRate(1.0), 1.25);
        QCOMPARE(settings.nextRate(2.0), 2.25);
        QCOMPARE(settings.nextRate(3.0), 0.75); // wraps to the slowest
        QCOMPARE(settings.nextRate(1.1), 1.25); // snaps onto the ladder
        QCOMPARE(settings.nextRate(0.5), 0.75);
        QCOMPARE(settings.nextRate(5.0), 0.75);
    }
};

QTEST_GUILESS_MAIN(TstAppSettings)
#include "tst_appsettings.moc"
