#include <QTest>

#include <QHostAddress>
#include <QTcpServer>

#include "net/ApiClient.h"

// Pins the cross-server isolation guarantees added to ApiClient: the origin epoch
// bumps only on a real origin change, and a request that is in flight when the
// origin changes completes as `stale` (so callers drop it and the 401-refresh
// retry never replays it with a different server's bearer token).
class TstApiClient : public QObject
{
    Q_OBJECT

private slots:
    void epochBumpsOnlyOnOriginChange()
    {
        ApiClient api;
        QCOMPARE(api.epoch(), quint64(0));

        api.setBaseUrl(QUrl(QStringLiteral("https://a.example")));
        const quint64 e1 = api.epoch();
        QVERIFY(e1 > 0);

        api.setBaseUrl(QUrl(QStringLiteral("https://a.example"))); // unchanged
        QCOMPARE(api.epoch(), e1);

        api.setBaseUrl(QUrl(QStringLiteral("https://b.example"))); // changed
        QVERIFY(api.epoch() > e1);
    }

    void inFlightRequestBecomesStaleOnOriginChange()
    {
        // A server that accepts the connection but never replies, so the request
        // is still in flight when we switch origins and abort it.
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        bool connected = false;
        connect(&server, &QTcpServer::newConnection, &server, [&]() { connected = true; });

        ApiClient api;
        const QUrl base(QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort()));
        api.setBaseUrl(base);
        const quint64 originalEpoch = api.epoch();

        bool called = false;
        ApiResponse got;
        api.get(QUrl(base.toString() + QStringLiteral("/status")),
                [&](const ApiResponse &res) { called = true; got = res; });

        // Let the request reach the server (it will hang there, unanswered).
        QTRY_VERIFY_WITH_TIMEOUT(connected, 3000);
        QVERIFY(!called);

        // Switch to a different origin: this must bump the epoch and abort the
        // in-flight reply, which then completes flagged stale and not-ok.
        api.setBaseUrl(QUrl(QStringLiteral("http://127.0.0.1:9")));
        QVERIFY(api.epoch() > originalEpoch);

        QTRY_VERIFY_WITH_TIMEOUT(called, 3000);
        QVERIFY(got.stale);
        QVERIFY(!got.ok);
    }
};

QTEST_GUILESS_MAIN(TstApiClient)
#include "tst_apiclient.moc"
