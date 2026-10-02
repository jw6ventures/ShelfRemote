#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QVector>

class ApiClient;
class MpvController;
class ProgressSyncer;

// Orchestrates an Audiobookshelf playback session end to end:
//   - POST /api/items/:id/play (with stable deviceInfo) to open a session
//   - maps the book's single global timeline across multiple audioTracks
//   - drives libmpv, loading the next track at EOF without closing the session
//   - periodically POST /api/session/:id/sync with currentTime + timeListened
//   - POST /api/session/:id/close (final sync) on stop/shutdown
// This is the primary object the NowPlaying QML screen binds to.
class PlaybackSession : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool active READ active NOTIFY activeChanged)
    Q_PROPERTY(bool playing READ playing NOTIFY playingChanged)
    // What the user asked for. Unlike `playing`, this stays false while a playing
    // stream stalls to buffer or moves between files, so it is what Play/Pause
    // controls should reflect and toggle.
    Q_PROPERTY(bool paused READ paused NOTIFY pausedChanged)
    Q_PROPERTY(QString title READ title NOTIFY metadataChanged)
    Q_PROPERTY(QString author READ author NOTIFY metadataChanged)
    Q_PROPERTY(QString itemId READ itemId NOTIFY metadataChanged)
    Q_PROPERTY(double position READ position NOTIFY positionChanged)   // global seconds
    Q_PROPERTY(double duration READ duration NOTIFY metadataChanged)   // whole-book seconds
    Q_PROPERTY(double speed READ speed WRITE setSpeed NOTIFY speedChanged)
    Q_PROPERTY(int chapterIndex READ chapterIndex NOTIFY chapterChanged)
    Q_PROPERTY(QString chapterTitle READ chapterTitle NOTIFY chapterChanged)
    // Sleep timer lives here (not in the Now Playing screen) so navigating away
    // from that screen does not cancel a running countdown. 0 == off.
    Q_PROPERTY(int sleepMinutes READ sleepMinutes NOTIFY sleepTimerChanged)
    // Whole seconds left on a minutes-based sleep timer (0 when off). The countdown
    // only runs while audio is actually playing.
    Q_PROPERTY(int sleepRemaining READ sleepRemaining NOTIFY sleepRemainingChanged)
    // True while the sleep timer is set to pause at the end of the current chapter.
    Q_PROPERTY(bool sleepAtChapterEnd READ sleepAtChapterEnd NOTIFY sleepTimerChanged)

public:
    PlaybackSession(ApiClient *api, MpvController *mpv, QObject *parent = nullptr);

    bool active() const { return m_active; }
    bool playing() const { return m_playing; }
    bool paused() const;
    QString title() const { return m_title; }
    QString author() const { return m_author; }
    QString itemId() const { return m_itemId; }
    // Non-empty only for podcast episodes. Episode progress is tracked server-side
    // against the session, not as item-level progress on the podcast.
    QString episodeId() const { return m_episodeId; }
    // True when the last session ended because playback reached the end of the
    // book/episode (natural EOF), as opposed to a manual stop or an error.
    bool completed() const { return m_completed; }
    double position() const { return m_globalPosition; }
    double duration() const { return m_duration; }
    double speed() const;
    double volume() const;                       // 0..1
    int chapterIndex() const { return m_chapterIndex; }
    QString chapterTitle() const;
    int sleepMinutes() const { return m_sleepMinutes; }
    int sleepRemaining() const;
    bool sleepAtChapterEnd() const { return m_sleepAtChapterEnd; }

    QJsonArray chapters() const { return m_chapters; }

    Q_INVOKABLE void playItem(const QString &itemId);
    Q_INVOKABLE void playEpisode(const QString &itemId, const QString &episodeId);
    Q_INVOKABLE void togglePlayPause();
    Q_INVOKABLE void play();
    Q_INVOKABLE void pause();
    Q_INVOKABLE void seekGlobal(double seconds);
    Q_INVOKABLE void skip(double deltaSeconds);
    Q_INVOKABLE void nextChapter();
    Q_INVOKABLE void previousChapter();
    Q_INVOKABLE void setSpeed(double speed);
    Q_INVOKABLE void setVolume(double volume);   // 0..1
    Q_INVOKABLE void stopAndClose();

    // Sleep timer: setSleepTimer(0) cancels; setSleepAtChapterEnd() pauses when
    // the chapter playing now ends; cycleSleepTimer() steps through off, the
    // 15/30/60-minute presets and (for books with chapters) end of chapter. On
    // expiry playback is paused (not closed).
    Q_INVOKABLE void setSleepTimer(int minutes);
    Q_INVOKABLE void setSleepAtChapterEnd();
    Q_INVOKABLE void cycleSleepTimer();

    // Called on session switch/stop to flush a final sync + close. On app
    // shutdown pass blocking=true so the close is actually transmitted before the
    // event loop exits (aboutToQuit otherwise returns before the request is sent).
    void flushAndClose(bool blocking = false);

signals:
    void activeChanged();
    void playingChanged(bool playing);
    void pausedChanged();
    // The position jumped (skip, chapter, bookmark, or any explicit seek), as
    // opposed to advancing through playback. Feeds MPRIS's Seeked signal.
    void seeked(double position);
    void metadataChanged();
    void positionChanged(double position);
    void speedChanged();
    void volumeChanged();
    void chapterChanged();
    void sleepTimerChanged();
    void sleepRemainingChanged();
    void playbackError(const QString &message);

private:
    void startSession(const QUrl &playUrl);
    void applyPlayResponse(const QJsonObject &obj);
    void loadTrackForGlobal(double globalSeconds, bool autoplay);
    int  trackIndexForGlobal(double globalSeconds) const;
    void onMpvPosition(double positionInFile);
    void onEndOfFile();
    void onPlayingChanged(bool playing);
    void updateChapterForPosition(double globalSeconds);
    void sync(const QString &reason);
    // Requests a sync a short moment from now, collapsing a burst of seeks into
    // one request. Any sync that goes out for another reason cancels it.
    void scheduleSync();
    // Sleep timer helpers. The minutes countdown is held while nothing is audible
    // (paused or buffering) and resumed when playback is.
    void holdSleepCountdown();
    void resumeSleepCountdown();
    // Cancels any sleep timer; returns true if one was set.
    bool clearSleepTimer();
    // Where "end of chapter" falls for a given position: the end of the chapter
    // containing it, else the start of the next chapter, else the end of the book.
    double chapterEndFor(double globalSeconds) const;
    // Previous/Next fall back to a skip of the user's interval when the book has
    // no chapters to move between.
    double fallbackSkipSeconds() const;

    struct Track {
        int index = 0;
        double startOffset = 0.0;
        double duration = 0.0;
        QString contentUrl;
        QString mimeType;
    };

    ApiClient      *m_api;
    MpvController  *m_mpv;
    ProgressSyncer *m_listen;
    QTimer          m_syncTimer;
    QTimer          m_seekSyncTimer;   // coalesces a burst of seeks into one sync
    QTimer          m_sleepTimer;      // fires once; pauses playback on expiry
    QTimer          m_sleepTick;       // refreshes sleepRemaining once a second
    int             m_sleepMinutes = 0; // configured sleep duration (0 == off)
    qint64          m_sleepRemainingMs = 0; // countdown left while it is held
    bool            m_sleepAtChapterEnd = false;
    double          m_sleepChapterEnd = 0.0; // global seconds to pause at

    bool     m_active = false;
    bool     m_playing = false;
    // Bumped per play request; a play response from a superseded request is
    // dropped so rapid item switches can never apply out of order.
    quint64  m_playGeneration = 0;
    // Identifies the current listening-accounting epoch. Bumped whenever a new
    // session's accumulator is reset, so a /sync or /close reply from a previous
    // session can never commit or roll back into the new session's accounting.
    quint64  m_listenGeneration = 0;
    QString  m_sessionId;
    QString  m_itemId;
    QString  m_episodeId;          // non-empty for podcast episodes
    bool     m_completed = false;  // set on natural EOF, reset when a session starts
    QString  m_title;
    QString  m_author;
    double   m_duration = 0.0;
    double   m_globalPosition = 0.0;
    int      m_currentTrack = -1;
    // Last track already present in mpv's current playlist. Consecutive tracks
    // with the same authorization policy are queued together for gapless output.
    int      m_queuedThroughTrack = -1;
    bool     m_currentPlaylistUsesAuth = false;
    QVector<Track> m_tracks;
    QJsonArray m_chapters;
    int      m_chapterIndex = -1;
    bool     m_isHls = false; // transcoded session: single HLS "track"
};
