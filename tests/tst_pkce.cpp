#include <QTest>

#include "auth/Pkce.h"

class TstPkce : public QObject
{
    Q_OBJECT
private slots:
    void challengeMatchesRfcVector()
    {
        // RFC 7636 Appendix B worked example.
        const QString verifier = QStringLiteral("dBjftJeZ4CVP-mB92K27uhbUJU1p1r_wW1gFWFOEjXk");
        const QString expected = QStringLiteral("E9Melhoa2OwvFrEMTJguCHaoeK1t8URWbuGJSstw-cM");
        QCOMPARE(Pkce::challengeFor(verifier), expected);
    }

    void generatedFieldsArePopulatedAndDistinct()
    {
        const Pkce a = Pkce::generate();
        QVERIFY(!a.state.isEmpty());
        QVERIFY(!a.codeVerifier.isEmpty());
        QVERIFY(!a.codeChallenge.isEmpty());
        // verifier length within RFC 43..128
        QVERIFY(a.codeVerifier.size() >= 43);
        QVERIFY(a.codeVerifier.size() <= 128);
        // challenge is deterministic from verifier
        QCOMPARE(a.codeChallenge, Pkce::challengeFor(a.codeVerifier));

        const Pkce b = Pkce::generate();
        QVERIFY(a.codeVerifier != b.codeVerifier);
        QVERIFY(a.state != b.state);
    }

    void base64UrlHasNoPaddingOrUnsafeChars()
    {
        const Pkce a = Pkce::generate();
        QVERIFY(!a.codeChallenge.contains('='));
        QVERIFY(!a.codeChallenge.contains('+'));
        QVERIFY(!a.codeChallenge.contains('/'));
    }
};

QTEST_MAIN(TstPkce)
#include "tst_pkce.moc"
