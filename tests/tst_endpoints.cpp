#include <QTest>

#include "net/Endpoints.h"

class TstEndpoints : public QObject
{
    Q_OBJECT
private slots:
    void rootHostedServer()
    {
        Endpoints e(QUrl("https://abs.example.com"));
        QCOMPARE(e.status().toString(), QStringLiteral("https://abs.example.com/status"));
        QCOMPARE(e.libraries().toString(),
                 QStringLiteral("https://abs.example.com/api/libraries"));
    }

    void subPathHostedServerIsPreserved()
    {
        // The critical invariant: /api/... must resolve UNDER the sub-path.
        Endpoints e(QUrl("https://example.com/audiobookshelf"));
        QCOMPARE(e.status().toString(),
                 QStringLiteral("https://example.com/audiobookshelf/status"));
        QCOMPARE(e.item(QStringLiteral("abc")).toString(),
                 QStringLiteral("https://example.com/audiobookshelf/api/items/abc"));
    }

    void trailingSlashNormalised()
    {
        Endpoints e(QUrl("https://example.com/audiobookshelf/"));
        QCOMPARE(e.libraries().toString(),
                 QStringLiteral("https://example.com/audiobookshelf/api/libraries"));
    }

    void queryParamsAttached()
    {
        Endpoints e(QUrl("https://abs.example.com"));
        QUrlQuery q;
        q.addQueryItem(QStringLiteral("width"), QStringLiteral("400"));
        const QString s = e.cover(QStringLiteral("id1"), q).toString();
        QVERIFY(s.startsWith(QStringLiteral("https://abs.example.com/api/items/id1/cover?")));
        QVERIFY(s.contains(QStringLiteral("width=400")));
    }

    void authorImageUnderSubPath()
    {
        Endpoints e(QUrl("https://example.com/audiobookshelf"));
        QCOMPARE(e.authorImage(QStringLiteral("au1")).toString(),
                 QStringLiteral("https://example.com/audiobookshelf/api/authors/au1/image"));
    }

    void relativeContentUrlResolvesUnderSubPath()
    {
        Endpoints e(QUrl("https://example.com/audiobookshelf"));
        const QString s =
            e.resolveContentUrl(QStringLiteral("/api/items/x/file/1.m4b")).toString();
        QCOMPARE(s, QStringLiteral("https://example.com/audiobookshelf/api/items/x/file/1.m4b"));
    }

    void absoluteContentUrlUnchanged()
    {
        Endpoints e(QUrl("https://example.com/audiobookshelf"));
        const QString abs = QStringLiteral("https://cdn.example.com/stream.m3u8");
        QCOMPARE(e.resolveContentUrl(abs).toString(), abs);
    }
};

QTEST_MAIN(TstEndpoints)
#include "tst_endpoints.moc"
