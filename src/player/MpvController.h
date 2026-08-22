#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVariantList>

struct mpv_handle;

// Minimal libmpv wrapper for windowless audio playback. Loads authenticated
// stream URLs (bearer token injected via http-header-fields, never in the URL),
// controls play/pause/seek/speed, and surfaces position + end-of-file events on
// the Qt thread. The higher-level PlaybackSession drives it.
class MpvController : public QObject
{
    Q_OBJECT
public:
    explicit MpvController(QObject *parent = nullptr);
    ~MpvController() override;

    bool init();

    // Loads a URL, optionally with an Authorization header, and (optionally)
    // seeks to `startSeconds` within that file once loaded.
    void load(const QString &url, const QString &authHeader, double startSeconds = 0.0);
    // Loads the first URL and queues the rest as one mpv playlist. All entries
    // must use the same HTTP headers. Keeping the next book track queued lets
    // mpv retain its audio output instead of closing and reopening it at EOF.
    void loadPlaylist(const QStringList &urls, const QString &authHeader,
                      double startSeconds = 0.0);

    void play();
    void pause();
    void stop();
    void seekAbsolute(double seconds);      // within the current file
    void seekRelative(double deltaSeconds);
    void setSpeed(double speed);            // 1.0 == normal
    double speed() const { return m_speed; }
    void setVolume(double percent);         // 0..100 (mpv scale)
    double volume() const { return m_volume; }
    void setHttpHeaders(const QString &headers);

    // Audio output device selection. audioDevices() returns a list of
    // {name, description} maps (with an "auto" entry first); setAudioDevice() routes
    // output to the named device (or "auto"). The choice is applied to mpv here and
    // persisted separately in AppSettings.
    Q_INVOKABLE QVariantList audioDevices() const;
    Q_INVOKABLE void setAudioDevice(const QString &name);
    QString audioDevice() const { return m_audioDevice; }

    double positionInFile() const { return m_position; }  // seconds into the file
    double durationOfFile() const { return m_duration; }
    bool isPlaying() const { return m_playing; }
    qint64 audioOutputSampleRate() const { return m_outputSampleRate; }
    qint64 audioOutputChannelCount() const { return m_outputChannelCount; }

signals:
    void positionChanged(double positionInFile);
    void durationChanged(double duration);
    void playingChanged(bool playing);
    void endOfFile();     // current file finished (drives track transitions)
    void fileLoaded();
    // Emitted when mpv opens, closes, or changes the underlying audio output.
    // Primarily useful for diagnostics and transition regression tests.
    void audioOutputReconfigured();
    void mpvError(const QString &message);

private:
    void setProperty(const QString &name, const QVariant &value);
    void command(const QStringList &args);
    static void onWakeup(void *ctx);
    void handleEvents();
    void updateAudioOutputState();
    // Recomputes "effectively playing" from the raw mpv states and emits
    // playingChanged only when it flips.
    void updatePlaying();

    mpv_handle *m_mpv = nullptr;
    double m_position = 0.0;
    double m_duration = 0.0;
    double m_speed = 1.0;
    double m_volume = 100.0;
    double m_pendingSeek = 0.0;
    QString m_audioDevice = QStringLiteral("auto");
    // Raw mpv states. "Effectively playing" (m_playing) means actually producing
    // audio: not paused, the core is not idle, and it is not stalled buffering.
    bool   m_paused = false;
    bool   m_coreIdle = true;
    bool   m_pausedForCache = false;
    bool   m_playing = false;
    qint64 m_outputSampleRate = 0;
    qint64 m_outputChannelCount = 0;
    QString m_outputFormat;
};
