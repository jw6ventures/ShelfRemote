#include <QTest>

#include <QSet>

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

    void randomTokenIsWellFormedAtAnyByteCount()
    {
        // The generator fills whole 32-bit words, so lengths that are not a
        // multiple of 4 are the interesting ones: cover every residue class.
        //
        // Note this pins the shape of the output, not the seeding of its last
        // bytes. Reading uninitialised memory is undefined behaviour and can look
        // perfectly random, so the tail-fill itself is not observable from here —
        // it is enforced by construction in randomToken(), not by this test.
        for (int bytes : {1, 13, 14, 15, 16, 48}) {
            QSet<QString> seen;
            for (int i = 0; i < 32; ++i) {
                const QString token = Pkce::randomToken(bytes);
                // base64url without padding: 4 characters per 3 bytes, rounded up.
                QCOMPARE(token.size(), (bytes * 4 + 2) / 3);
                QVERIFY(!token.contains('='));
                QVERIFY(!token.contains('+'));
                QVERIFY(!token.contains('/'));
                seen.insert(token);
            }
            if (bytes > 4)
                QCOMPARE(seen.size(), 32); // the seeded words alone must vary
        }
        QVERIFY(Pkce::randomToken(0).isEmpty());
    }

    void base64UrlHasNoPaddingOrUnsafeChars()
    {
        const Pkce a = Pkce::generate();
        QVERIFY(!a.codeChallenge.contains('='));
        QVERIFY(!a.codeChallenge.contains('+'));
        QVERIFY(!a.codeChallenge.contains('/'));
    }
};

QTEST_GUILESS_MAIN(TstPkce)
#include "tst_pkce.moc"
