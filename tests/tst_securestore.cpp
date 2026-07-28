#include <QTest>

#include <QFile>
#include <QStandardPaths>

#include "app/AppConfig.h"
#include "storage/Database.h"
#include "storage/SecureStore.h"

// Pins the AAD-bound secret format and the local ("local-v1") provider path. Portal
// integration needs a live xdg-desktop-portal; these deterministic unit tests
// explicitly select local even when the test runner itself is sandboxed.
class TstSecureStore : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true); // isolate data dir + DB
        QVERIFY(Database::instance().open());
        // Keep the encryption tests independent of whether their runner itself is
        // a Flatpak: explicitly select local, then generate a fresh key on store.
        Database::instance().putSetting(QStringLiteral("secretProvider"),
                                        QStringLiteral("local-v1"));
        Database::instance().removeSetting(QStringLiteral("secretPortalToken"));
        QFile::remove(AppConfig::dataDir() + QStringLiteral("/master.key"));
    }

    void roundTripsWithMatchingContext()
    {
        SecureStore s;
        const SecretContext ctx{QStringLiteral("srv1"), QStringLiteral("acct1"),
                                QStringLiteral("tokens"), 1};
        const QByteArray secret = QByteArrayLiteral("{\"access\":\"abc\",\"refresh\":\"xyz\"}");
        s.store(QStringLiteral("srv1/tokens"), secret, ctx);

        SecureStore::RetrieveStatus st = SecureStore::RetrieveStatus::Missing;
        const QByteArray got = s.retrieve(QStringLiteral("srv1/tokens"), ctx, &st);
        QVERIFY(st == SecureStore::RetrieveStatus::Ok);
        QCOMPARE(got, secret);

        // The provider was selected once and made sticky as local.
        QCOMPARE(Database::instance().getSetting(QStringLiteral("secretProvider")),
                 QStringLiteral("local-v1"));
    }

    void wrongContextFailsToDecrypt()
    {
        SecureStore s;
        const SecretContext ctx{QStringLiteral("srv1"), QStringLiteral("acct1"),
                                QStringLiteral("tokens"), 1};
        s.store(QStringLiteral("k"), QByteArrayLiteral("payload"), ctx);

        SecureStore::RetrieveStatus st = SecureStore::RetrieveStatus::Ok;
        // Different account => different AAD => authentication tag fails.
        QVERIFY(s.retrieve(QStringLiteral("k"),
                           SecretContext{QStringLiteral("srv1"), QStringLiteral("OTHER"),
                                         QStringLiteral("tokens"), 1}, &st).isEmpty());
        QVERIFY(st == SecureStore::RetrieveStatus::Undecryptable);

        // Different schema version likewise binds differently.
        QVERIFY(s.retrieve(QStringLiteral("k"),
                           SecretContext{QStringLiteral("srv1"), QStringLiteral("acct1"),
                                         QStringLiteral("tokens"), 2}, &st).isEmpty());
        QVERIFY(st == SecureStore::RetrieveStatus::Undecryptable);
    }

    void unmarkedLegacyLocalKeyIsMigrated()
    {
        // Releases predating the provider marker already wrote master.key. Even
        // when the upgraded app runs under Flatpak, it must keep using that key
        // instead of switching to the portal and stranding the saved tokens.
        Database::instance().removeSetting(QStringLiteral("secretProvider"));
        qputenv("FLATPAK_ID", QByteArrayLiteral("us.jw6.ShelfRemote"));

        SecureStore migrated;
        SecureStore::RetrieveStatus st = SecureStore::RetrieveStatus::Missing;
        const SecretContext ctx{QStringLiteral("srv1"), QStringLiteral("acct1"),
                                QStringLiteral("tokens"), 1};
        QCOMPARE(migrated.retrieve(QStringLiteral("srv1/tokens"), ctx, &st),
                 QByteArrayLiteral("{\"access\":\"abc\",\"refresh\":\"xyz\"}"));
        QVERIFY(st == SecureStore::RetrieveStatus::Ok);
        QCOMPARE(Database::instance().getSetting(QStringLiteral("secretProvider")),
                 QStringLiteral("local-v1"));

        qunsetenv("FLATPAK_ID");
    }

    void missingKeyReportsMissing()
    {
        // A missing credential should not select or contact a provider at all.
        Database::instance().removeSetting(QStringLiteral("secretProvider"));
        qputenv("FLATPAK_ID", QByteArrayLiteral("us.jw6.ShelfRemote"));

        SecureStore s;
        SecureStore::RetrieveStatus st = SecureStore::RetrieveStatus::Ok;
        QVERIFY(s.retrieve(QStringLiteral("no-such-key"),
                           SecretContext{QStringLiteral("s"), QStringLiteral("a"),
                                         QStringLiteral("tokens"), 1}, &st).isEmpty());
        QVERIFY(st == SecureStore::RetrieveStatus::Missing);
        QVERIFY(Database::instance().getSetting(QStringLiteral("secretProvider")).isEmpty());

        qunsetenv("FLATPAK_ID");
        Database::instance().putSetting(QStringLiteral("secretProvider"),
                                        QStringLiteral("local-v1"));
    }

    void legacyBlobIsRejected()
    {
        SecureStore s;
        const SecretContext ctx{QStringLiteral("srv1"), QStringLiteral("acct1"),
                                QStringLiteral("tokens"), 1};
        // A pre-AAD (0x01) blob of plausible length must be treated as undecryptable
        // so the caller forces a one-time re-login.
        QByteArray legacy;
        legacy.append(char(0x01));
        legacy.append(QByteArray(12 + 16 + 8, 'x'));
        Database::instance().putSecret(QStringLiteral("legacy"), legacy);

        SecureStore::RetrieveStatus st = SecureStore::RetrieveStatus::Ok;
        QVERIFY(s.retrieve(QStringLiteral("legacy"), ctx, &st).isEmpty());
        QVERIFY(st == SecureStore::RetrieveStatus::Undecryptable);
    }
};

QTEST_GUILESS_MAIN(TstSecureStore)
#include "tst_securestore.moc"
