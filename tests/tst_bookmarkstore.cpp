#include <QTest>

#include <QJsonArray>
#include <QJsonObject>
#include <QVariantList>
#include <QVariantMap>

#include "model/BookmarkStore.h"
#include "net/ApiClient.h"

// Exercises the local bookmark cache: parsing the /api/authorize payload, sorted
// per-item lookup, and the optimistic add/remove mutations. The event loop is
// never spun, so the network callbacks never fire — only the synchronous local
// cache updates are under test.
class TstBookmarkStore : public QObject
{
    Q_OBJECT

private:
    static QJsonObject userWithBookmarks()
    {
        QJsonArray marks;
        marks.append(QJsonObject{{"libraryItemId", "A"}, {"time", 300}, {"title", "late"}});
        marks.append(QJsonObject{{"libraryItemId", "A"}, {"time", 120}, {"title", "early"}});
        marks.append(QJsonObject{{"libraryItemId", "B"}, {"time", 60}, {"title", "bee"}});
        return QJsonObject{{"bookmarks", marks}};
    }

private slots:
    void loadsAndSortsByTime()
    {
        ApiClient api;
        BookmarkStore bs(&api);
        bs.loadFromUser(userWithBookmarks());

        const QVariantList a = bs.forItem(QStringLiteral("A"));
        QCOMPARE(a.size(), 2);
        QCOMPARE(a.at(0).toMap().value("time").toDouble(), 120.0); // sorted ascending
        QCOMPARE(a.at(1).toMap().value("time").toDouble(), 300.0);
        QCOMPARE(a.at(0).toMap().value("title").toString(), QStringLiteral("early"));

        QCOMPARE(bs.forItem(QStringLiteral("B")).size(), 1);
        QVERIFY(bs.forItem(QStringLiteral("missing")).isEmpty());
    }

    void hasMatchesByWholeSecond()
    {
        ApiClient api;
        BookmarkStore bs(&api);
        bs.loadFromUser(userWithBookmarks());
        QVERIFY(bs.has(QStringLiteral("A"), 120.0));
        QVERIFY(bs.has(QStringLiteral("A"), 120.9)); // floored to 120
        QVERIFY(!bs.has(QStringLiteral("A"), 121.0));
        QVERIFY(!bs.has(QStringLiteral("B"), 300.0));
    }

    void addInsertsOptimisticallyAndDedupes()
    {
        ApiClient api;
        api.setBaseUrl(QUrl(QStringLiteral("http://127.0.0.1:9"))); // valid but unreachable
        BookmarkStore bs(&api);
        bs.loadFromUser(userWithBookmarks());

        int changes = 0;
        connect(&bs, &BookmarkStore::changed, this, [&changes]() { ++changes; });

        bs.add(QStringLiteral("A"), 200.4, QStringLiteral("mid"));
        QVERIFY(bs.has(QStringLiteral("A"), 200.0));
        QCOMPARE(bs.forItem(QStringLiteral("A")).size(), 3);
        QCOMPARE(changes, 1);

        // Same whole second: no duplicate, no extra change signal.
        bs.add(QStringLiteral("A"), 200.0, QStringLiteral("dup"));
        QCOMPARE(bs.forItem(QStringLiteral("A")).size(), 3);
        QCOMPARE(changes, 1);
    }

    void removeDropsOptimistically()
    {
        ApiClient api;
        api.setBaseUrl(QUrl(QStringLiteral("http://127.0.0.1:9")));
        BookmarkStore bs(&api);
        bs.loadFromUser(userWithBookmarks());

        bs.remove(QStringLiteral("A"), 120.0);
        QVERIFY(!bs.has(QStringLiteral("A"), 120.0));
        QCOMPARE(bs.forItem(QStringLiteral("A")).size(), 1);

        // Removing something absent is a no-op.
        bs.remove(QStringLiteral("A"), 999.0);
        QCOMPARE(bs.forItem(QStringLiteral("A")).size(), 1);
    }

    void clearEmptiesEverything()
    {
        ApiClient api;
        BookmarkStore bs(&api);
        bs.loadFromUser(userWithBookmarks());
        bs.clear();
        QVERIFY(bs.forItem(QStringLiteral("A")).isEmpty());
        QVERIFY(bs.forItem(QStringLiteral("B")).isEmpty());
    }
};

QTEST_GUILESS_MAIN(TstBookmarkStore)
#include "tst_bookmarkstore.moc"
