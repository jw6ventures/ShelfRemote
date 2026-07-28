#include "player/MprisAdapter.h"
#include "app/AppConfig.h"
#include "player/PlaybackSession.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusObjectPath>

namespace {
// The single source of truth for the current MPRIS track object path, so
// Metadata (mpris:trackid) and SetPosition compare the exact same value.
QString trackObjectPath(PlaybackSession *session)
{
    return QStringLiteral("/us/jw6/ShelfRemote/track/") +
           (session->itemId().isEmpty() ? QStringLiteral("none") : session->itemId());
}
} // namespace

// ---------------------------------------------------------------------------
MprisAdapter::MprisAdapter(PlaybackSession *session, QObject *parent)
    : QObject(parent)
    , m_session(session)
{
    new MprisRootAdaptor(this);
    m_player = new MprisPlayerAdaptor(this, session);

    // Push property-change notifications when the session state moves. Always
    // report the adaptor's computed status (Playing/Paused/Stopped) rather than a
    // local guess, so a stopped session is not announced as merely Paused.
    connect(session, &PlaybackSession::playingChanged, this, [this](bool) {
        notify({{QStringLiteral("PlaybackStatus"), m_player->playbackStatus()}});
    });
    // active toggles between Stopped and Playing/Paused and swaps the track, so the
    // status and metadata both change with it.
    connect(session, &PlaybackSession::activeChanged, this, [this]() {
        notify({{QStringLiteral("PlaybackStatus"), m_player->playbackStatus()},
                {QStringLiteral("Metadata"), m_player->metadata()}});
    });
    connect(session, &PlaybackSession::metadataChanged, this, [this]() {
        // Ship the actual metadata map (title/artist/length) and the current
        // status; an invalid QVariant here would be stripped and notify nothing.
        notify({{QStringLiteral("Metadata"), m_player->metadata()},
                {QStringLiteral("PlaybackStatus"), m_player->playbackStatus()}});
    });
    connect(session, &PlaybackSession::speedChanged, this, [this]() {
        notify({{QStringLiteral("Rate"), m_player->rate()}});
    });
    connect(session, &PlaybackSession::volumeChanged, this, [this]() {
        notify({{QStringLiteral("Volume"), m_player->volume()}});
    });
}

bool MprisAdapter::registerOnBus()
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.registerObject(QStringLiteral("/org/mpris/MediaPlayer2"), this))
        return false;
    return bus.registerService(AppConfig::mprisServiceName());
}

void MprisAdapter::notify(const QVariantMap &changed)
{
    QDBusMessage msg = QDBusMessage::createSignal(
        QStringLiteral("/org/mpris/MediaPlayer2"),
        QStringLiteral("org.freedesktop.DBus.Properties"),
        QStringLiteral("PropertiesChanged"));
    QVariantMap valid;
    for (auto it = changed.constBegin(); it != changed.constEnd(); ++it)
        if (it.value().isValid())
            valid.insert(it.key(), it.value());
    msg << QStringLiteral("org.mpris.MediaPlayer2.Player") << valid << QStringList();
    QDBusConnection::sessionBus().send(msg);
}

// --- Root -------------------------------------------------------------------
QString MprisRootAdaptor::desktopEntry() const
{
    return AppConfig::appId();
}

// CanRaise is advertised false, so Raise() is intentionally a no-op.
void MprisRootAdaptor::Raise() {}
// aboutToQuit is wired to flush a final progress sync + session close.
void MprisRootAdaptor::Quit() { QCoreApplication::quit(); }

// --- Player -----------------------------------------------------------------
MprisPlayerAdaptor::MprisPlayerAdaptor(MprisAdapter *owner, PlaybackSession *session)
    : QDBusAbstractAdaptor(owner)
    , m_owner(owner)
    , m_session(session)
{
}

QString MprisPlayerAdaptor::playbackStatus() const
{
    if (!m_session->active())
        return QStringLiteral("Stopped");
    return m_session->playing() ? QStringLiteral("Playing") : QStringLiteral("Paused");
}

double MprisPlayerAdaptor::rate() const { return m_session->speed(); }
void MprisPlayerAdaptor::setRate(double r) { m_session->setSpeed(r); }

double MprisPlayerAdaptor::volume() const { return m_session->volume(); }
void MprisPlayerAdaptor::setVolume(double v) { m_session->setVolume(v); }

qlonglong MprisPlayerAdaptor::position() const
{
    return static_cast<qlonglong>(m_session->position() * 1'000'000.0);
}

QVariantMap MprisPlayerAdaptor::metadata() const
{
    QVariantMap m;
    m.insert(QStringLiteral("mpris:trackid"),
             QVariant::fromValue(QDBusObjectPath(trackObjectPath(m_session))));
    m.insert(QStringLiteral("mpris:length"),
             static_cast<qlonglong>(m_session->duration() * 1'000'000.0));
    m.insert(QStringLiteral("xesam:title"), m_session->title());
    m.insert(QStringLiteral("xesam:artist"), QStringList{m_session->author()});
    return m;
}

void MprisPlayerAdaptor::Play()      { m_session->play(); }
void MprisPlayerAdaptor::Pause()     { m_session->pause(); }
void MprisPlayerAdaptor::PlayPause() { m_session->togglePlayPause(); }
void MprisPlayerAdaptor::Stop()      { m_session->stopAndClose(); }
void MprisPlayerAdaptor::Next()      { m_session->nextChapter(); }
void MprisPlayerAdaptor::Previous()  { m_session->previousChapter(); }

void MprisPlayerAdaptor::Seek(qlonglong offsetMicroseconds)
{
    m_session->skip(offsetMicroseconds / 1'000'000.0);
    emit Seeked(static_cast<qlonglong>(m_session->position() * 1'000'000.0));
}

void MprisPlayerAdaptor::SetPosition(const QDBusObjectPath &trackId, qlonglong positionMicroseconds)
{
    // MPRIS requires ignoring the call if trackId is not the current track (the
    // controller computed the position against a track that has since changed).
    if (trackId.path() != trackObjectPath(m_session))
        return;
    m_session->seekGlobal(positionMicroseconds / 1'000'000.0);
    emit Seeked(positionMicroseconds);
}
