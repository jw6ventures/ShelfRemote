#include <QTest>

#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTcpSocket>

#include "net/ApiClient.h"
#include "player/MpvController.h"
#include "player/PlaybackSession.h"
#include "storage/Database.h"

namespace {

// A throwaway Audiobookshelf stand-in: answers /play with a usable two-track
// session and records every /sync and /close it receives, so a test can assert on
// what the client actually put on the wire.
class FakeServer : public QObject
{
    Q_OBJECT
public:
    int syncCount = 0;
    int closeCount = 0;
    double lastSyncedTime = 0.0;

    bool start()
    {
        if (!m_server.listen(QHostAddress::LocalHost))
            return false;
        connect(&m_server, &QTcpServer::newConnection, this, &FakeServer::onConnection);
        return true;
    }

    QUrl baseUrl() const
    {
        return QUrl(QStringLiteral("http://127.0.0.1:%1").arg(m_server.serverPort()));
    }

private:
    void onConnection()
    {
        while (QTcpSocket *sock = m_server.nextPendingConnection()) {
            auto *buffer = new QByteArray;
            connect(sock, &QTcpSocket::readyRead, this, [this, sock, buffer]() {
                buffer->append(sock->readAll());
                // Requests here are small and always carry Content-Length, so a
                // whole request is present once the body has caught up with it.
                const int headerEnd = buffer->indexOf("\r\n\r\n");
                if (headerEnd < 0)
                    return;
                const QByteArray head = buffer->left(headerEnd);
                const int bodyLen = contentLength(head);
                if (buffer->size() < headerEnd + 4 + bodyLen)
                    return;
                const QByteArray body = buffer->mid(headerEnd + 4, bodyLen);
                buffer->remove(0, headerEnd + 4 + bodyLen);
                respond(sock, head.left(head.indexOf("\r\n")), body);
            });
            connect(sock, &QTcpSocket::disconnected, sock, &QTcpSocket::deleteLater);
            connect(sock, &QObject::destroyed, this, [buffer]() { delete buffer; });
        }
    }

    static int contentLength(const QByteArray &head)
    {
        for (const QByteArray &line : head.split('\n')) {
            const QByteArray trimmed = line.trimmed();
            if (trimmed.toLower().startsWith("content-length:"))
                return trimmed.mid(trimmed.indexOf(':') + 1).trimmed().toInt();
        }
        return 0;
    }

    void respond(QTcpSocket *sock, const QByteArray &requestLine, const QByteArray &body)
    {
        QJsonObject payload;
        if (requestLine.contains("/play")) {
            payload = playSession();
        } else if (requestLine.contains("/sync")) {
            ++syncCount;
            lastSyncedTime = QJsonDocument::fromJson(body)
                                 .object()
                                 .value(QStringLiteral("currentTime"))
                                 .toDouble();
        } else if (requestLine.contains("/close")) {
            ++closeCount;
            lastSyncedTime = QJsonDocument::fromJson(body)
                                 .object()
                                 .value(QStringLiteral("currentTime"))
                                 .toDouble();
        }
        const QByteArray json = QJsonDocument(payload).toJson(QJsonDocument::Compact);
        sock->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: "
                    + QByteArray::number(json.size()) + "\r\n\r\n" + json);
        sock->flush();
    }

    static QJsonObject playSession()
    {
        return QJsonObject{
            {QStringLiteral("id"), QStringLiteral("session-1")},
            {QStringLiteral("duration"), 2000.0},
            {QStringLiteral("displayTitle"), QStringLiteral("A Book")},
            {QStringLiteral("currentTime"), 0.0},
            {QStringLiteral("audioTracks"), QJsonArray{
                QJsonObject{{QStringLiteral("index"), 0},
                            {QStringLiteral("startOffset"), 0.0},
                            {QStringLiteral("duration"), 1000.0},
                            {QStringLiteral("contentUrl"), QStringLiteral("/file/1.mp3")},
                            {QStringLiteral("mimeType"), QStringLiteral("audio/mpeg")}},
                QJsonObject{{QStringLiteral("index"), 1},
                            {QStringLiteral("startOffset"), 1000.0},
                            {QStringLiteral("duration"), 1000.0},
                            {QStringLiteral("contentUrl"), QStringLiteral("/file/2.mp3")},
                            {QStringLiteral("mimeType"), QStringLiteral("audio/mpeg")}}}},
        };
    }

    QTcpServer m_server;
};

} // namespace

// Seeking is what the remote's skip button does, so a held button produces a
// burst of them. These tests pin that the burst reaches the server as one report
// carrying the final position, rather than one request per press.
class TstPlaybackSession : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        Database::instance().open(); // PlaybackSession reads the default rate setting
    }

    void aBurstOfSeeksBecomesOneSync()
    {
        FakeServer server;
        QVERIFY(server.start());

        ApiClient api;
        api.setBaseUrl(server.baseUrl());
        // Never initialised: every MpvController call is a guarded no-op, which is
        // exactly the isolation this test wants around the session's own logic.
        MpvController mpv;
        PlaybackSession session(&api, &mpv);

        session.playItem(QStringLiteral("item-1"));
        QTRY_VERIFY_WITH_TIMEOUT(session.active(), 3000);
        QCOMPARE(server.syncCount, 0);

        // Ten rapid skips, as a held-down remote button produces.
        for (int i = 0; i < 10; ++i)
            session.skip(30.0);
        QCOMPARE(session.position(), 300.0);

        // Nothing goes out immediately: the burst is still settling.
        QCOMPARE(server.syncCount, 0);

        // One request follows, carrying where the user actually ended up.
        QTRY_VERIFY_WITH_TIMEOUT(server.syncCount == 1, 5000);
        QCOMPARE(server.lastSyncedTime, 300.0);

        // And it stays at one — the coalesced request is not repeated.
        QTest::qWait(1500);
        QCOMPARE(server.syncCount, 1);
    }

    void closingReportsTheLatestPositionWithoutAnExtraSync()
    {
        FakeServer server;
        QVERIFY(server.start());

        ApiClient api;
        api.setBaseUrl(server.baseUrl());
        MpvController mpv;
        PlaybackSession session(&api, &mpv);

        session.playItem(QStringLiteral("item-1"));
        QTRY_VERIFY_WITH_TIMEOUT(session.active(), 3000);

        session.seekGlobal(750.0);
        // Stop before the coalescing window elapses: the close must carry the seek
        // rather than leaving a pending sync to fire against a dead session.
        session.stopAndClose();
        QVERIFY(!session.active());

        QTRY_VERIFY_WITH_TIMEOUT(server.closeCount == 1, 3000);
        QCOMPARE(server.lastSyncedTime, 750.0);

        QTest::qWait(1500);
        QCOMPARE(server.syncCount, 0);
        QCOMPARE(server.closeCount, 1);
    }
};

QTEST_GUILESS_MAIN(TstPlaybackSession)
#include "tst_playbacksession.moc"
