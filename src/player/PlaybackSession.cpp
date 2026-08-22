#include "player/PlaybackSession.h"
#include "app/AppConfig.h"
#include "net/ApiClient.h"
#include "player/MpvController.h"
#include "player/ProgressSyncer.h"
#include "storage/Database.h"

#include <QPointer>

#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTimer>

#include <algorithm>

namespace {
constexpr int kSyncIntervalMs = 12000; // 12s while actively playing

// True when two URLs share scheme + host + effective port. Used to decide whether
// the Audiobookshelf bearer token may be attached to a (server-supplied) content
// URL: the token must only ever be sent to the configured server's own origin.
bool sameOrigin(const QUrl &a, const QUrl &b)
{
    auto effectivePort = [](const QUrl &u) {
        if (u.port() != -1)
            return u.port();
        const QString s = u.scheme().toLower();
        if (s == QLatin1String("https")) return 443;
        if (s == QLatin1String("http")) return 80;
        return -1;
    };
    return a.scheme().compare(b.scheme(), Qt::CaseInsensitive) == 0 &&
           a.host().compare(b.host(), Qt::CaseInsensitive) == 0 &&
           effectivePort(a) == effectivePort(b);
}
} // namespace

PlaybackSession::PlaybackSession(ApiClient *api, MpvController *mpv, QObject *parent)
    : QObject(parent)
    , m_api(api)
    , m_mpv(mpv)
    , m_listen(new ProgressSyncer(this))
{
    m_syncTimer.setInterval(kSyncIntervalMs);
    connect(&m_syncTimer, &QTimer::timeout, this, [this]() { sync(QStringLiteral("interval")); });

    // Sleep timer is owned by the session, not the (disposable) Now Playing screen,
    // so leaving that screen never cancels a running countdown. On expiry we pause
    // rather than close, so the user can resume from exactly where they drifted off.
    m_sleepTimer.setSingleShot(true);
    connect(&m_sleepTimer, &QTimer::timeout, this, [this]() {
        pause();
        m_sleepMinutes = 0;
        emit sleepTimerChanged();
    });

    connect(m_mpv, &MpvController::positionChanged, this, &PlaybackSession::onMpvPosition);
    connect(m_mpv, &MpvController::endOfFile, this, &PlaybackSession::onEndOfFile);
    connect(m_mpv, &MpvController::playingChanged, this, &PlaybackSession::onPlayingChanged);
    connect(m_api, &ApiClient::accessTokenChanged, this, [this]() {
        if (m_active && m_currentPlaylistUsesAuth) {
            m_mpv->setHttpHeaders(QStringLiteral("Authorization: Bearer ") +
                                  m_api->accessToken());
        }
    });
    connect(m_mpv, &MpvController::mpvError, this, [this](const QString &message) {
        // A stream failed: close the (now silent) session and tell the user rather
        // than leaving it apparently active.
        if (m_active) {
            flushAndClose();
            // A multi-track book has later files queued in mpv. Do not let mpv
            // advance into one of them after the session has been closed.
            m_mpv->stop();
        }
        emit playbackError(tr("Playback error: %1").arg(message));
    });
}

double PlaybackSession::speed() const
{
    return m_mpv->speed();
}

double PlaybackSession::volume() const
{
    return m_mpv->volume() / 100.0; // mpv 0..100 -> 0..1
}

QString PlaybackSession::chapterTitle() const
{
    if (m_chapterIndex < 0 || m_chapterIndex >= m_chapters.size())
        return {};
    return m_chapters.at(m_chapterIndex).toObject().value(QStringLiteral("title")).toString();
}

namespace {

QJsonObject deviceInfo()
{
    return QJsonObject{
        {QStringLiteral("deviceId"), AppConfig::deviceId()},
        {QStringLiteral("clientName"), QStringLiteral("ShelfRemote")},
        {QStringLiteral("clientVersion"), AppConfig::version()},
        {QStringLiteral("manufacturer"), QStringLiteral("JW6")},
        {QStringLiteral("model"), QStringLiteral("Linux HTPC")},
    };
}

QJsonObject playRequestBody()
{
    return QJsonObject{
        {QStringLiteral("deviceInfo"), deviceInfo()},
        {QStringLiteral("mediaPlayer"), QStringLiteral("mpv")},
        {QStringLiteral("supportedMimeTypes"), QJsonArray{
            QStringLiteral("audio/mpeg"), QStringLiteral("audio/mp4"),
            QStringLiteral("audio/aac"), QStringLiteral("audio/flac"),
            QStringLiteral("audio/ogg"), QStringLiteral("audio/opus")}},
        {QStringLiteral("forceDirectPlay"), false},
        {QStringLiteral("forceTranscode"), false},
    };
}

} // namespace

void PlaybackSession::playItem(const QString &itemId)
{
    // Close the prior session BEFORE adopting the new id, so its final sync/close
    // (and the activeChanged handler that records progress) is attributed to the
    // item that was actually playing, not the one about to start.
    if (m_active) {
        flushAndClose();
        // flushAndClose() only stops the accounting/session; stop the outgoing
        // stream too, or the previous track keeps playing (untracked) until the
        // new play response loads a file.
        m_mpv->stop();
    }
    m_itemId = itemId;
    m_episodeId.clear(); // a book, not an episode
    startSession(m_api->endpoints().play(itemId));
}

void PlaybackSession::playEpisode(const QString &itemId, const QString &episodeId)
{
    if (m_active) {
        flushAndClose();
        m_mpv->stop();
    }
    m_itemId = itemId;
    m_episodeId = episodeId; // track the episode identity explicitly
    startSession(m_api->endpoints().playEpisode(itemId, episodeId));
}

void PlaybackSession::startSession(const QUrl &playUrl)
{
    const quint64 gen = ++m_playGeneration;
    m_api->post(playUrl, playRequestBody(), [this, gen](const ApiResponse &res) {
        // A newer play request has superseded this one, or the client was pointed
        // at a different server since we asked to play; either way, never open a
        // session from this response (it may belong to the previous server).
        if (res.stale || gen != m_playGeneration)
            return;
        if (!res.ok) {
            emit playbackError(tr("Could not start playback (HTTP %1)").arg(res.status));
            return;
        }
        applyPlayResponse(res.json());
    });
}

void PlaybackSession::applyPlayResponse(const QJsonObject &obj)
{
    m_sessionId = obj.value(QStringLiteral("id")).toString();
    m_duration = obj.value(QStringLiteral("duration")).toDouble();
    m_chapters = obj.value(QStringLiteral("chapters")).toArray();

    const QJsonObject media = obj.value(QStringLiteral("mediaMetadata")).toObject();
    m_title = obj.value(QStringLiteral("displayTitle")).toString(
        media.value(QStringLiteral("title")).toString());
    m_author = obj.value(QStringLiteral("displayAuthor")).toString(
        media.value(QStringLiteral("author")).toString());

    m_tracks.clear();
    const QJsonArray tracks = obj.value(QStringLiteral("audioTracks")).toArray();
    for (const auto &tv : tracks) {
        const QJsonObject t = tv.toObject();
        Track track;
        track.index = t.value(QStringLiteral("index")).toInt();
        track.startOffset = t.value(QStringLiteral("startOffset")).toDouble();
        track.duration = t.value(QStringLiteral("duration")).toDouble();
        track.contentUrl = t.value(QStringLiteral("contentUrl")).toString();
        track.mimeType = t.value(QStringLiteral("mimeType")).toString();
        m_tracks.push_back(track);
    }
    std::sort(m_tracks.begin(), m_tracks.end(),
              [](const Track &a, const Track &b) { return a.startOffset < b.startOffset; });

    // Transcoded sessions return a single HLS playlist.
    m_isHls = m_tracks.size() == 1 &&
              m_tracks.first().mimeType.contains(QStringLiteral("mpegurl"));

    // Some servers omit a top-level duration; derive it from the tracks so global
    // seeking still works.
    if (m_duration <= 0.0 && !m_tracks.isEmpty()) {
        const Track &last = m_tracks.last();
        m_duration = last.startOffset + last.duration;
    }

    // Validate before committing: a 2xx status is not proof of a usable session. A
    // changed or malformed response (no session id, no playable track, or no usable
    // duration) must surface as an error, not open a silent, audio-less Now Playing.
    if (m_sessionId.isEmpty() || m_tracks.isEmpty() ||
        m_tracks.first().contentUrl.isEmpty() || m_duration <= 0.0) {
        emit playbackError(tr("The server returned an unusable playback session"));
        return;
    }

    const double resume = obj.value(QStringLiteral("currentTime")).toDouble(
        obj.value(QStringLiteral("startTime")).toDouble(0.0));
    m_globalPosition = resume;

    m_active = true;
    m_completed = false; // fresh session: not yet finished
    emit activeChanged();
    emit metadataChanged();

    m_listen->reset();
    // New accounting epoch: any late /sync or /close reply from the previous
    // session now belongs to a stale generation and will be ignored.
    ++m_listenGeneration;

    // Apply the user's configured default playback rate to the new session.
    const double defaultRate =
        Database::instance().getSetting(QStringLiteral("defaultRate"),
                                        QStringLiteral("1.0")).toDouble();
    if (defaultRate > 0.0)
        m_mpv->setSpeed(defaultRate);

    loadTrackForGlobal(resume, /*autoplay*/ true);
    emit speedChanged();
}

int PlaybackSession::trackIndexForGlobal(double globalSeconds) const
{
    for (int i = 0; i < m_tracks.size(); ++i) {
        const Track &t = m_tracks.at(i);
        if (globalSeconds >= t.startOffset && globalSeconds < t.startOffset + t.duration)
            return i;
    }
    return m_tracks.isEmpty() ? -1 : m_tracks.size() - 1;
}

void PlaybackSession::loadTrackForGlobal(double globalSeconds, bool autoplay)
{
    const int idx = trackIndexForGlobal(globalSeconds);
    if (idx < 0)
        return;
    m_currentTrack = idx;
    const Track &t = m_tracks.at(idx);

    // For HLS the whole book is one stream: seek is global, offset is 0.
    const double inFile = m_isHls ? globalSeconds : (globalSeconds - t.startOffset);

    // The contentUrl is server-controlled and may be absolute. Attach the bearer
    // ONLY when it resolves to the configured server's own origin: direct-play file
    // URLs live there and need it; a URL pointing anywhere else (a CDN, or a
    // malicious absolute URL) must never receive our access token. HLS transcode
    // URLs are same-origin and pre-authorized, so the header is harmless there.
    // Queue every consecutive track with the same header policy. mpv can then
    // advance before its core becomes idle, preserving the audio output across
    // file boundaries. Splitting at an origin-policy change ensures the bearer
    // token can never be inherited by an external playlist entry.
    QStringList urls;
    QString playlistAuthHeader;
    for (int i = idx; i < m_tracks.size(); ++i) {
        const QUrl url = m_api->endpoints().resolveContentUrl(m_tracks.at(i).contentUrl);
        QString authHeader;
        if (sameOrigin(url, m_api->baseUrl()))
            authHeader = QStringLiteral("Authorization: Bearer ") + m_api->accessToken();
        if (urls.isEmpty())
            playlistAuthHeader = authHeader;
        else if (authHeader != playlistAuthHeader)
            break;
        urls.push_back(url.toString());
    }
    m_queuedThroughTrack = idx + urls.size() - 1;
    m_currentPlaylistUsesAuth = !playlistAuthHeader.isEmpty();
    m_mpv->loadPlaylist(urls, playlistAuthHeader, inFile);
    if (!autoplay)
        m_mpv->pause();
    updateChapterForPosition(globalSeconds);
}

void PlaybackSession::onMpvPosition(double positionInFile)
{
    if (!m_active || m_currentTrack < 0)
        return;
    const Track &t = m_tracks.at(m_currentTrack);
    const double global = m_isHls ? positionInFile : (t.startOffset + positionInFile);
    m_globalPosition = global;
    emit positionChanged(global);
    updateChapterForPosition(global);
}

void PlaybackSession::onEndOfFile()
{
    if (!m_active)
        return;
    // A transcoded session is a single HLS stream, so EOF means the whole book
    // finished: close it rather than leaving the session open. stopAndClose()
    // flushes a final /close carrying the outstanding listened time, so a separate
    // preceding sync() would only re-report the same interval.
    if (m_isHls) {
        m_completed = true; // natural end of the whole book/episode
        stopAndClose();
        return;
    }
    // Advance to the next track without closing the session.
    if (m_currentTrack + 1 < m_tracks.size()) {
        sync(QStringLiteral("track-transition"));
        const int nextTrack = m_currentTrack + 1;
        if (nextTrack <= m_queuedThroughTrack) {
            // mpv is already moving to this playlist entry. Update the global
            // timeline mapping before its first time-pos event arrives.
            m_currentTrack = nextTrack;
            updateChapterForPosition(m_tracks.at(nextTrack).startOffset + 0.01);
        } else {
            // Authorization policy changed, so this track could not safely share
            // the previous playlist. Start a new same-policy queue here.
            loadTrackForGlobal(m_tracks.at(nextTrack).startOffset + 0.01, true);
        }
    } else {
        // Book finished; the final /close reports the outstanding listened time.
        m_completed = true; // reached the end of the last track
        stopAndClose();
    }
}

void PlaybackSession::onPlayingChanged(bool playing)
{
    m_playing = playing;
    // Only account for listening time while a session is actually active. The mpv
    // controller now reports `playing` as truly-producing-audio (paused, idle, and
    // cache-buffering states all read as not-playing), so buffering time is
    // correctly excluded here.
    if (m_active) {
        m_listen->setPlaying(playing);
        if (playing)
            m_syncTimer.start();
        else {
            m_syncTimer.stop();
            sync(QStringLiteral("pause"));
        }
    }
    emit playingChanged(playing);
}

void PlaybackSession::updateChapterForPosition(double globalSeconds)
{
    int idx = -1;
    for (int i = 0; i < m_chapters.size(); ++i) {
        const QJsonObject c = m_chapters.at(i).toObject();
        const double start = c.value(QStringLiteral("start")).toDouble();
        const double end = c.value(QStringLiteral("end")).toDouble();
        if (globalSeconds >= start && globalSeconds < end) {
            idx = i;
            break;
        }
    }
    if (idx != m_chapterIndex) {
        m_chapterIndex = idx;
        emit chapterChanged();
    }
}

void PlaybackSession::togglePlayPause()
{
    if (!m_active) return;
    if (m_playing) pause(); else play();
}

void PlaybackSession::play()  { if (m_active) m_mpv->play(); }
void PlaybackSession::pause() { if (m_active) m_mpv->pause(); }

void PlaybackSession::seekGlobal(double seconds)
{
    // Global media shortcuts and MPRIS advertise every control as available, so
    // this can be invoked before a track is loaded; never index an empty vector.
    if (!m_active || m_currentTrack < 0 || m_currentTrack >= m_tracks.size())
        return;
    seconds = qBound(0.0, seconds, m_duration);
    if (m_isHls || trackIndexForGlobal(seconds) == m_currentTrack) {
        const Track &t = m_tracks.at(m_currentTrack);
        m_mpv->seekAbsolute(m_isHls ? seconds : (seconds - t.startOffset));
    } else {
        loadTrackForGlobal(seconds, m_playing);
    }
    m_globalPosition = seconds;
    emit positionChanged(seconds);
    sync(QStringLiteral("seek"));
}

void PlaybackSession::skip(double deltaSeconds)
{
    seekGlobal(m_globalPosition + deltaSeconds);
}

void PlaybackSession::nextChapter()
{
    if (!m_active) return;
    if (m_chapterIndex + 1 < m_chapters.size())
        seekGlobal(m_chapters.at(m_chapterIndex + 1).toObject()
                       .value(QStringLiteral("start")).toDouble());
}

void PlaybackSession::previousChapter()
{
    if (!m_active) return;
    // If more than 3s into the chapter, restart it; otherwise go to the previous.
    const double chapStart = (m_chapterIndex >= 0 && m_chapterIndex < m_chapters.size())
        ? m_chapters.at(m_chapterIndex).toObject().value(QStringLiteral("start")).toDouble()
        : 0.0;
    if (m_globalPosition - chapStart > 3.0 || m_chapterIndex <= 0)
        seekGlobal(chapStart);
    else
        seekGlobal(m_chapters.at(m_chapterIndex - 1).toObject()
                       .value(QStringLiteral("start")).toDouble());
}

void PlaybackSession::setSpeed(double speed)
{
    m_mpv->setSpeed(speed);
    emit speedChanged();
}

void PlaybackSession::setVolume(double volume)
{
    m_mpv->setVolume(qBound(0.0, volume, 1.0) * 100.0);
    emit volumeChanged();
}

void PlaybackSession::setSleepTimer(int minutes)
{
    m_sleepMinutes = qMax(0, minutes);
    if (m_sleepMinutes > 0)
        m_sleepTimer.start(m_sleepMinutes * 60 * 1000);
    else
        m_sleepTimer.stop();
    emit sleepTimerChanged();
}

void PlaybackSession::cycleSleepTimer()
{
    // off -> 15 -> 30 -> 60 -> off
    switch (m_sleepMinutes) {
    case 0:  setSleepTimer(15); break;
    case 15: setSleepTimer(30); break;
    case 30: setSleepTimer(60); break;
    default: setSleepTimer(0);  break;
    }
}

void PlaybackSession::sync(const QString &reason)
{
    Q_UNUSED(reason)
    if (!m_active || m_sessionId.isEmpty())
        return;
    // Reserve the outstanding listened time up front: this request now exclusively
    // owns that interval, so an overlapping sync (interval timer + pause + seek can
    // all fire close together) reserves only the disjoint time accrued after this,
    // and the same seconds are never reported to the server twice.
    const double listened = m_listen->takeListenedSeconds();
    const quint64 gen = m_listenGeneration;
    QJsonObject body{
        {QStringLiteral("currentTime"), m_globalPosition},
        {QStringLiteral("timeListened"), listened}, // wall-clock, rate-independent
        {QStringLiteral("duration"), m_duration},
    };
    m_api->post(m_api->endpoints().sessionSync(m_sessionId), body,
                [this, listened, gen](const ApiResponse &res) {
                    // Drop a reply that belongs to a session already superseded, or
                    // one from a server we have since switched away from, so it cannot
                    // roll stale time back into the new session's book.
                    if (res.stale || gen != m_listenGeneration)
                        return;
                    if (!res.ok)
                        m_listen->rollback(listened); // keep it for the next sync
                }, /*followRedirects*/ false);
}

void PlaybackSession::stopAndClose()
{
    flushAndClose();
    m_mpv->stop();
}

void PlaybackSession::flushAndClose(bool blocking)
{
    if (!m_active)
        return;
    // A closing session has nothing left to pause; drop any pending sleep timer so
    // it can't fire into the next session.
    if (m_sleepTimer.isActive() || m_sleepMinutes != 0) {
        m_sleepTimer.stop();
        m_sleepMinutes = 0;
        emit sleepTimerChanged();
    }
    m_syncTimer.stop();
    m_listen->setPlaying(false);
    // Reserve the final interval; this /close request owns it (see sync()).
    const double listened = m_listen->takeListenedSeconds();
    const quint64 gen = m_listenGeneration;

    const QString sessionId = m_sessionId;
    QJsonObject body{
        {QStringLiteral("currentTime"), m_globalPosition},
        {QStringLiteral("timeListened"), listened},
        {QStringLiteral("duration"), m_duration},
    };

    m_active = false;
    m_sessionId.clear();
    emit activeChanged();

    if (sessionId.isEmpty())
        return;

    const QUrl closeUrl = m_api->endpoints().sessionClose(sessionId);
    if (!blocking) {
        m_api->post(closeUrl, body, [this, listened, gen](const ApiResponse &res) {
            if (res.stale || gen != m_listenGeneration)
                return; // a new session already started; don't touch its accounting
            if (!res.ok)
                m_listen->rollback(listened);
        }, /*followRedirects*/ false);
        return;
    }

    // Shutdown path: spin a local event loop so the close is actually sent before
    // the application exits, with a hard cap so a hung network never blocks quit.
    // The callback captures ONLY a guarded QPointer to the (stack) loop: if the
    // 3s cap fires first and this function returns, a late reply must not touch
    // the by-now-destroyed loop/locals. Local accumulator bookkeeping is pointless
    // during shutdown, so it is deliberately omitted here.
    QEventLoop loop;
    QPointer<QEventLoop> loopPtr(&loop);
    m_api->post(closeUrl, body, [loopPtr](const ApiResponse &) {
        if (loopPtr)
            loopPtr->quit();
    }, /*followRedirects*/ false);
    // The reply is delivered through the event loop, so it cannot fire before
    // exec() begins; no quit-before-exec race to guard against.
    QTimer::singleShot(3000, &loop, &QEventLoop::quit);
    loop.exec();
}
