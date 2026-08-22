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

public:
    PlaybackSession(ApiClient *api, MpvController *mpv, QObject *parent = nullptr);

    bool active() const { return m_active; }
    bool playing() const { return m_playing; }
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

    // Sleep timer: setSleepTimer(0) cancels; cycleSleepTimer() steps through the
    // off/15/30/60-minute presets. On expiry playback is paused (not closed).
    Q_INVOKABLE void setSleepTimer(int minutes);
    Q_INVOKABLE void cycleSleepTimer();

    // Called on session switch/stop to flush a final sync + close. On app
    // shutdown pass blocking=true so the close is actually transmitted before the
    // event loop exits (aboutToQuit otherwise returns before the request is sent).
    void flushAndClose(bool blocking = false);

signals:
    void activeChanged();
    void playingChanged(bool playing);
    void metadataChanged();
    void positionChanged(double position);
    void speedChanged();
    void volumeChanged();
    void chapterChanged();
    void sleepTimerChanged();
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
    void scheduleSync();

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
    QTimer          m_sleepTimer;      // fires once; pauses playback on expiry
    int             m_sleepMinutes = 0; // configured sleep duration (0 == off)

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
