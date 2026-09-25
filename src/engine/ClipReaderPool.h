#pragma once

#include "ClipReader.h"
#include "core/Time.h"

#include <QElapsedTimer>
#include <QHash>
#include <QImage>
#include <QMutex>
#include <QObject>
#include <QSet>
#include <QString>
#include <QThread>

#include <QList>

#include <atomic>
#include <condition_variable>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

class ClipReaderWorker;

// Threaded reader pool. Video gets a worker thread per (path, stream id): two clips cut from one
// file each decode on their own thread, so a clip opening and seeking its decoder — the incoming
// side of a transition, say — never stalls the other one's frames behind it. Audio keeps one worker
// per path, holding a decoder per stream id (see ClipReaderWorker).
class ClipReaderPool
{
public:
    static ClipReaderPool &instance();

    // A stream id is one caller's decode cursor on a path: one per timeline clip, per preview
    // player, per offline scan. Two callers sharing one is what made overlapping clips cut from a
    // single file desync their audio and reseek their video on every frame.
    static quint64 streamIdForClip(const QString &clipId) { return qHash(clipId); }

    struct VideoRequest
    {
        QString path;
        quint64 streamId = 0;
        drift::TimeUs sourceUs = 0;
        int maxWidth = 0;
        int maxHeight = 0;
        // Must match what the readVideoFrame/readPreviewVideoFrame that follows will pass: the
        // reader clears its frame caches whenever this changes, so a warm at one value and a read
        // at another would decode every frame twice and cache nothing.
        int rotationCorrection = 0;
    };

    // Kick every request off on its own worker thread without waiting. Each path
    // has its own thread, so the decodes run concurrently; the readVideoFrame
    // calls that follow then hit each reader's cache instead of decoding one
    // clip after another on the caller's thread.
    void warmVideoFrames(const QList<VideoRequest> &requests);

    // How far past the frame being composited each reader keeps decoding, in
    // source time. Set per composite from the render options; 0 (the default)
    // leaves the plain one-frame-ahead prefetch. Only the preview path
    // buffers — export and thumbnails consume as fast as they decode anyway.
    void setReadAheadUs(drift::TimeUs readAheadUs);

    // Time this thread has spent blocked inside a video decoder. The compositor zeroes it
    // before building a frame and reads it after, which attributes decode wait to that frame
    // without threading a timer down through every layer of the scene build. Per-thread, so
    // concurrent compositor workers do not pollute each other's figure.
    static void resetDecodeWaitNs();
    static qint64 decodeWaitNs();

    // Scrub mode for preview reads made on this thread while the scope lives (see
    // ClipReader::readPreviewVideoFrame's `approximate`). Per-thread for the same reason as the
    // decode wait: the compositor sets it around one frame, and export may be compositing
    // exactly at the same time on another thread.
    struct ApproximateSeekScope
    {
        explicit ApproximateSeekScope(bool approximate);
        ~ApproximateSeekScope();
        bool previous;
    };

    // Preview toolbar: Auto (per clip), Software, or Hardware on a named backend.
    // Drops every open video decoder so the next read opens on the chosen path.
    // Drop every open video decoder so the next read reopens on whatever the current decode
    // settings say. Blocking: it returns once every worker has actually let go.
    void resetVideoDecoders();

    void setHardwareDecodeMode(ClipReader::HardwareDecodeMode mode,
                               drift::hwaccel::Backend backend = drift::hwaccel::Backend::None);

    // `rotationCorrection` (Clip::rotationCorrection) lets a clip's orientation fix reach the
    // decoder losslessly.
    QImage readVideoFrame(const QString &path, quint64 streamId, drift::TimeUs sourceUs, int maxWidth,
                          int maxHeight, const QString &stabilizePath = QString(),
                          int stabilizeSmoothing = 15, bool stabilizeTripod = false,
                          int rotationCorrection = 0);
    // Preview path: AVFrame handle (hardware surfaces stay on the GPU). Empty when decode fails.
    PreviewVideoFrame readPreviewVideoFrame(const QString &path, quint64 streamId, drift::TimeUs sourceUs,
                                            int maxWidth, int maxHeight,
                                            const QString &stabilizePath = QString(),
                                            int stabilizeSmoothing = 15, bool stabilizeTripod = false,
                                            int rotationCorrection = 0);
    int readAudioInterleaved(const QString &path, quint64 streamId, drift::TimeUs sourceStartUs,
                             int sampleCount, int outputSampleRate, float *interleavedStereoOut,
                             int audioStreamOrdinal = 0);
    // Tell every audio reader its cursor is stale, so the next read seeks to the position asked
    // for. Called when the timeline playhead moves: a short forward seek looks like ordinary
    // playback to the sequential fast path, which would keep streaming from the old position.
    void resetAudioStreams();
    // Records the paths the current frame reads as the active set, which the background idle
    // sweep (see idleSweepLoop()) never evicts — for video, only the streams on those paths the
    // last warmVideoFrames read, so pausing keeps exactly the clips on screen. Everything else —
    // including one-shot reads for segmentation or face tracking, which never appear on the
    // timeline — is reclaimed once idle, even while the project is paused.
    void retainActivePaths(const QSet<QString> &videoPaths, const QSet<QString> &audioPaths);

    // Drop every worker that is not mid-decode, ignoring the idle gate. For the Android
    // application-state handler: backgrounding is the one moment where reopening every file later
    // is cheaper than holding the decoders. Callable from any thread except a worker thread.
    void releaseAll();

private:
    ClipReaderPool();
    ~ClipReaderPool();

    struct WorkerEntry
    {
        std::unique_ptr<QThread> thread;
        ClipReaderWorker *worker = nullptr;
        // Restarted by every ensureVideoWorker()/ensureAudioWorker(). The release below is gated on it so scrubbing a clip
        // in and out of the active set frame by frame does not tear the decoder down and reopen it.
        QElapsedTimer lastUse;
        // Callers currently inside a blocking decode, holding this entry's raw worker pointer with
        // the pool mutex released. Never destroy an entry while this is non-zero.
        int inFlight = 0;
        // Video only: compositor thread that last read through this worker, 0 for everything else.
        quintptr session = 0;
    };
    using VideoKey = std::pair<QString, quint64>;

    static void stopWorkerEntry(WorkerEntry &entry);
    static std::unique_ptr<WorkerEntry> startWorker(const QString &path, QThread::Priority priority);
    // Caller holds m_mutex. A stream new to a path takes over the least recently used worker that
    // `session` read on that path but no longer does: a file split into pieces keeps one decoder,
    // positioned where the last piece stopped, instead of opening one per piece. Only for a
    // session whose streams on this path warmVideoFrames recorded — without that, two clips it reads
    // alternately would take the worker from each other on every frame.
    WorkerEntry &ensureVideoWorker(const QString &path, quint64 streamId, quintptr session);
    WorkerEntry &ensureAudioWorker(const QString &path);
    // Caller holds m_mutex. Erases every entry outside `keep` that has been untouched for at least
    // minIdleMs and has no decode in flight, and hands the owning pointers back so the caller can
    // stop them once the lock is released — stopWorkerEntry joins a thread and must not run under
    // m_mutex.
    template<typename Map, typename Keep>
    static std::vector<std::unique_ptr<WorkerEntry>> detachIdleLocked(Map &workers, Keep keep,
                                                                      qint64 minIdleMs);
    bool keepVideoWorkerLocked(const VideoKey &key, const WorkerEntry &entry) const;

    // Runs on its own thread for the pool's lifetime so idle workers are reclaimed even when
    // nothing calls retainActivePaths for a while (its only caller is FrameCompositor::prepare,
    // which stops running entirely while the project is paused). The last active set is kept, so
    // pausing on a clip never closes the readers under the playhead.
    void idleSweepLoop();
    void sweepIdleWorkersOnce();

    // Desktop too: a 4K decoder holds a few hundred MB of frames or GPU surfaces, and a timeline
    // cut from a dozen files kept every one of them open for as long as it looped.
    static constexpr qint64 kIdleReleaseMs = 10'000;

    QMutex m_mutex;
    std::atomic<drift::TimeUs> m_readAheadUs{0};
    std::map<VideoKey, std::unique_ptr<WorkerEntry>> m_videoWorkers;
    std::map<QString, std::unique_ptr<WorkerEntry>> m_audioWorkers;
    QSet<QString> m_activeVideoPaths;
    // Per session, the streams on each path its last warmVideoFrames read.
    QHash<quintptr, QHash<QString, QList<quint64>>> m_activeVideoStreams;
    QSet<QString> m_activeAudioPaths;

    std::thread m_idleSweepThread;
    std::mutex m_sweepMutex;
    std::condition_variable m_sweepCv;
    bool m_sweepStop = false;
};

// Blocks MediaCodec surface decoding for its lifetime, and resets every open video decoder on the
// way in and out so a reader opened in surface mode for the preview is not reused underneath it.
// Export scopes one of these around the whole encode: a surface frame's YUV->RGB is done by the
// driver from the buffer's own dataspace, which is not necessarily the matrix the desktop
// compositor uses, and an export has to match. A no-op off Android.
namespace drift {
class MediaCodecSurfaceDecodeBlock
{
public:
    MediaCodecSurfaceDecodeBlock();
    ~MediaCodecSurfaceDecodeBlock();

    MediaCodecSurfaceDecodeBlock(const MediaCodecSurfaceDecodeBlock &) = delete;
    MediaCodecSurfaceDecodeBlock &operator=(const MediaCodecSurfaceDecodeBlock &) = delete;
};
} // namespace drift
