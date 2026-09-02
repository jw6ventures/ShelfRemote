#include <QTest>

#include <QJsonObject>
#include <QStandardPaths>

#include "storage/Database.h"

// The item cache is the only table with no natural upper bound — it gains a row
// for every item detail ever opened. These tests pin the prune that bounds it and
// the per-server purge that runs when a server is forgotten.
class TstDatabase : public QObject
{
    Q_OBJECT

    static QJsonObject item(const QString &title)
    {
        return QJsonObject{{QStringLiteral("title"), title}};
    }

private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        QVERIFY(Database::instance().open());
    }

    void init()
    {
        Database::instance().pruneItemCache(0); // empty the table between cases
    }

    void pruneKeepsTheNewestEntries()
    {
        // `updated` has one-second resolution, so all three rows share a timestamp
        // here; the prune tie-breaks on rowid, which is insertion order.
        Database::instance().cacheItem(QStringLiteral("s/1"), item(QStringLiteral("one")));
        Database::instance().cacheItem(QStringLiteral("s/2"), item(QStringLiteral("two")));
        Database::instance().cacheItem(QStringLiteral("s/3"), item(QStringLiteral("three")));

        Database::instance().pruneItemCache(2);

        QVERIFY(Database::instance().cachedItem(QStringLiteral("s/1")).isEmpty());
        QCOMPARE(Database::instance().cachedItem(QStringLiteral("s/2")),
                 item(QStringLiteral("two")));
        QCOMPARE(Database::instance().cachedItem(QStringLiteral("s/3")),
                 item(QStringLiteral("three")));
    }

    void pruneIsANoOpBelowTheLimit()
    {
        Database::instance().cacheItem(QStringLiteral("s/1"), item(QStringLiteral("one")));
        Database::instance().pruneItemCache(10);
        QCOMPARE(Database::instance().cachedItem(QStringLiteral("s/1")),
                 item(QStringLiteral("one")));
    }

    void forgettingAServerPurgesOnlyItsItems()
    {
        Database::instance().cacheItem(QStringLiteral("srvA/1"), item(QStringLiteral("a1")));
        Database::instance().cacheItem(QStringLiteral("srvB/1"), item(QStringLiteral("b1")));

        Database::instance().removeCachedItemsForServer(QStringLiteral("srvA"));

        QVERIFY(Database::instance().cachedItem(QStringLiteral("srvA/1")).isEmpty());
        QCOMPARE(Database::instance().cachedItem(QStringLiteral("srvB/1")),
                 item(QStringLiteral("b1")));
    }
};

QTEST_GUILESS_MAIN(TstDatabase)
#include "tst_database.moc"
