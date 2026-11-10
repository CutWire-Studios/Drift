#pragma once

#include "engine/compositor/FrameCompositor.h"
#include "engine/compositor/GpuCompositor.h"
#include "engine/compositor/PreviewStream.h"

#include <QHash>
#include <QObject>
#include <QSet>
#include <QThread>

#include <atomic>
#include <memory>

class PlaybackStats;
namespace drift {
class BakeReaderPool;
}

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
    void setBakeSource(std::shared_ptr<drift::BakeReaderPool> bakes);

signals:
    void frameReady(const GpuFrameTexture &frame, drift::TimeUs timeUs, CompositeTiming timing,
                    quint64 sequence, int stream);

private:
    FrameCompositor m_compositor;
    std::shared_ptr<const drift::Project> m_snapshot;
};

// Background compositor threads. Frames are delivered to the GUI as live GL textures out of
// the runtime's presentation ring — never read back to the CPU and never re-uploaded. The
// ring is what keeps the scene graph from sampling a target that is being drawn into, and
// its depth is therefore what caps how many composites may run at once.
//
// Preview composites run sequentially on one worker. This keeps video decoder sessions
// and their forward-read cursors stable across frames and across adjacent video clips.
// New playback requests replace the pending timestamp while a composite is running.
// Each PreviewStream has its own pending request; the worker serves the streams that have
// something to draw in turn, so a busy stream cannot starve the other.
class CompositorService : public QObject
{
    Q_OBJECT

public:
    explicit CompositorService(QObject *parent = nullptr);
    ~CompositorService() override;

    void setProject(const drift::Project *project);
    // The immutable copy of the project that composites are reading, made now if an edit
    // invalidated the last one. Shared with the audio thread so one edit costs one copy.
    std::shared_ptr<const drift::Project> snapshot();
    // Bumped each time snapshot() makes a new copy; never 0 for a real snapshot.
    quint64 snapshotSerial() const { return m_snapshotSerial; }
    void requestComposite(drift::TimeUs timeUs,
                          FrameCompositor::RenderOptions options = FrameCompositor::RenderOptions{})
    {
        requestComposite(PreviewStream::Camera, timeUs, std::move(options));
    }
    void requestComposite(PreviewStream stream, drift::TimeUs timeUs,
                          FrameCompositor::RenderOptions options);
    // Forget the stream's pending request, so it is not drawn again until requested anew.
    void cancelStream(PreviewStream stream);
    // The stream to serve after `after`: the next one in round-robin order that has something to
    // draw, or -1 when none does.
    static int nextStreamToServe(int after, const bool (&wantsDraw)[kPreviewStreamCount]);

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
    // Whether a request would redraw exactly what `last` already asked for. An approximate
    // request after an exact one of the same frame is redundant; an exact one after an
    // approximate one is not — it is the settle.
    static bool isRedundantRequest(drift::TimeUs time, const FrameCompositor::RenderOptions &options,
                                   drift::TimeUs lastTime,
                                   const FrameCompositor::RenderOptions &last);

    void setPlaybackActive(bool active);

    // Preview-only: effects whose clip is in clipIds and whose catalog id is in catalogIds are
    // skipped when the snapshot is taken. Nothing in the live project changes.
    void setPreviewBypass(const QSet<QString> &clipIds, const QSet<QString> &catalogIds);

    // Where baked frames come from. The pool's index may change at any time (it is thread safe);
    // call invalidateSnapshot() after it does so the current frame is drawn again.
    void setBakeSource(std::shared_ptr<drift::BakeReaderPool> bakes);

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
    void frameReady(int stream, const GpuFrameTexture &frame);

private slots:
    void onWorkerFrameReady(const GpuFrameTexture &frame, drift::TimeUs timeUs,
                            CompositeTiming timing, quint64 sequence, int stream);

private:
    void dispatch(drift::TimeUs timeUs, const FrameCompositor::RenderOptions &options);
    bool wantsDraw(int stream) const;
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
    quint64 m_snapshotSerial = 0;
    QSet<QString> m_bypassClipIds;
    QSet<QString> m_bypassCatalogIds;

    // Requests and completions are both GUI-thread only (every requestComposite caller is
    // PlaybackEngine, and onWorkerFrameReady is a queued slot), so this needs no atomic.
    int m_inFlight = 0;
    quint64 m_nextSequence = 1;
    // Highest sequence already put on screen. Completions older than this are overtaken;
    // playback also rejects frames lagging far behind the latest requested timestamp.
    quint64 m_lastPresentedSequence[kPreviewStreamCount] = {};
    // Dispatch time per sequence, for request-to-screen latency. Filled only under perf logging.
    QHash<quint64, qint64> m_perfDispatchNs;
    // The newest request, whole. Both writers (requestComposite and the catch-up dispatch in
    // onWorkerFrameReady) run on the GUI thread. It used to be four atomics, which silently
    // dropped every option they did not name — proxies and the inline-edit skip never reached
    // the worker.
    struct StreamSlot
    {
        bool requested = false;
        drift::TimeUs pendingTimeUs = 0;
        FrameCompositor::RenderOptions pendingOptions;
        drift::TimeUs lastDispatchedTimeUs = -1;
        // The scale the caller asked for, before any adaptive multiplier — that is
        // applied at dispatch, so a change takes effect on the very next frame.
        FrameCompositor::RenderOptions lastDispatchedOptions;
    };
    StreamSlot m_slots[kPreviewStreamCount];
    int m_lastServedStream = kPreviewStreamCount - 1;
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

    // Preview composites run on one persistent thread so decoder cursors survive cuts.
    // The GPU presentation ring retains its own independent capacity.
    QThread m_thread;
    CompositorWorker *m_worker = nullptr;

public:
    // Call when the live project has been mutated so the next dispatch recopies.
    void invalidateSnapshot();
};

Q_DECLARE_METATYPE(GpuFrameTexture)
Q_DECLARE_METATYPE(CompositeTiming)
Q_DECLARE_METATYPE(std::shared_ptr<drift::BakeReaderPool>)
Q_DECLARE_METATYPE(std::shared_ptr<const drift::Project>)
