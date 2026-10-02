#include "player/MprisAdapter.h"
#include "app/AppConfig.h"
#include "player/PlaybackSession.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusObjectPath>

namespace {
// D-Bus object path elements allow only [A-Za-z0-9_], and Audiobookshelf ids are
// UUIDs: their hyphens made the path invalid, QDBusObjectPath discarded it, and
// the Metadata map then failed to marshal. Hex keeps any id valid and distinct.
QString pathElement(const QString &id)
{
    return QString::fromLatin1(id.toUtf8().toHex());
}

// The single source of truth for the current MPRIS track object path, so
// Metadata (mpris:trackid) and SetPosition compare the exact same value. Podcast
// episodes share their show's item id, so the episode is part of the identity.
QString trackObjectPath(PlaybackSession *session)
{
    QString appPath = AppConfig::appId();
    appPath.replace(QLatin1Char('.'), QLatin1Char('/'));
    QString track = session->itemId().isEmpty() ? QStringLiteral("none")
                                                : QStringLiteral("i") + pathElement(session->itemId());
    if (!session->episodeId().isEmpty())
        track += QStringLiteral("_e") + pathElement(session->episodeId());
    return QStringLiteral("/") + appPath + QStringLiteral("/track/") + track;
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
    connect(session, &PlaybackSession::pausedChanged, this, [this]() {
        notify({{QStringLiteral("PlaybackStatus"), m_player->playbackStatus()}});
    });
    // active toggles between Stopped and Playing/Paused and swaps the track, so the
    // status, metadata and what can be controlled all change with it.
    connect(session, &PlaybackSession::activeChanged, this, [this]() {
        const bool can = m_player->canAct();
        notify({{QStringLiteral("PlaybackStatus"), m_player->playbackStatus()},
                {QStringLiteral("Metadata"), m_player->metadata()},
                {QStringLiteral("CanGoNext"), can},
                {QStringLiteral("CanGoPrevious"), can},
                {QStringLiteral("CanPlay"), can},
                {QStringLiteral("CanPause"), can},
                {QStringLiteral("CanSeek"), can}});
    });
    // Every jump, not only those requested over MPRIS: a controller extrapolates
    // Position from Rate between Seeked signals, so an in-app skip or chapter
    // change it never hears about leaves its progress bar wrong.
    connect(session, &PlaybackSession::seeked, this, [this](double position) {
        emit m_player->Seeked(static_cast<qlonglong>(position * 1'000'000.0));
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
    // The requested state: a stream stalled buffering is still "Playing".
    return m_session->paused() ? QStringLiteral("Paused") : QStringLiteral("Playing");
}

double MprisPlayerAdaptor::rate() const { return m_session->speed(); }

void MprisPlayerAdaptor::setRate(double r)
{
    // The spec says a client setting 0 means Pause; anything else is held to the
    // advertised range rather than handed to mpv unchecked.
    if (r <= 0.0) {
        m_session->pause();
        return;
    }
    m_session->setSpeed(qBound(minimumRate(), r, maximumRate()));
}

double MprisPlayerAdaptor::volume() const { return m_session->volume(); }
void MprisPlayerAdaptor::setVolume(double v) { m_session->setVolume(v); }

qlonglong MprisPlayerAdaptor::position() const
{
    return static_cast<qlonglong>(m_session->position() * 1'000'000.0);
}

bool MprisPlayerAdaptor::canAct() const { return m_session->active(); }

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

// Seeked is emitted from the session's own seeked signal (see MprisAdapter), so
// it reports where playback actually landed, and only when it moved.
void MprisPlayerAdaptor::Seek(qlonglong offsetMicroseconds)
{
    m_session->skip(offsetMicroseconds / 1'000'000.0);
}

void MprisPlayerAdaptor::SetPosition(const QDBusObjectPath &trackId, qlonglong positionMicroseconds)
{
    // MPRIS requires ignoring the call if trackId is not the current track (the
    // controller computed the position against a track that has since changed).
    if (!m_session->active() || trackId.path() != trackObjectPath(m_session))
        return;
    // ...and if the position is outside the track. Clamping instead turned a stray
    // past-the-end request into a seek to EOF, which closes the session and marks
    // the book finished.
    const double seconds = positionMicroseconds / 1'000'000.0;
    if (seconds < 0.0 || seconds > m_session->duration())
        return;
    m_session->seekGlobal(seconds);
}
