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
    if (bytes <= 0)
        return {};
    // fillRange works in whole quint32s, so a length that is not a multiple of 4
    // would leave the tail of the buffer uninitialised and carry that unseeded
    // memory straight into a state value or code verifier. Generate whole words
    // and trim, so any length is fully random.
    const qsizetype words = (bytes + int(sizeof(quint32)) - 1) / int(sizeof(quint32));
    QByteArray buf(words * qsizetype(sizeof(quint32)), Qt::Uninitialized);
    QRandomGenerator::system()->fillRange(
        reinterpret_cast<quint32 *>(buf.data()), words);
    buf.truncate(bytes);
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
