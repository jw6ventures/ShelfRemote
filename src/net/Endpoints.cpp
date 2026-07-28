#include "net/Endpoints.h"

Endpoints::Endpoints(const QUrl &base)
{
    setBase(base);
}

void Endpoints::setBase(const QUrl &base)
{
    m_base = base;
    // Normalise to guarantee a trailing-slash-free path we can append to.
    QString path = m_base.path();
    while (path.endsWith(QLatin1Char('/')))
        path.chop(1);
    m_base.setPath(path);
    m_base.setQuery(QString());
    m_base.setFragment(QString());
}

QUrl Endpoints::build(const QString &path, const QUrlQuery &query) const
{
    QUrl url = m_base;
    // path is an absolute API path with a leading slash. Prepend the base's
    // sub-path so a server hosted under /audiobookshelf keeps that prefix.
    url.setPath(m_base.path() + path);
    if (!query.isEmpty())
        url.setQuery(query);
    return url;
}

QUrl Endpoints::resolveContentUrl(const QString &contentUrl) const
{
    const QUrl u(contentUrl);
    if (u.isRelative())
        return build(contentUrl.startsWith(QLatin1Char('/')) ? contentUrl
                                                             : QLatin1Char('/') + contentUrl);
    return u;
}
