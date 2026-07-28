#pragma once

#include <QObject>
#include <QScopedPointer>
#include <QString>

class QLocalServer;
class UriHandler;

// Owns the single-instance guard. The FIRST process to start binds a named
// QLocalServer; later processes launched by the desktop for a URI callback
// connect to that socket, forward their %u argument, and exit.
class SingleInstanceGuard : public QObject
{
    Q_OBJECT
public:
    explicit SingleInstanceGuard(QObject *parent = nullptr);
    // Explicit socket path for isolated tests.
    SingleInstanceGuard(const QString &socketPath, QObject *parent);
    ~SingleInstanceGuard() override;

    // Returns true if this process is the primary instance (server bound).
    // If false, the callbackArg (if any) has already been forwarded to the
    // primary and the caller should exit.
    bool acquire(const QString &callbackArg);

signals:
    // Emitted in the primary process when a secondary forwards a URI.
    void uriReceived(const QString &uri);

private:
    // Binds QLocalServer after acquire() has won the adjacent kernel file lock.
    bool startListening();

    QScopedPointer<QLocalServer> m_server;
    QString m_socketName;
    int m_lockFd = -1;
};
