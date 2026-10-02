#include <QTest>

#include <QHostAddress>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTcpSocket>

#include "auth/TokenStore.h"
#include "net/ApiClient.h"
#include "storage/Database.h"
#include "storage/SecureStore.h"

namespace {

// Answers every request with a fixed status, or (status 0) drops the connection
// without a word, as a stalled network does.
class FakeServer : public QObject
{
    Q_OBJECT
public:
    int status = 200;

    bool start()
    {
        if (!m_server.listen(QHostAddress::LocalHost))
            return false;
        connect(&m_server, &QTcpServer::newConnection, this, [this]() {
            while (QTcpSocket *sock = m_server.nextPendingConnection()) {
                connect(sock, &QTcpSocket::readyRead, this, [this, sock]() {
                    sock->readAll();
                    if (status == 0) {
                        sock->abort();
                        return;
                    }
                    sock->write("HTTP/1.1 " + QByteArray::number(status)
                                + " X\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
                    sock->flush();
                    sock->disconnectFromHost();
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
    QTcpServer m_server;
};

} // namespace

// A failed refresh sends the user to the login screen, so it must only be
// reported when the server actually refused the refresh token.
class TstTokenStore : public QObject
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

    void refreshOutcome_data()
    {
        QTest::addColumn<int>("status");
        QTest::addColumn<bool>("sessionOver");
        QTest::newRow("refused (401)") << 401 << true;
        QTest::newRow("refused (403)") << 403 << true;
        QTest::newRow("bad request (400)") << 400 << true;
        QTest::newRow("server fault (503)") << 503 << false;
        QTest::newRow("connection dropped") << 0 << false;
    }

    void refreshOutcome()
    {
        QFETCH(int, status);
        QFETCH(bool, sessionOver);

        FakeServer server;
        server.status = status;
        QVERIFY(server.start());

        ApiClient api;
        api.setBaseUrl(server.baseUrl());
        SecureStore secure;
        TokenStore tokens(&api, &secure);
        tokens.setServerKey(QStringLiteral("tst-tokenstore"));
        tokens.setAccount(QStringLiteral("user-1"));
        tokens.setTokens(QStringLiteral("access"), QStringLiteral("refresh"));

        QSignalSpy failed(&tokens, &TokenStore::refreshFailed);
        bool settled = false;
        bool ok = true;
        tokens.refresh([&](bool result) { settled = true; ok = result; });
        QTRY_VERIFY_WITH_TIMEOUT(settled, 5000);

        QVERIFY(!ok);
        QCOMPARE(failed.count(), sessionOver ? 1 : 0);
        // Either way the stored tokens are left for the next attempt or a re-login
        // to replace; a transient failure must not discard them.
        QVERIFY(tokens.hasTokens());
    }

    // The case the setting exists for: the keyring is locked, so signing in with
    // a password works but can't be saved, and the next boot asks again. Moving
    // sign-ins to this device must save the session in use right then.
    void movingToTheDeviceSavesTheCurrentSession()
    {
        Database::instance().putSetting(QStringLiteral("secretProvider"),
                                        QStringLiteral("portal-v1"));
        ApiClient api;
        SecureStore secure;
        TokenStore tokens(&api, &secure);
        tokens.setServerKey(QStringLiteral("tst-tokenstore-move"));
        tokens.setAccount(QStringLiteral("user-1"));
        tokens.setTokens(QStringLiteral("access"), QStringLiteral("refresh"));
        if (Database::instance().getSecret(QStringLiteral("tst-tokenstore-move/tokens")).size() > 0)
            QSKIP("A Secret portal answered in this environment");

        QVERIFY(tokens.setStorage(SecureStore::Storage::Device)
                == SecureStore::SwitchResult::Reset);

        SecureStore nextLaunch;
        TokenStore restored(&api, &nextLaunch);
        restored.setServerKey(QStringLiteral("tst-tokenstore-move"));
        restored.setAccount(QStringLiteral("user-1"));
        QVERIFY(restored.load());
        QCOMPARE(restored.refreshToken(), QStringLiteral("refresh"));

        Database::instance().putSetting(QStringLiteral("secretProvider"),
                                        QStringLiteral("local-v1"));
    }
};

QTEST_GUILESS_MAIN(TstTokenStore)
#include "tst_tokenstore.moc"
