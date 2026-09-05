#pragma once

#include "core/Time.h"

#include <QMutex>
#include <atomic>
#include <chrono>

// Timeline clock for audio-master playback. Two positions are tracked:
//   * the produce position advances as audio is mixed into the sink buffer and
//     tells the mixer which samples to generate next;
//   * the playback position is anchored to the sink's actually-played audio
//     (processedUSecs) and drives what the viewer sees/hears, so video stays in
//     sync with audio instead of leading it by the buffer depth.
class PlaybackClock
{
public:
    void reset(drift::TimeUs playheadUs, int sampleRate);
    void start();
    void pause();
    void stop();

    bool isRunning() const { return m_running.load(std::memory_order_acquire); }
    drift::TimeUs pausedAt() const { return m_pausedAtUs; }

    // How much timeline one second of sink output covers. The sink always runs at the project
    // sample rate; speed changes how far the timeline moves per rendered sample, and the retiming
    // that keeps the audio listenable happens above this class. Only set while stopped — the
    // positions below are derived from the total sample count, so changing it mid-run would
    // retroactively rescale everything already rendered.
    void setRate(double rate);
    double rate() const { return m_rate; }

    // Produce side: where the mixer should generate the next buffer.
    drift::TimeUs produceTimeUs() const;
    void onAudioSamplesRendered(int sampleCount);
    // Sink-domain position: rendered samples as elapsed time, without the rate applied. This is
    // what a post-mix retimer's output cursor advances by, which is real time, not timeline time.
    drift::TimeUs renderedFramesUs() const;

    // Playback side: the position currently audible, wall-interpolated between
    // sink updates for smoothness. This is the timeline's visible playhead.
    void syncPlaybackUs(drift::TimeUs playedUs);
    drift::TimeUs currentTimeUs() const;

    // Steady-clock nanoseconds. Public because the engine measures display cadence against
    // the same clock the playhead is interpolated on; comparing the two across different
    // clock sources would be meaningless.
    static qint64 nowNs();

private:

    drift::TimeUs m_startPlayheadUs = 0;
    drift::TimeUs m_pausedAtUs = 0;
    std::atomic<int64_t> m_audioSamplesRendered{0};
    std::atomic<bool> m_running{false};
    int m_sampleRate = 48000;
    double m_rate = 1.0;

    // Playback anchor (guarded together so time never tears across the two).
    mutable QMutex m_anchorMutex;
    drift::TimeUs m_anchorPlayedUs = 0;
    qint64 m_anchorWallNs = 0; // 0 until the first sink sync arrives
    qint64 m_startWallNs = 0;  // wall time at start(), for the pre-sync fallback
    mutable drift::TimeUs m_lastReportedUs = 0; // monotonic guard on the visible playhead
};
