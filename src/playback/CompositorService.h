#pragma once

#include "engine/FrameCompositor.h"
#include "engine/GpuCompositor.h"

#include <QObject>
#include <QThread>

#include <array>
#include <atomic>
#include <memory>

class PlaybackStats;

// What one composite cost, measured inside the worker that ran it. Reported with the frame
// rather than timed from the service, because the service cannot tell queue wait from work
// once more than one composite is in flight.
struct CompositeTiming
{
    double compositeMs = 0.0;  // wall time in the worker
    double decodeWaitMs = 0.0; // of which, blocked on a decoder
};

class CompositorWorker : public QObject
{
    Q_OBJECT

public:
    explicit CompositorWorker(QObject *parent = nullptr);

public slots:
    // Shared immutable snapshot taken on the GUI thread. The worker must never
    // hold a pointer into the live project: compositing runs concurrently with
    // editing, and reading a QMap/QList while the GUI thread rebalances it is a
    // use-after-free. Use Project::detachedCopy() so Qt COW payloads are unique,
    // then share that snapshot via shared_ptr across queued invokes.
    //
    // `sequence` is the service's request counter, returned untouched. With more than one
    // composite running, completion order is not request order, and the sequence is how the
    // service tells a frame that superseded what is on screen from one that has been overtaken.
    void composite(drift::TimeUs timeUs, FrameCompositor::RenderOptions options,
                   std::shared_ptr<const drift::Project> snapshot, quint64 sequence);

signals:
    void frameReady(const GpuFrameTexture &frame, drift::TimeUs timeUs, CompositeTiming timing,
                    quint64 sequence);

private:
    FrameCompositor m_compositor;
    std::shared_ptr<const drift::Project> m_snapshot;
};

// Background compositor threads. Frames are delivered to the GUI as live GL textures out of
// the runtime's presentation ring — never read back to the CPU and never re-uploaded. The
// ring is what keeps the scene graph from sampling a target that is being drawn into, and
// its depth is therefore what caps how many composites may run at once.
//
// During playback up to GpuCompositor::kMaxPreviewComposites run concurrently. That is stage
// parallelism, not throughput: GL work cannot overlap GL work, because every composite funnels
// through the one GL thread. What it does overlap is one frame's wait on a decoder with
// another frame's GL work, which is where the pipeline previously sat idle. Outside playback
// the depth drops back to one, so scrubbing and seeking behave exactly as before.
class CompositorService : public QObject
{
    Q_OBJECT

public:
    explicit CompositorService(QObject *parent = nullptr);
    ~CompositorService() override;

    void setProject(const drift::Project *project);
    void requestComposite(drift::TimeUs timeUs,
                          FrameCompositor::RenderOptions options = FrameCompositor::RenderOptions{});

    // Fast playback (the default) drops frames that finish after the playhead has
    // moved on. Turn this off to present every rendered frame, however long it takes.
    void setDropLateFrames(bool drop);

    // Automatic preview quality. Off — the default — every frame renders at exactly
    // the previewScale the caller asked for (Full/Half/Quarter stay put). On, the
    // service walks that scale down while composites overrun their frame budget
    // and back up once they stop, which only ever happens during fast-mode playback.
    void setAdaptiveQuality(bool enabled);

    // Realtime playback running. Adaptation measures playback frames only — paused
    // and scrubbed frames have no deadline to miss — and ignores the first few
    // frames of each run, which pay for opening and seeking the decoders.
    void setPlaybackActive(bool active);

    // Wall-clock budget for one composite before it counts against the adaptive
    // scale. Callers derive it from the display cadence so it tracks project fps
    // and playback rate rather than assuming 30 fps at 1x.
    void setLateFrameBudgetMs(int ms);

    // Multiplier applied on top of the caller's previewScale while adaptive
    // quality is on (1.0 = full requested quality).
    double adaptiveScaleFactor() const;

    // Optional counter sink, owned by the caller. Everything reported here happens on the
    // GUI thread, so the stats block needs no locking of its own.
    void setStats(PlaybackStats *stats) { m_stats = stats; }

signals:
    void frameReady(const GpuFrameTexture &frame);
    // One per completed request, whether or not a frame was produced or shown.
    void compositeFinished();

private slots:
    void onWorkerFrameReady(const GpuFrameTexture &frame, drift::TimeUs timeUs,
                            CompositeTiming timing, quint64 sequence);

private:
    void dispatch(drift::TimeUs timeUs, const FrameCompositor::RenderOptions &options);
    // Starts the newest pending request if it differs from the last one dispatched and there
    // is room in flight. Returns whether it dispatched.
    bool dispatchPending();
    int maxInFlight() const;
    void noteFrameLate(bool late);
    void resetAdaptiveState();
    FrameCompositor::RenderOptions effectiveOptions(FrameCompositor::RenderOptions options) const;

    const drift::Project *m_project = nullptr;
    // Reused across ticks until the live project pointer changes or the GUI
    // asks for a fresh snapshot after edits (generation bump via invalidateSnapshot).
    std::shared_ptr<const drift::Project> m_sharedSnapshot;
    int m_snapshotGeneration = 0;
    int m_liveGeneration = 0;

    // Requests and completions are both GUI-thread only (every requestComposite caller is
    // PlaybackEngine, and onWorkerFrameReady is a queued slot), so this needs no atomic.
    int m_inFlight = 0;
    quint64 m_nextSequence = 1;
    // Highest sequence already put on screen. A completion below it has been overtaken —
    // either it finished out of order, or the playhead moved past it while it rendered — and
    // showing it would step the picture backwards. This replaces the old wall-clock staleness
    // test, which threw away finished frames that had nothing newer to replace them with.
    quint64 m_lastPresentedSequence = 0;
    std::atomic<drift::TimeUs> m_pendingTimeUs{0};
    std::atomic<int> m_pendingPreviewScalePercent{100};
    std::atomic<int> m_pendingMaxTimeEchoHistoryFrames{-1};
    std::atomic<drift::TimeUs> m_pendingReadAheadUs{0};
    drift::TimeUs m_lastDispatchedTimeUs = -1;
    // The scale the caller asked for, before any adaptive multiplier — that is
    // applied at dispatch, so a change takes effect on the very next frame.
    FrameCompositor::RenderOptions m_lastDispatchedOptions;
    // Adaptive quality: consecutive on-time frames recover; late frames drop scale.
    int m_lateStreak = 0;
    int m_onTimeStreak = 0;
    int m_warmupFramesLeft = 0;
    int m_lateFrameBudgetMs = 100;
    double m_adaptiveScale = 1.0;
    bool m_adaptiveQuality = false;
    bool m_playbackActive = false;
    bool m_dropLateFrames = true;

    PlaybackStats *m_stats = nullptr;

    // One thread per possible in-flight composite. Each worker owns its own FrameCompositor,
    // which is safe because that class holds nothing but a project pointer and every cache it
    // reaches is either mutex-guarded (the still-image cache) or serialised on the GL thread.
    std::array<QThread, GpuCompositor::kMaxPreviewComposites> m_threads;
    std::array<CompositorWorker *, GpuCompositor::kMaxPreviewComposites> m_workers{};
    int m_nextWorker = 0;

public:
    // Call when the live project has been mutated so the next dispatch recopies.
    void invalidateSnapshot();
};

Q_DECLARE_METATYPE(GpuFrameTexture)
Q_DECLARE_METATYPE(CompositeTiming)
Q_DECLARE_METATYPE(std::shared_ptr<const drift::Project>)
