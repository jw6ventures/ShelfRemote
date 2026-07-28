#pragma once

#include <QElapsedTimer>
#include <QObject>

// Accumulates *actual listening time* (wall-clock while playing) independently of
// the media position, which is essential at non-1x speed: a 10s real interval at
// 2x advances the media 20s but only adds ~10s of listening time. Paused and
// buffering time are not counted.
//
// The owner calls setPlaying() on play/pause and reads takeListenedSeconds() when
// it builds a /sync payload; the accumulator resets after each read.
class ProgressSyncer : public QObject
{
    Q_OBJECT
public:
    explicit ProgressSyncer(QObject *parent = nullptr);

    void reset();
    void setPlaying(bool playing);

    // Reserve-and-rollback sync pair. takeListenedSeconds() folds the current
    // playing span into the accumulator, returns the whole pending total, and
    // clears it — so the returned interval is now "owned" by exactly one in-flight
    // /sync request and can never be reported by a concurrent request (which is
    // what would double-count it on the server). If that request fails, the caller
    // returns the interval with rollback() so a later sync retries it.
    double takeListenedSeconds();
    void   rollback(double seconds);

    // Peek the pending total without resetting.
    double pendingListenedSeconds() const;

private:
    void flushInto();

    bool          m_playing = false;
    double        m_accumulated = 0.0; // seconds not yet taken
    QElapsedTimer m_timer;             // measures the current playing span
};
