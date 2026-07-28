#include "app/UriHandler.h"
#include "app/AppConfig.h"

#include <QUrl>
#include <QUrlQuery>

UriHandler::UriHandler(QObject *parent)
    : QObject(parent)
{
}

bool UriHandler::handle(const QString &uri)
{
    const QUrl url(uri.trimmed());
    if (url.scheme() != AppConfig::uriScheme())
        return false;

    // jw6-shelfremote://oauth?code=...&state=...  (host == "oauth")
    if (url.host() == QStringLiteral("oauth")) {
        const QUrlQuery q(url);
        emit oauthCallback(q.queryItemValue(QStringLiteral("code")),
                           q.queryItemValue(QStringLiteral("state")),
                           q.queryItemValue(QStringLiteral("error")));
        return true;
    }
    return false;
}
