#include "auth/Pkce.h"

#include <QCryptographicHash>
#include <QRandomGenerator>

QString Pkce::base64Url(const QByteArray &raw)
{
    return QString::fromLatin1(
        raw.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
}

QString Pkce::randomToken(int bytes)
{
    QByteArray buf(bytes, Qt::Uninitialized);
    QRandomGenerator::system()->fillRange(
        reinterpret_cast<quint32 *>(buf.data()), bytes / sizeof(quint32));
    return base64Url(buf);
}

QString Pkce::challengeFor(const QString &verifier)
{
    const QByteArray digest =
        QCryptographicHash::hash(verifier.toUtf8(), QCryptographicHash::Sha256);
    return base64Url(digest);
}

Pkce Pkce::generate()
{
    Pkce p;
    p.state = randomToken(16);
    p.codeVerifier = randomToken(48); // 64 base64url chars, within RFC 43..128
    p.codeChallenge = challengeFor(p.codeVerifier);
    return p;
}
