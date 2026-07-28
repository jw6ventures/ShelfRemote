#pragma once

#include <QCryptographicHash>
#include <QString>
#include <QUrl>

// A saved Audiobookshelf server the user can connect to. `id` is a stable key
// derived from the normalised base URL so tokens/secrets survive renames.
struct ServerProfile {
    QString id;
    QString name;
    QUrl    baseUrl;
    QString lastUserId;

    static QString idForUrl(const QUrl &url)
    {
        return QString::fromLatin1(
            QCryptographicHash::hash(url.toString(QUrl::StripTrailingSlash).toUtf8(),
                                     QCryptographicHash::Sha1)
                .toHex());
    }

    bool isValid() const { return baseUrl.isValid() && !baseUrl.host().isEmpty(); }
};
