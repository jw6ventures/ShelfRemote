#include <QTest>

#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTcpSocket>

#include "app/UriHandler.h"
#include "auth/AuthManager.h"
#include "auth/TokenStore.h"
#include "net/ApiClient.h"
#include "server/ServerProfile.h"
#include "storage/Database.h"
#include "storage/SecureStore.h"

namespace {

// An Audiobookshelf stand-in that carries a local login through to authenticated,
// then goes silent on /logout — the case that matters here, because a server that
// never answers is exactly when a user reaches for Sign out.
class FakeServer : public QObject
{
    Q_OBJECT
public:
    bool sawLogout = false;

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
            connect(sock, &QTcpSocket::readyRead, this, [this, sock]() {
                const QByteArray chunk = sock->readAll();
                const QByteArray requestLine = chunk.left(chunk.indexOf('\r'));
                if (requestLine.isEmpty())
                    return;
                if (requestLine.contains("/logout")) {
                    sawLogout = true;
                    return; // deliberately never answered
                }
                QJsonObject payload;
                if (requestLine.contains("/status")) {
                    payload = QJsonObject{
                        {QStringLiteral("serverVersion"), QStringLiteral("2.17.0")},
                        {QStringLiteral("authMethods"), QJsonArray{QStringLiteral("local")}}};
                } else if (requestLine.contains("/login")) {
                    payload = QJsonObject{
                        {QStringLiteral("user"), QJsonObject{
                            {QStringLiteral("id"), QStringLiteral("user-1")}}},
                        {QStringLiteral("accessToken"), QStringLiteral("access-token")},
                        {QStringLiteral("refreshToken"), QStringLiteral("refresh-token")}};
                } else if (requestLine.contains("/api/authorize")) {
                    payload = QJsonObject{
                        {QStringLiteral("user"), QJsonObject{
                            {QStringLiteral("id"), QStringLiteral("user-1")},
                            {QStringLiteral("username"), QStringLiteral("someone")}}}};
                }
                const QByteArray json = QJsonDocument(payload).toJson(QJsonDocument::Compact);
                sock->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n"
                            "Content-Length: " + QByteArray::number(json.size())
                            + "\r\n\r\n" + json);
                sock->flush();
            });
            connect(sock, &QTcpSocket::disconnected, sock, &QTcpSocket::deleteLater);
        }
    }

    QTcpServer m_server;
};

} // namespace

class TstAuthManager : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        qunsetenv("DBUS_SESSION_BUS_ADDRESS"); // keep the key provider local
        QVERIFY(Database::instance().open());
        Database::instance().putSetting(QStringLiteral("secretProvider"),
                                        QStringLiteral("local-v1"));
    }

    void signOutTakesEffectWithoutWaitingForTheServer()
    {
        FakeServer server;
        QVERIFY(server.start());

        ApiClient api;
        SecureStore secure;
        TokenStore tokens(&api, &secure);
        UriHandler uris;
        AuthManager auth(&api, &tokens, &uris);

        auth.checkServer(server.baseUrl());
        QTRY_VERIFY_WITH_TIMEOUT(auth.needsLogin(), 5000);

        auth.loginLocal(QStringLiteral("someone"), QStringLiteral("secret"));
        QTRY_VERIFY_WITH_TIMEOUT(auth.isAuthenticated(), 5000);

        QSignalSpy ending(&auth, &AuthManager::sessionEnding);
        auth.logout();

        // The /logout request is still unanswered, and will stay that way. The
        // local session must already be over: sessionEnding() has emptied the
        // content models, so anything short of this leaves the user on a hollow
        // shell that still claims to be signed in.
        QCOMPARE(ending.count(), 1);
        QVERIFY(!auth.isAuthenticated());
        QVERIFY(auth.needsLogin());
        QVERIFY(!tokens.hasTokens());
        QVERIFY(api.accessToken().isEmpty());

        // The request did go out; it simply is not what the UI waits on.
        QTRY_VERIFY_WITH_TIMEOUT(server.sawLogout, 5000);

        // And it stays signed out while that request hangs.
        QTest::qWait(500);
        QVERIFY(!auth.isAuthenticated());
    }

    // Signs in for real so the tokens are saved under the local key, and records
    // the server the way the app does, so a later restore has something to read.
    static void signInAndSave(FakeServer &server)
    {
        ApiClient api;
        SecureStore secure;
        TokenStore tokens(&api, &secure);
        UriHandler uris;
        AuthManager auth(&api, &tokens, &uris);
        auth.checkServer(server.baseUrl());
        QTRY_VERIFY_WITH_TIMEOUT(auth.needsLogin(), 5000);
        auth.loginLocal(QStringLiteral("someone"), QStringLiteral("secret"));
        QTRY_VERIFY_WITH_TIMEOUT(auth.isAuthenticated(), 5000);

        Database::ServerRow row;
        row.id = ServerProfile::idForUrl(server.baseUrl());
        row.name = QStringLiteral("test");
        row.baseUrl = server.baseUrl().toString();
        row.lastUserId = QStringLiteral("user-1");
        Database::instance().upsertServer(row);
    }

    // Stands in for the system keyring being down: with the portal provider chosen
    // and no session bus, the master secret cannot be acquired.
    static void setKeyringAvailable(bool available)
    {
        Database::instance().putSetting(QStringLiteral("secretProvider"),
                                        available ? QStringLiteral("local-v1")
                                                  : QStringLiteral("portal-v1"));
    }

    // Right after boot the keyring is often not up yet. That used to drop the user
    // on the login screen with a perfectly good saved session; now the restore
    // waits, and signs in by itself once the keyring answers.
    void aKeyringThatComesUpLateStillRestoresTheSession()
    {
        FakeServer server;
        QVERIFY(server.start());
        signInAndSave(server);

        ApiClient api;
        SecureStore secure; // fresh: nothing cached from the sign-in above
        TokenStore tokens(&api, &secure);
        UriHandler uris;
        AuthManager auth(&api, &tokens, &uris);

        setKeyringAvailable(false);
        QVERIFY(auth.restoreSession(server.baseUrl(), ServerProfile::idForUrl(server.baseUrl())));
        QVERIFY(auth.isBusy());
        QVERIFY(!auth.needsLogin());
        QVERIFY(!auth.notice().isEmpty());
        QVERIFY(auth.lastError().isEmpty());

        setKeyringAvailable(true);
        QTRY_VERIFY_WITH_TIMEOUT(auth.isAuthenticated(), 10000);
        QVERIFY(auth.notice().isEmpty());
    }

    // Cancel stops the waiting for good; a retry must not sign in behind the
    // user's back afterwards.
    void cancellingStopsWaitingForTheKeyring()
    {
        FakeServer server;
        QVERIFY(server.start());
        signInAndSave(server);

        ApiClient api;
        SecureStore secure;
        TokenStore tokens(&api, &secure);
        UriHandler uris;
        AuthManager auth(&api, &tokens, &uris);

        setKeyringAvailable(false);
        QVERIFY(auth.restoreSession(server.baseUrl(), ServerProfile::idForUrl(server.baseUrl())));
        QVERIFY(auth.isBusy());

        auth.cancelAuth();
        QVERIFY(auth.needsLogin());
        QVERIFY(auth.notice().isEmpty());

        setKeyringAvailable(true);
        QTest::qWait(3000); // past the first retry
        QVERIFY(auth.needsLogin());
    }
};

QTEST_GUILESS_MAIN(TstAuthManager)
#include "tst_authmanager.moc"
