#include "player/ProgressSyncer.h"

ProgressSyncer::ProgressSyncer(QObject *parent)
    : QObject(parent)
{
}

void ProgressSyncer::reset()
{
    m_accumulated = 0.0;
    m_playing = false;
    m_timer.invalidate();
}

void ProgressSyncer::flushInto()
{
    // Fold any elapsed playing span into the accumulator and restart the timer.
    if (m_playing && m_timer.isValid()) {
        m_accumulated += m_timer.elapsed() / 1000.0;
        m_timer.restart();
    }
}

void ProgressSyncer::setPlaying(bool playing)
{
    if (playing == m_playing)
        return;
    if (playing) {
        m_timer.start();
        m_playing = true;
    } else {
        flushInto();     // capture the span that just ended
        m_playing = false;
        m_timer.invalidate();
    }
}

double ProgressSyncer::pendingListenedSeconds() const
{
    double v = m_accumulated;
    if (m_playing && m_timer.isValid())
        v += m_timer.elapsed() / 1000.0;
    return v;
}

double ProgressSyncer::takeListenedSeconds()
{
    flushInto();
    const double v = m_accumulated;
    m_accumulated = 0.0;
    return v;
}

void ProgressSyncer::rollback(double seconds)
{
    // A /sync request that carried `seconds` failed: return that interval to the
    // pending accumulator (the timer kept running, so more may have accrued in the
    // meantime) so the next sync retries it. No listening time is lost.
    if (seconds > 0.0)
        m_accumulated += seconds;
}
