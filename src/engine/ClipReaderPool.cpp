#include "ClipReaderPool.h"

#include "ClipReaderWorker.h"

#include <QMetaObject>
#include <QMetaType>

#include <iterator>

ClipReaderPool &ClipReaderPool::instance()
{
    static ClipReaderPool pool;
    static bool registered = false;
    if (!registered) {
        qRegisterMetaType<drift::TimeUs>("drift::TimeUs");
        qRegisterMetaType<PreviewVideoFrame>("PreviewVideoFrame");
        registered = true;
    }
    return pool;
}

ClipReaderPool::ClipReaderPool()
{
    m_idleSweepThread = std::thread([this] { idleSweepLoop(); });
}

ClipReaderPool::~ClipReaderPool()
{
    {
        std::lock_guard<std::mutex> sweepLock(m_sweepMutex);
        m_sweepStop = true;
    }
    m_sweepCv.notify_all();
    if (m_idleSweepThread.joinable())
        m_idleSweepThread.join();

    QMutexLocker lock(&m_mutex);
    for (auto &entry : m_videoWorkers)
        stopWorkerEntry(*entry.second);
    for (auto &entry : m_audioWorkers)
        stopWorkerEntry(*entry.second);
    m_videoWorkers.clear();
    m_audioWorkers.clear();
}

// Wakes on its own cadence rather than piggybacking on any caller, which is the whole point:
// FrameCompositor::prepare (retainActivePaths' only caller) simply stops running while the
// project is paused, and a one-shot reader opened just before that would otherwise never be
// swept. The interval only needs to be fine-grained relative to the idle budget it is checking.
void ClipReaderPool::idleSweepLoop()
{
#ifdef Q_OS_ANDROID
    constexpr auto kInterval = std::chrono::seconds(5);
#else
    constexpr auto kInterval = std::chrono::seconds(30);
#endif
    std::unique_lock<std::mutex> lock(m_sweepMutex);
    while (!m_sweepCv.wait_for(lock, kInterval, [this] { return m_sweepStop; })) {
        lock.unlock();
        sweepIdleWorkersOnce();
        lock.lock();
    }
}

void ClipReaderPool::sweepIdleWorkersOnce()
{
#ifdef Q_OS_ANDROID
    const qint64 idleMs = kIdleReleaseMs;
#else
    const qint64 idleMs = kDesktopIdleReleaseMs;
#endif
    std::vector<std::unique_ptr<WorkerEntry>> evicted;
    {
        QMutexLocker lock(&m_mutex);
        evicted = detachIdleLocked(m_videoWorkers, {}, idleMs);
        std::vector<std::unique_ptr<WorkerEntry>> audio = detachIdleLocked(m_audioWorkers, {}, idleMs);
        evicted.insert(evicted.end(), std::make_move_iterator(audio.begin()),
                       std::make_move_iterator(audio.end()));
    }
    for (const std::unique_ptr<WorkerEntry> &entry : evicted)
        stopWorkerEntry(*entry);
}

void ClipReaderPool::stopWorkerEntry(WorkerEntry &entry)
{
    if (!entry.thread)
        return;

    if (entry.worker) {
        QMetaObject::invokeMethod(entry.worker, "closePath", Qt::BlockingQueuedConnection);
    }

    entry.thread->quit();
    entry.thread->wait();
    delete entry.worker;
    entry.worker = nullptr;
    entry.thread.reset();
}

ClipReaderPool::WorkerEntry &ClipReaderPool::ensureWorker(
    std::map<QString, std::unique_ptr<WorkerEntry>> &workers, const QString &path)
{
    auto it = workers.find(path);
    if (it == workers.end()) {
        auto entry = std::make_unique<WorkerEntry>();
        entry->thread = std::make_unique<QThread>();
        entry->worker = new ClipReaderWorker;
        entry->worker->moveToThread(entry->thread.get());
        // The audio thread blocks on its decode workers every buffer, so those run above the
        // video ones, which are paced by read-ahead and have slack to spare.
        entry->thread->start(&workers == &m_audioWorkers ? QThread::HighPriority
                                                         : QThread::InheritPriority);

        // Open asynchronously. Callers that need frames/audio use BlockingQueued
        // decode methods, which run after this open on the worker's event queue —
        // so the GUI/audio threads are never stuck inside avformat_find_stream_info
        // while holding the pool mutex (multi-hour files make that open very slow).
        QMetaObject::invokeMethod(entry->worker, "openPath", Qt::QueuedConnection, Q_ARG(QString, path));
        it = workers.emplace(path, std::move(entry)).first;
    }

    it->second->lastUse.start();
    return *it->second;
}

// Only unlinks the evictable workers; the caller tears them down after dropping the lock.
// stopWorkerEntry blocks twice — a BlockingQueuedConnection into the worker and then a thread join —
// and running that under m_mutex would stall every other reader for its duration, including the
// audio thread inside readAudioInterleaved. A worker with inFlight > 0 is being read right now and
// is never taken: that is what makes the raw WorkerEntry* those readers hold across the unlocked
// decode safe.
std::vector<std::unique_ptr<ClipReaderPool::WorkerEntry>> ClipReaderPool::detachIdleLocked(
    std::map<QString, std::unique_ptr<WorkerEntry>> &workers, const QSet<QString> &keep,
    qint64 minIdleMs)
{
    std::vector<std::unique_ptr<WorkerEntry>> evicted;
    for (auto it = workers.begin(); it != workers.end();) {
        WorkerEntry &entry = *it->second;
        if (keep.contains(it->first) || entry.inFlight > 0 || entry.lastUse.elapsed() < minIdleMs) {
            ++it;
            continue;
        }
        evicted.push_back(std::move(it->second));
        it = workers.erase(it);
    }
    return evicted;
}

void ClipReaderPool::releaseAll()
{
    std::vector<std::unique_ptr<WorkerEntry>> evicted;
    {
        QMutexLocker lock(&m_mutex);
        evicted = detachIdleLocked(m_videoWorkers, {}, 0);
        std::vector<std::unique_ptr<WorkerEntry>> audio = detachIdleLocked(m_audioWorkers, {}, 0);
        evicted.insert(evicted.end(), std::make_move_iterator(audio.begin()),
                       std::make_move_iterator(audio.end()));
    }
    for (const std::unique_ptr<WorkerEntry> &entry : evicted)
        stopWorkerEntry(*entry);
}

void ClipReaderPool::setReadAheadUs(drift::TimeUs readAheadUs)
{
    m_readAheadUs.store(qMax<drift::TimeUs>(0, readAheadUs), std::memory_order_relaxed);
}

void ClipReaderPool::resetVideoDecoders()
{
    QMutexLocker lock(&m_mutex);
    for (auto &entry : m_videoWorkers) {
        QMetaObject::invokeMethod(entry.second->worker, "resetVideoDecoders",
                                  Qt::BlockingQueuedConnection);
    }
}

void ClipReaderPool::setHardwareDecodeMode(ClipReader::HardwareDecodeMode mode,
                                           drift::hwaccel::Backend backend)
{
    ClipReader::setHardwareDecodeMode(mode, backend);
    resetVideoDecoders();
}

void ClipReaderPool::warmVideoFrames(const QList<VideoRequest> &requests)
{
    for (const VideoRequest &request : requests) {
        if (request.path.isEmpty())
            continue;

        // The post happens under the pool mutex: it does not block, and holding the lock is what
        // stops the idle release from deleting the worker between resolving it and posting to it.
        QMutexLocker lock(&m_mutex);
        // Prefer the preview decode path so warm hits the same cache as composite.
        QMetaObject::invokeMethod(ensureWorker(m_videoWorkers, request.path).worker, "decodePreviewVideo",
                                  Qt::QueuedConnection,
                                  Q_ARG(quint64, request.streamId), Q_ARG(drift::TimeUs, request.sourceUs),
                                  Q_ARG(int, request.maxWidth), Q_ARG(int, request.maxHeight),
                                  Q_ARG(QString, QString()), Q_ARG(int, 15), Q_ARG(bool, false),
                                  Q_ARG(int, request.rotationCorrection));
    }
}

namespace {
// Per-thread so two compositor workers building different frames each measure only their own
// blocking reads. Nanoseconds: a single 4K hardware readback can be well under a millisecond,
// and rounding those to 0 would hide exactly the cost this is here to find.
thread_local qint64 t_decodeWaitNs = 0;
} // namespace

void ClipReaderPool::resetDecodeWaitNs()
{
    t_decodeWaitNs = 0;
}

qint64 ClipReaderPool::decodeWaitNs()
{
    return t_decodeWaitNs;
}

QImage ClipReaderPool::readVideoFrame(const QString &path, quint64 streamId, drift::TimeUs sourceUs,
                                      int maxWidth, int maxHeight, const QString &stabilizePath,
                                      int stabilizeSmoothing, bool stabilizeTripod, int rotationCorrection)
{
    if (path.isEmpty())
        return {};

    WorkerEntry *entry = nullptr;
    {
        // Hold the pool mutex only to resolve the worker; releasing it before the
        // blocking decode lets audio and video (different workers) decode in
        // parallel instead of serializing on this lock. inFlight keeps the idle
        // release from destroying the entry while we hold its raw worker pointer.
        QMutexLocker lock(&m_mutex);
        entry = &ensureWorker(m_videoWorkers, path);
        ++entry->inFlight;
    }
    ClipReaderWorker *worker = entry->worker;

    QImage frame;
    QElapsedTimer decodeWait;
    decodeWait.start();
    QMetaObject::invokeMethod(worker, "decodeVideo", Qt::BlockingQueuedConnection, Q_RETURN_ARG(QImage, frame),
                               Q_ARG(quint64, streamId), Q_ARG(drift::TimeUs, sourceUs),
                               Q_ARG(int, maxWidth), Q_ARG(int, maxHeight),
                               Q_ARG(QString, stabilizePath), Q_ARG(int, stabilizeSmoothing), Q_ARG(bool, stabilizeTripod),
                               Q_ARG(int, rotationCorrection));
    t_decodeWaitNs += decodeWait.nsecsElapsed();

    // Decode one frame beyond the current position while the caller composites
    // this one. The reader knows the source frame duration; the old code guessed
    // a hardcoded 1/30 s, which missed on every clip that isn't 30 fps.
    QMetaObject::invokeMethod(worker, "prefetchNextVideo", Qt::QueuedConnection,
                              Q_ARG(quint64, streamId), Q_ARG(int, maxWidth), Q_ARG(int, maxHeight));

    QMutexLocker lock(&m_mutex);
    --entry->inFlight;
    return frame;
}

PreviewVideoFrame ClipReaderPool::readPreviewVideoFrame(const QString &path, quint64 streamId,
                                                        drift::TimeUs sourceUs, int maxWidth, int maxHeight,
                                                        const QString &stabilizePath,
                                                        int stabilizeSmoothing, bool stabilizeTripod,
                                                        int rotationCorrection)
{
    if (path.isEmpty())
        return {};

    WorkerEntry *entry = nullptr;
    {
        QMutexLocker lock(&m_mutex);
        entry = &ensureWorker(m_videoWorkers, path);
        ++entry->inFlight;
    }
    ClipReaderWorker *worker = entry->worker;

    PreviewVideoFrame frame;
    QElapsedTimer decodeWait;
    decodeWait.start();
    QMetaObject::invokeMethod(worker, "decodePreviewVideo", Qt::BlockingQueuedConnection,
                               Q_RETURN_ARG(PreviewVideoFrame, frame), Q_ARG(quint64, streamId),
                               Q_ARG(drift::TimeUs, sourceUs), Q_ARG(int, maxWidth),
                               Q_ARG(int, maxHeight),
                               Q_ARG(QString, stabilizePath), Q_ARG(int, stabilizeSmoothing),
                               Q_ARG(bool, stabilizeTripod), Q_ARG(int, rotationCorrection));
    t_decodeWaitNs += decodeWait.nsecsElapsed();

    worker->requestPrefetchPreview(streamId, maxWidth, maxHeight,
                                   m_readAheadUs.load(std::memory_order_relaxed));

    QMutexLocker lock(&m_mutex);
    --entry->inFlight;
    return frame;
}

int ClipReaderPool::readAudioInterleaved(const QString &path, quint64 streamId,
                                         drift::TimeUs sourceStartUs, int sampleCount,
                                         int outputSampleRate, float *interleavedStereoOut,
                                         int audioStreamOrdinal)
{
    if (path.isEmpty() || !interleavedStereoOut || sampleCount <= 0)
        return 0;

    WorkerEntry *entry = nullptr;
    {
        QMutexLocker lock(&m_mutex);
        entry = &ensureWorker(m_audioWorkers, path);
        ++entry->inFlight;
    }

    int written = 0;
    QMetaObject::invokeMethod(entry->worker, "decodeAudio", Qt::BlockingQueuedConnection,
                              Q_RETURN_ARG(int, written),
                              Q_ARG(quint64, streamId), Q_ARG(drift::TimeUs, sourceStartUs),
                              Q_ARG(int, sampleCount), Q_ARG(int, outputSampleRate),
                              Q_ARG(float *, interleavedStereoOut),
                              Q_ARG(int, audioStreamOrdinal));

    QMutexLocker lock(&m_mutex);
    --entry->inFlight;
    return written;
}

void ClipReaderPool::resetAudioStreams()
{
    QMutexLocker lock(&m_mutex);
    for (auto &entry : m_audioWorkers)
        entry.second->worker->requestAudioReposition();
}

void ClipReaderPool::retainActivePaths(const QSet<QString> &videoPaths, const QSet<QString> &audioPaths)
{
    // Idle eviction lives in idleSweepLoop() now, not here — see its declaration for why.
    QMutexLocker lock(&m_mutex);
    for (const QString &path : videoPaths)
        ensureWorker(m_videoWorkers, path);
    for (const QString &path : audioPaths)
        ensureWorker(m_audioWorkers, path);
}

namespace drift {

MediaCodecSurfaceDecodeBlock::MediaCodecSurfaceDecodeBlock()
{
#ifdef Q_OS_ANDROID
    ClipReader::setSurfaceDecodeAllowed(false);
    ClipReaderPool::instance().resetVideoDecoders();
#endif
}

MediaCodecSurfaceDecodeBlock::~MediaCodecSurfaceDecodeBlock()
{
#ifdef Q_OS_ANDROID
    ClipReader::setSurfaceDecodeAllowed(true);
    ClipReaderPool::instance().resetVideoDecoders();
#endif
}

} // namespace drift
