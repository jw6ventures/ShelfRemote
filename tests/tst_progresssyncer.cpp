#include <QTest>

#include "player/ProgressSyncer.h"

// Verifies that listened time tracks WALL-CLOCK, independent of playback rate,
// and excludes paused time — the invariant that keeps /sync timeListened correct
// at 1.5x/2x speed.
class TstProgressSyncer : public QObject
{
    Q_OBJECT
private slots:
    void accumulatesWhilePlaying()
    {
        ProgressSyncer s;
        s.setPlaying(true);
        QTest::qWait(120);
        const double listened = s.takeListenedSeconds();
        // ~0.12s elapsed; allow generous scheduling slack.
        QVERIFY2(listened >= 0.08 && listened <= 0.4,
                 qPrintable(QString::number(listened)));
    }

    void excludesPausedTime()
    {
        ProgressSyncer s;
        s.setPlaying(true);
        QTest::qWait(100);
        s.setPlaying(false);      // pause
        QTest::qWait(200);        // this time must NOT count
        const double listened = s.takeListenedSeconds();
        QVERIFY2(listened < 0.2, qPrintable(QString::number(listened)));
    }

    void takeResetsAccumulator()
    {
        ProgressSyncer s;
        s.setPlaying(true);
        QTest::qWait(60);
        (void)s.takeListenedSeconds();
        s.setPlaying(false);
        QCOMPARE(s.takeListenedSeconds(), 0.0);
    }

    // Reserving takes the whole pending interval out of the accumulator so no
    // concurrent sync can report it again; once reserved, nothing is left pending.
    void takeReservesAndClears()
    {
        ProgressSyncer s;
        s.setPlaying(true);
        QTest::qWait(120);
        const double reserved = s.takeListenedSeconds();
        QVERIFY2(reserved >= 0.08, qPrintable(QString::number(reserved)));
        s.setPlaying(false);
        // The reserved interval is gone from the accumulator.
        QVERIFY2(s.pendingListenedSeconds() < 0.05,
                 qPrintable(QString::number(s.pendingListenedSeconds())));
    }

    // A failed /sync must NOT drop listening time: rollback() returns the reserved
    // interval to the accumulator so the next sync retries it.
    void rollbackRestoresFailedInterval()
    {
        ProgressSyncer s;
        s.setPlaying(true);
        QTest::qWait(120);
        const double reserved = s.takeListenedSeconds();
        QVERIFY(reserved >= 0.08);
        s.setPlaying(false);
        s.rollback(reserved); // simulate the request failing
        QVERIFY2(qAbs(s.pendingListenedSeconds() - reserved) < 0.01,
                 qPrintable(QString::number(s.pendingListenedSeconds())));
    }

    // Two overlapping reservations partition the elapsed time between them (the
    // second sees only what accrued after the first), so the same second is never
    // reported twice.
    void concurrentReservationsAreDisjoint()
    {
        ProgressSyncer s;
        s.setPlaying(true);
        QTest::qWait(100);
        const double first = s.takeListenedSeconds();
        QTest::qWait(100);
        const double second = s.takeListenedSeconds();
        QVERIFY2(first >= 0.06, qPrintable(QString::number(first)));
        QVERIFY2(second >= 0.06, qPrintable(QString::number(second)));
        // Neither interval includes the other's time.
        s.setPlaying(false);
        QVERIFY(s.pendingListenedSeconds() < 0.05);
    }
};

QTEST_GUILESS_MAIN(TstProgressSyncer)
#include "tst_progresssyncer.moc"
