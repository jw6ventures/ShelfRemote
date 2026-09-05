#pragma once

#include <QByteArray>
#include <QString>

// PKCE (RFC 7636) + state generation for the OIDC Authorization Code flow.
struct Pkce {
    QString state;         // opaque CSRF token, echoed back on callback
    QString codeVerifier;  // high-entropy random string
    QString codeChallenge; // BASE64URL(SHA256(codeVerifier))

    static Pkce generate();

    // Exposed for unit testing the transform independently of randomness.
    static QString challengeFor(const QString &verifier);
    // base64url of `bytes` fully random bytes. Exposed for the same reason.
    static QString randomToken(int bytes);

private:
    static QString base64Url(const QByteArray &raw);
};
