#include "app/Application.h"
#include "app/AppConfig.h"

#include <QDebug>
#include <QElapsedTimer>
#include <QLocalServer>
#include <QLocalSocket>
#include <QThread>
#include <QtEndian>

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <memory>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {
// Callback URIs are short; cap a frame so a garbage/hostile length prefix can
// never make us buffer unboundedly.
constexpr int kMaxFrameBytes = 64 * 1024;
constexpr char kFrameAccepted = 0x06;

// Frame = 4-byte big-endian length + that many UTF-8 bytes. Framing is required
// because a stream socket does not preserve message boundaries: a single write
// may arrive split across several readyRead signals (or several writes coalesced
// into one), so the reader cannot assume one readyRead is exactly one URI.
bool writeFrame(QLocalSocket &sock, const QString &uri)
{
    const QByteArray payload = uri.toUtf8();
    QByteArray frame;
    frame.resize(sizeof(quint32));
    qToBigEndian<quint32>(quint32(payload.size()),
                          reinterpret_cast<uchar *>(frame.data()));
    frame.append(payload);
    if (sock.write(frame) != frame.size())
        return false;
    sock.flush();
    if (sock.bytesToWrite() > 0 && !sock.waitForBytesWritten(1000))
        return false;

    // Do not let the redirect launcher exit until the primary confirms that it
    // parsed and dispatched the callback. This prevents a successful socket write
    // from being mistaken for successful delivery if the primary disconnects.
    if (sock.bytesAvailable() == 0 && !sock.waitForReadyRead(2000))
        return false;
    return sock.readAll().contains(kFrameAccepted);
}

bool forwardToPrimary(const QString &socketName, const QString &callbackArg)
{
    // The lock owner may be between acquiring the lock and entering listen().
    // Retry briefly so a simultaneous desktop launch does not lose its callback.
    QElapsedTimer timeout;
    timeout.start();
    while (timeout.elapsed() < 3000) {
        QLocalSocket probe;
        probe.connectToServer(socketName);
        if (probe.waitForConnected(200)) {
            if (!callbackArg.isEmpty() && !writeFrame(probe, callbackArg))
                qWarning() << "Primary instance did not acknowledge callback delivery";
            probe.disconnectFromServer();
            if (probe.state() != QLocalSocket::UnconnectedState)
                probe.waitForDisconnected(200);
            return true;
        }
        QThread::msleep(25);
    }
    return false;
}
} // namespace

SingleInstanceGuard::SingleInstanceGuard(QObject *parent)
    : SingleInstanceGuard(AppConfig::instanceSocketPath(), parent)
{
}

SingleInstanceGuard::SingleInstanceGuard(const QString &socketPath, QObject *parent)
    : QObject(parent)
    , m_socketName(socketPath)
{
}

SingleInstanceGuard::~SingleInstanceGuard()
{
    if (m_lockFd >= 0) {
        ::flock(m_lockFd, LOCK_UN);
        ::close(m_lockFd);
        m_lockFd = -1;
    }
}

bool SingleInstanceGuard::startListening()
{
    m_server.reset(new QLocalServer(this));
    m_server->setSocketOptions(QLocalServer::UserAccessOption);
    if (!m_server->listen(m_socketName)) {
        qDebug() << "Single-instance listen failed for" << m_socketName
                 << m_server->errorString();
        m_server.reset();
        return false;
    }
    qDebug() << "Single-instance listening on" << m_server->fullServerName();

    connect(m_server.data(), &QLocalServer::newConnection, this, [this]() {
        while (QLocalSocket *conn = m_server->nextPendingConnection()) {
            // Per-connection reassembly buffer: a forwarded URI may span multiple
            // readyRead signals, so accumulate until a whole frame is present.
            auto buffer = std::make_shared<QByteArray>();
            connect(conn, &QLocalSocket::readyRead, this, [this, conn, buffer]() {
                buffer->append(conn->readAll());
                // Drain every complete frame currently buffered.
                while (buffer->size() >= int(sizeof(quint32))) {
                    const quint32 len = qFromBigEndian<quint32>(
                        reinterpret_cast<const uchar *>(buffer->constData()));
                    if (len == 0 || len > kMaxFrameBytes) {
                        conn->abort(); // malformed/oversized length: drop it
                        return;
                    }
                    if (buffer->size() < int(sizeof(quint32) + len))
                        break; // rest of this frame has not arrived yet
                    const QByteArray payload = buffer->mid(sizeof(quint32), int(len));
                    buffer->remove(0, int(sizeof(quint32) + len));
                    const QString uri = QString::fromUtf8(payload).trimmed();
                    if (!uri.isEmpty()) {
                        emit uriReceived(uri);
                        conn->write(&kFrameAccepted, 1);
                        conn->flush();
                    }
                }
            });
            connect(conn, &QLocalSocket::disconnected, conn, &QLocalSocket::deleteLater);
        }
    });
    return true;
}

bool SingleInstanceGuard::acquire(const QString &callbackArg)
{
    // QLocalServer's filesystem socket alone is not an ownership primitive: a
    // second listen can replace its path on Unix. An adjacent kernel file lock
    // provides atomic ownership across Flatpak PID namespaces and is released
    // automatically if the owner crashes or is killed.
    const QByteArray lockPath = (m_socketName + QStringLiteral(".lock")).toLocal8Bit();
    m_lockFd = ::open(lockPath.constData(), O_CREAT | O_RDWR | O_CLOEXEC,
                      S_IRUSR | S_IWUSR);
    if (m_lockFd < 0) {
        qWarning() << "Could not open single-instance lock file:" << strerror(errno);
        if (forwardToPrimary(m_socketName, callbackArg))
            return false;
        qWarning() << "Running without single-instance ownership";
        return true;
    }
    if (::flock(m_lockFd, LOCK_EX | LOCK_NB) != 0) {
        const int lockError = errno;
        ::close(m_lockFd);
        m_lockFd = -1;
        if (lockError != EWOULDBLOCK && lockError != EAGAIN)
            qWarning() << "Could not acquire single-instance lock:" << strerror(lockError);
        if (!forwardToPrimary(m_socketName, callbackArg))
            qWarning() << "Primary instance owns the lock but its callback socket is unavailable";
        return false;
    }

    // We alone own the endpoint. Remove a socket left behind by a crashed prior
    // owner, then bind the primary listener.
    QLocalServer::removeServer(m_socketName);
    if (startListening())
        return true;

    // Keep the ownership lock even in degraded mode so later launches cannot
    // replace this live instance with another primary.
    qWarning() << "Single-instance callback socket could not be created; running without URI IPC";
    return true;
}
