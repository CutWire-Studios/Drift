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
        qRegisterMetaType<quintptr>("quintptr");
        qRegisterMetaType<QList<quint64>>("QList<quint64>");
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
    constexpr auto kInterval = std::chrono::seconds(5);
    std::unique_lock<std::mutex> lock(m_sweepMutex);
    while (!m_sweepCv.wait_for(lock, kInterval, [this] { return m_sweepStop; })) {
        lock.unlock();
        sweepIdleWorkersOnce();
        lock.lock();
    }
}

void ClipReaderPool::sweepIdleWorkersOnce()
{
    const qint64 idleMs = kIdleReleaseMs;
    std::vector<std::unique_ptr<WorkerEntry>> evicted;
    {
        QMutexLocker lock(&m_mutex);
        evicted = detachIdleLocked(
            m_videoWorkers,
            [this](const VideoKey &key, const WorkerEntry &entry) { return keepVideoWorkerLocked(key, entry); },
            idleMs);
        std::vector<std::unique_ptr<WorkerEntry>> audio = detachIdleLocked(
            m_audioWorkers,
            [this](const QString &path, const WorkerEntry &) { return m_activeAudioPaths.contains(path); },
            idleMs);
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

std::unique_ptr<ClipReaderPool::WorkerEntry> ClipReaderPool::startWorker(const QString &path,
                                                                        QThread::Priority priority)
{
    auto entry = std::make_unique<WorkerEntry>();
    entry->thread = std::make_unique<QThread>();
    entry->worker = new ClipReaderWorker;
    entry->worker->moveToThread(entry->thread.get());
    entry->thread->start(priority);

    // Open asynchronously. Callers that need frames/audio use BlockingQueued
    // decode methods, which run after this open on the worker's event queue —
    // so the GUI/audio threads are never stuck inside avformat_find_stream_info
    // while holding the pool mutex (multi-hour files make that open very slow).
    QMetaObject::invokeMethod(entry->worker, "openPath", Qt::QueuedConnection, Q_ARG(QString, path));
    return entry;
}

ClipReaderPool::WorkerEntry &ClipReaderPool::ensureVideoWorker(const QString &path, quint64 streamId,
                                                               quintptr session)
{
    const VideoKey key{path, streamId};
    auto it = m_videoWorkers.find(key);
    if (it == m_videoWorkers.end() && session != 0) {
        const auto sessionIt = m_activeVideoStreams.constFind(session);
        if (sessionIt != m_activeVideoStreams.cend() && sessionIt->contains(path)) {
            const QList<quint64> active = sessionIt->value(path);
            auto spare = m_videoWorkers.end();
            for (auto entry = m_videoWorkers.lower_bound(VideoKey{path, 0});
                 entry != m_videoWorkers.end() && entry->first.first == path; ++entry) {
                if (entry->second->session != session || entry->second->inFlight > 0
                    || active.contains(entry->first.second))
                    continue;
                if (spare == m_videoWorkers.end()
                    || entry->second->lastUse.elapsed() > spare->second->lastUse.elapsed())
                    spare = entry;
            }
            if (spare != m_videoWorkers.end()) {
                auto node = m_videoWorkers.extract(spare);
                node.key() = key;
                it = m_videoWorkers.insert(std::move(node)).position;
                // The worker hands its reader to the new stream id only once it knows the old one
                // left this session's set (see ClipReaderWorker::readerFor).
                QMetaObject::invokeMethod(it->second->worker, "setActiveStreams", Qt::QueuedConnection,
                                          Q_ARG(quintptr, session), Q_ARG(QList<quint64>, active));
            }
        }
    }
    if (it == m_videoWorkers.end())
        it = m_videoWorkers.emplace(key, startWorker(path, QThread::InheritPriority)).first;

    it->second->lastUse.start();
    if (session != 0)
        it->second->session = session;
    return *it->second;
}

ClipReaderPool::WorkerEntry &ClipReaderPool::ensureAudioWorker(const QString &path)
{
    auto it = m_audioWorkers.find(path);
    // The audio thread blocks on its decode workers every buffer, so those run above the
    // video ones, which are paced by read-ahead and have slack to spare.
    if (it == m_audioWorkers.end())
        it = m_audioWorkers.emplace(path, startWorker(path, QThread::HighPriority)).first;

    it->second->lastUse.start();
    return *it->second;
}

bool ClipReaderPool::keepVideoWorkerLocked(const VideoKey &key, const WorkerEntry &entry) const
{
    return m_activeVideoPaths.contains(key.first)
        && m_activeVideoStreams.value(entry.session).value(key.first).contains(key.second);
}

// Only unlinks the evictable workers; the caller tears them down after dropping the lock.
// stopWorkerEntry blocks twice — a BlockingQueuedConnection into the worker and then a thread join —
// and running that under m_mutex would stall every other reader for its duration, including the
// audio thread inside readAudioInterleaved. A worker with inFlight > 0 is being read right now and
// is never taken: that is what makes the raw WorkerEntry* those readers hold across the unlocked
// decode safe.
template<typename Map, typename Keep>
std::vector<std::unique_ptr<ClipReaderPool::WorkerEntry>> ClipReaderPool::detachIdleLocked(
    Map &workers, Keep keep, qint64 minIdleMs)
{
    std::vector<std::unique_ptr<WorkerEntry>> evicted;
    for (auto it = workers.begin(); it != workers.end();) {
        WorkerEntry &entry = *it->second;
        if (keep(it->first, entry) || entry.inFlight > 0 || entry.lastUse.elapsed() < minIdleMs) {
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
        const auto keepNone = [](const auto &, const WorkerEntry &) { return false; };
        evicted = detachIdleLocked(m_videoWorkers, keepNone, 0);
        std::vector<std::unique_ptr<WorkerEntry>> audio = detachIdleLocked(m_audioWorkers, keepNone, 0);
        evicted.insert(evicted.end(), std::make_move_iterator(audio.begin()),
                       std::make_move_iterator(audio.end()));
    }
    for (const std::unique_ptr<WorkerEntry> &entry : evicted)
        stopWorkerEntry(*entry);
}

void ClipReaderPool::releasePath(const QString &path)
{
    std::vector<std::unique_ptr<WorkerEntry>> evicted;
    {
        QMutexLocker lock(&m_mutex);
        evicted = detachIdleLocked(
            m_videoWorkers, [&](const VideoKey &key, const WorkerEntry &) { return key.first != path; }, 0);
        std::vector<std::unique_ptr<WorkerEntry>> audio = detachIdleLocked(
            m_audioWorkers, [&](const QString &key, const WorkerEntry &) { return key != path; }, 0);
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

namespace {
thread_local bool t_approximateSeek = false;
} // namespace

ClipReaderPool::ApproximateSeekScope::ApproximateSeekScope(bool approximate)
    : previous(t_approximateSeek)
{
    t_approximateSeek = approximate;
}

ClipReaderPool::ApproximateSeekScope::~ApproximateSeekScope()
{
    t_approximateSeek = previous;
}

void ClipReaderPool::warmVideoFrames(const QList<VideoRequest> &requests)
{
    const bool approximate = t_approximateSeek;
    const quintptr session = quintptr(QThread::currentThread());
    // Tell each path's worker which of its streams this frame reads before any read arrives,
    // so a clip coming on screen can take over a reader the previous one left (see readerFor).
    QHash<QString, QList<quint64>> streamsByPath;
    for (const VideoRequest &request : requests) {
        if (!request.path.isEmpty())
            streamsByPath[request.path].append(request.streamId);
    }
    // The posts happen under the pool mutex: they do not block, and holding the lock is what stops
    // the idle release from deleting a worker between resolving it and posting to it.
    QMutexLocker lock(&m_mutex);
    m_activeVideoStreams.insert(session, streamsByPath);
    for (const VideoRequest &request : requests) {
        if (request.path.isEmpty())
            continue;

        ClipReaderWorker *worker = ensureVideoWorker(request.path, request.streamId, session).worker;
        QMetaObject::invokeMethod(worker, "setActiveStreams", Qt::QueuedConnection,
                                  Q_ARG(quintptr, session),
                                  Q_ARG(QList<quint64>, streamsByPath.value(request.path)));
        // Prefer the preview decode path so warm hits the same cache as composite.
        QMetaObject::invokeMethod(worker, "decodePreviewVideo",
                                  Qt::QueuedConnection,
                                  Q_ARG(quint64, request.streamId), Q_ARG(drift::TimeUs, request.sourceUs),
                                  Q_ARG(int, request.maxWidth), Q_ARG(int, request.maxHeight),
                                  Q_ARG(QString, QString()), Q_ARG(int, 15), Q_ARG(bool, false),
                                  Q_ARG(int, request.rotationCorrection), Q_ARG(bool, approximate),
                                  Q_ARG(quintptr, session));
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
        entry = &ensureVideoWorker(path, streamId, 0);
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
        entry = &ensureVideoWorker(path, streamId, quintptr(QThread::currentThread()));
        ++entry->inFlight;
    }
    ClipReaderWorker *worker = entry->worker;

    const bool approximate = t_approximateSeek;
    PreviewVideoFrame frame;
    QElapsedTimer decodeWait;
    decodeWait.start();
    QMetaObject::invokeMethod(worker, "decodePreviewVideo", Qt::BlockingQueuedConnection,
                               Q_RETURN_ARG(PreviewVideoFrame, frame), Q_ARG(quint64, streamId),
                               Q_ARG(drift::TimeUs, sourceUs), Q_ARG(int, maxWidth),
                               Q_ARG(int, maxHeight),
                               Q_ARG(QString, stabilizePath), Q_ARG(int, stabilizeSmoothing),
                               Q_ARG(bool, stabilizeTripod), Q_ARG(int, rotationCorrection),
                               Q_ARG(bool, approximate), Q_ARG(quintptr, quintptr(QThread::currentThread())));
    t_decodeWaitNs += decodeWait.nsecsElapsed();

    // A scrub's next request is somewhere else entirely; a frame decoded ahead of this one would
    // only sit in the worker's queue in front of it.
    if (!approximate)
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
        entry = &ensureAudioWorker(path);
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
    QMutexLocker lock(&m_mutex);
    m_activeVideoPaths = videoPaths;
    m_activeAudioPaths = audioPaths;
    for (const QString &path : audioPaths)
        ensureAudioWorker(path);
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
