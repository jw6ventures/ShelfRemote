#pragma once

#include <QObject>
#include <QString>

// Parses and routes incoming jw6-shelfremote:// callback URIs. A single instance
// lives in the running process; the second-launch process forwards its URI here
// via the single-instance QLocalServer channel (see Application).
class UriHandler : public QObject
{
    Q_OBJECT
public:
    explicit UriHandler(QObject *parent = nullptr);

    // Dispatches a raw callback URI. Returns true if it was a recognised scheme.
    bool handle(const QString &uri);

signals:
    // Emitted for jw6-shelfremote://oauth?code=...&state=... callbacks.
    void oauthCallback(const QString &code, const QString &state, const QString &error);
};
