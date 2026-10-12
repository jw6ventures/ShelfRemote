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

    // With nothing cached either, the details screen needs to know to stop
    // waiting rather than sit on a half-built page.
    void anUncachedItemThatCannotLoadIsReported()
    {
        FakeServer server;
        server.failEverything = true;
        QVERIFY(server.start());

        ApiClient api;
        api.setBaseUrl(server.baseUrl());
        Backend backend(&api);

        QSignalSpy loaded(&backend, &Backend::itemLoaded);
        QSignalSpy failed(&backend, &Backend::itemLoadFailed);
        backend.loadItem(QStringLiteral("never-cached"));
        QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 5000);
        QCOMPARE(failed.at(0).at(0).toString(), QStringLiteral("never-cached"));
        QCOMPARE(loaded.count(), 0);
    }

    // A search that cannot run still settles, so the screen never shows
    // "Searching…" forever.
    void aSearchWithNoLibrarySettles()
    {
        ApiClient api;
        Backend backend(&api);
        QSignalSpy finished(&backend, &Backend::searchFinished);
        backend.search(QStringLiteral("dune"));
        QCOMPARE(finished.count(), 1);
        QCOMPARE(finished.at(0).at(1).toBool(), false);
    }

    // One card per picked playlist: the book in progress on it if there is one,
    // else the first unfinished book. Finished-through playlists and playlists
    // nobody picked contribute nothing.
    void playlistNextUpPicksTheNextBook()
    {
        ApiClient api;
        ProgressStore progress;
        Backend backend(&api);
        backend.setProgressStore(&progress);

        auto book = [](const char *id) {
            return QJsonObject{
                {QStringLiteral("libraryItemId"), QString::fromLatin1(id)},
                {QStringLiteral("libraryItem"), QJsonObject{
                    {QStringLiteral("id"), QString::fromLatin1(id)},
                    {QStringLiteral("media"), QJsonObject{
                        {QStringLiteral("metadata"), QJsonObject{
                            {QStringLiteral("title"), QString::fromLatin1(id)}}}}}}}};
        };
        auto playlist = [](const char *id, const char *name, const QJsonArray &items) {
            return QJsonObject{{QStringLiteral("id"), QString::fromLatin1(id)},
                               {QStringLiteral("name"), QString::fromLatin1(name)},
                               {QStringLiteral("items"), items}};
        };
        backend.playlists()->setPlaylists(QJsonArray{
            playlist("pl-unread", "Unread", QJsonArray{book("u1"), book("u2"), book("u3")}),
            playlist("pl-reading", "Reading", QJsonArray{book("r1"), book("r2"), book("r3")}),
            playlist("pl-done", "Done", QJsonArray{book("d1")}),
            playlist("pl-ignored", "Ignored", QJsonArray{book("i1")}),
        });
        progress.set(QStringLiteral("u1"), 1.0, 3600.0, true);   // finished
        progress.set(QStringLiteral("r1"), 1.0, 3600.0, true);   // finished
        progress.set(QStringLiteral("r3"), 0.4, 1200.0, false);  // in progress, after unread r2
        progress.set(QStringLiteral("d1"), 1.0, 3600.0, true);

        ShelfModel *shelves = backend.homeShelves();
        auto nextUp = [shelves]() -> QVariantList {
            for (int row = 0; row < shelves->rowCount(); ++row) {
                const QModelIndex idx = shelves->index(row, 0);
                if (shelves->data(idx, ShelfModel::ShelfTypeRole).toString() ==
                    QLatin1String("playlistNextUp"))
                    return shelves->data(idx, ShelfModel::ItemsRole).toList();
            }
            return {};
        };

        backend.setNextUpPlaylist(QStringLiteral("pl-unread"), true);
        backend.setNextUpPlaylist(QStringLiteral("pl-reading"), true);
        backend.setNextUpPlaylist(QStringLiteral("pl-done"), true);
        // Picked, but the setting itself is off: no shelf.
        QVERIFY(nextUp().isEmpty());

        backend.setPlaylistNextUpEnabled(true);
        QVariantList cards = nextUp();
        QCOMPARE(cards.size(), 2);
        QCOMPARE(cards.at(0).toMap().value(QStringLiteral("itemId")).toString(),
                 QStringLiteral("u2"));
        QCOMPARE(cards.at(0).toMap().value(QStringLiteral("author")).toString(),
                 QStringLiteral("Unread"));
        QCOMPARE(cards.at(1).toMap().value(QStringLiteral("itemId")).toString(),
                 QStringLiteral("r3"));

        // Finishing a book moves its playlist on.
        progress.set(QStringLiteral("u2"), 1.0, 3600.0, true);
        QCOMPARE(nextUp().at(0).toMap().value(QStringLiteral("itemId")).toString(),
                 QStringLiteral("u3"));

        backend.setNextUpPlaylist(QStringLiteral("pl-unread"), false);
        cards = nextUp();
        QCOMPARE(cards.size(), 1);
        QCOMPARE(cards.at(0).toMap().value(QStringLiteral("itemId")).toString(),
                 QStringLiteral("r3"));

        backend.setPlaylistNextUpEnabled(false);
        QVERIFY(nextUp().isEmpty());
    }
};

QTEST_GUILESS_MAIN(TstBackend)
#include "tst_backend.moc"
