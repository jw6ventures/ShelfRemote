#include <QTest>

#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTcpSocket>

#include "app/Backend.h"
#include "model/ProgressStore.h"
#include "net/ApiClient.h"
#include "storage/Database.h"

namespace {

// Serves one library item, and can be told to start failing so the offline
// fallback can be exercised against the same item it previously cached.
class FakeServer : public QObject
{
    Q_OBJECT
public:
    bool failEverything = false;
    double progress = 0.25;
    bool finished = false;

    bool start()
    {
        if (!m_server.listen(QHostAddress::LocalHost))
            return false;
        connect(&m_server, &QTcpServer::newConnection, this, [this]() {
            while (QTcpSocket *sock = m_server.nextPendingConnection()) {
                connect(sock, &QTcpSocket::readyRead, this, [this, sock]() {
                    if (!sock->readAll().contains("GET "))
                        return;
                    if (failEverything) {
                        sock->write("HTTP/1.1 503 Service Unavailable\r\n"
                                    "Content-Length: 0\r\n\r\n");
                        sock->flush();
                        return;
                    }
                    const QByteArray json = QJsonDocument(item()).toJson(QJsonDocument::Compact);
                    sock->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n"
                                "Content-Length: " + QByteArray::number(json.size())
                                + "\r\n\r\n" + json);
                    sock->flush();
                });
                connect(sock, &QTcpSocket::disconnected, sock, &QTcpSocket::deleteLater);
            }
        });
        return true;
    }

    QUrl baseUrl() const
    {
        return QUrl(QStringLiteral("http://127.0.0.1:%1").arg(m_server.serverPort()));
    }

private:
    QJsonObject item() const
    {
        return QJsonObject{
            {QStringLiteral("id"), QStringLiteral("item-1")},
            {QStringLiteral("media"), QJsonObject{
                {QStringLiteral("metadata"), QJsonObject{
                    {QStringLiteral("title"), QStringLiteral("A Book")}}}}},
            {QStringLiteral("userMediaProgress"), QJsonObject{
                {QStringLiteral("progress"), progress},
                {QStringLiteral("currentTime"), 900.0},
                {QStringLiteral("isFinished"), finished}}},
        };
    }

    QTcpServer m_server;
};

} // namespace

class TstBackend : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        QVERIFY(Database::instance().open());
    }

    void loadingAnItemRefreshesTheSharedProgressStore()
    {
        FakeServer server;
        QVERIFY(server.start());

        ApiClient api;
        api.setBaseUrl(server.baseUrl());
        ProgressStore progress;
        Backend backend(&api);
        backend.setProgressStore(&progress);

        // Nothing known about this item yet — as after a sign-in that predates it.
        QVERIFY(!progress.has(QStringLiteral("item-1")));

        QSignalSpy loaded(&backend, &Backend::itemLoaded);
        backend.loadItem(QStringLiteral("item-1"));
        QTRY_COMPARE_WITH_TIMEOUT(loaded.count(), 1, 5000);

        // The reply is the server's own view of where the user is, so the browse
        // cards should now be showing that rather than whatever sign-in reported.
        QVERIFY(progress.has(QStringLiteral("item-1")));
        QCOMPARE(progress.fraction(QStringLiteral("item-1")), 0.25);
        QCOMPARE(progress.currentTime(QStringLiteral("item-1")), 900.0);
        QVERIFY(!progress.isFinished(QStringLiteral("item-1")));

        // A book completed elsewhere is picked up on the next open.
        server.progress = 1.0;
        server.finished = true;
        backend.loadItem(QStringLiteral("item-1"));
        QTRY_COMPARE_WITH_TIMEOUT(loaded.count(), 2, 5000);
        QVERIFY(progress.isFinished(QStringLiteral("item-1")));
    }

    void anUnreachableServerFallsBackToTheCachedItem()
    {
        FakeServer server;
        QVERIFY(server.start());

        ApiClient api;
        api.setBaseUrl(server.baseUrl());
        ProgressStore progress;
        Backend backend(&api);
        backend.setProgressStore(&progress);

        QSignalSpy loaded(&backend, &Backend::itemLoaded);
        QSignalSpy errors(&backend, &Backend::errorOccurred);

        backend.loadItem(QStringLiteral("item-1")); // caches it
        QTRY_COMPARE_WITH_TIMEOUT(loaded.count(), 1, 5000);
        QCOMPARE(errors.count(), 0);

        server.failEverything = true;
        backend.loadItem(QStringLiteral("item-1"));

        // The details screen is still filled in, from the copy saved last time...
        QTRY_COMPARE_WITH_TIMEOUT(loaded.count(), 2, 5000);
        QCOMPARE(loaded.at(1).at(0).toMap().value(QStringLiteral("id")).toString(),
                 QStringLiteral("item-1"));
        // ...and the user is told, rather than the stale copy passing as current.
        QCOMPARE(errors.count(), 1);
    }
};

QTEST_GUILESS_MAIN(TstBackend)
#include "tst_backend.moc"
