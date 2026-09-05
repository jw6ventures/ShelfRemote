#include <QTest>

#include <QJsonArray>
#include <QSignalSpy>
#include <QJsonObject>

#include "model/ProgressStore.h"

// Browse cards read their progress bar and completed badge straight out of this
// store, and nothing refreshes it between logins except the writes below — so a
// wrong value here stays on screen.
class TstProgressStore : public QObject
{
    Q_OBJECT

    static QJsonObject userWith(double progress, double currentTime, bool finished)
    {
        return QJsonObject{{QStringLiteral("mediaProgress"), QJsonArray{
            QJsonObject{{QStringLiteral("libraryItemId"), QStringLiteral("item-1")},
                        {QStringLiteral("progress"), progress},
                        {QStringLiteral("currentTime"), currentTime},
                        {QStringLiteral("isFinished"), finished}}}}};
    }

private slots:
    void unmarkingFinishedDoesNotLeaveACompletedBar()
    {
        ProgressStore store;
        // Signed in with the book finished, as the server reported it.
        store.loadFromUser(userWith(1.0, 3600.0, true));
        QCOMPARE(store.fraction(QStringLiteral("item-1")), 1.0);
        QVERIFY(store.isFinished(QStringLiteral("item-1")));

        // The user un-marks it. Only the flag is known locally, so the card should
        // stop showing the completed badge right away.
        store.setFinished(QStringLiteral("item-1"), false);
        QVERIFY(!store.isFinished(QStringLiteral("item-1")));

        // Then the item load lands with the server's own numbers, and the bar
        // follows them rather than staying pinned at 100%.
        store.set(QStringLiteral("item-1"), 0.25, 900.0, false);
        QCOMPARE(store.fraction(QStringLiteral("item-1")), 0.25);
        QCOMPARE(store.currentTime(QStringLiteral("item-1")), 900.0);
        QVERIFY(!store.isFinished(QStringLiteral("item-1")));
    }

    void setReplacesWhatTheServerReports()
    {
        ProgressStore store;
        store.loadFromUser(userWith(0.8, 2880.0, false));

        store.set(QStringLiteral("item-1"), 0.1, 360.0, false);
        QCOMPARE(store.fraction(QStringLiteral("item-1")), 0.1);
        QCOMPARE(store.currentTime(QStringLiteral("item-1")), 360.0);

        // Out-of-range fractions from a server are clamped rather than propagated
        // into a progress bar wider than its track.
        store.set(QStringLiteral("item-1"), 1.7, 360.0, false);
        QCOMPARE(store.fraction(QStringLiteral("item-1")), 1.0);
        store.set(QStringLiteral("item-1"), -0.5, 360.0, false);
        QCOMPARE(store.fraction(QStringLiteral("item-1")), 0.0);
    }

    void markingFinishedOnAnUnseenItemStillShows()
    {
        ProgressStore store;
        QVERIFY(!store.has(QStringLiteral("fresh")));

        store.setFinished(QStringLiteral("fresh"), true);
        QVERIFY(store.isFinished(QStringLiteral("fresh")));
        QCOMPARE(store.fraction(QStringLiteral("fresh")), 1.0);
    }

    void changedIsEmittedSoCardsRepaint()
    {
        ProgressStore store;
        QSignalSpy spy(&store, &ProgressStore::changed);

        store.set(QStringLiteral("item-1"), 0.5, 100.0, false);
        QCOMPARE(spy.count(), 1);
        store.setFinished(QStringLiteral("item-1"), true);
        QCOMPARE(spy.count(), 2);
        // A no-op toggle must not churn every card in the grid.
        store.setFinished(QStringLiteral("item-1"), true);
        QCOMPARE(spy.count(), 2);
    }
};

QTEST_GUILESS_MAIN(TstProgressStore)
#include "tst_progressstore.moc"
