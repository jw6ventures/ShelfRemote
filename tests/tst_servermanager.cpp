#include <QTest>

#include "server/ServerManager.h"

// Pins the version-gated capability layer: features with a known minimum server
// version are hidden below it and exposed at/above it, while unknown features (or
// an unknown server version) stay available so we never hide functionality on a
// server we simply have not classified.
class TstServerManager : public QObject
{
    Q_OBJECT

private slots:
    void unknownVersionAssumesAvailable()
    {
        ServerManager mgr; // no version set yet
        QVERIFY(mgr.hasCapability(QStringLiteral("openidLogoutUrl")));
    }

    void gatesBelowMinimumVersion()
    {
        ServerManager mgr;
        mgr.setServerVersion(QStringLiteral("2.2.22")); // one patch below 2.2.23
        QVERIFY(!mgr.hasCapability(QStringLiteral("openidLogoutUrl")));
        mgr.setServerVersion(QStringLiteral("2.1.99"));
        QVERIFY(!mgr.hasCapability(QStringLiteral("openidLogoutUrl")));
        mgr.setServerVersion(QStringLiteral("1.9.9"));
        QVERIFY(!mgr.hasCapability(QStringLiteral("openidLogoutUrl")));
    }

    void allowsAtOrAboveMinimumVersion()
    {
        ServerManager mgr;
        mgr.setServerVersion(QStringLiteral("2.2.23")); // exactly the minimum
        QVERIFY(mgr.hasCapability(QStringLiteral("openidLogoutUrl")));
        mgr.setServerVersion(QStringLiteral("2.3.0"));
        QVERIFY(mgr.hasCapability(QStringLiteral("openidLogoutUrl")));
        mgr.setServerVersion(QStringLiteral("10.0.0"));
        QVERIFY(mgr.hasCapability(QStringLiteral("openidLogoutUrl")));
    }

    void ignoresPreReleaseSuffix()
    {
        ServerManager mgr;
        mgr.setServerVersion(QStringLiteral("2.3.0-beta1")); // parses as 2.3.0
        QVERIFY(mgr.hasCapability(QStringLiteral("openidLogoutUrl")));
    }

    void unknownFeatureAlwaysAvailable()
    {
        ServerManager mgr;
        mgr.setServerVersion(QStringLiteral("1.0.0"));
        QVERIFY(mgr.hasCapability(QStringLiteral("someFeatureWeHaveNotClassified")));
    }
};

QTEST_GUILESS_MAIN(TstServerManager)
#include "tst_servermanager.moc"
