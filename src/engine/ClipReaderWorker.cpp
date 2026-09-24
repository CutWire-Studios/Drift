#include "ClipReaderWorker.h"

#include <algorithm>

ClipReaderWorker::ClipReaderWorker(QObject *parent)
    : QObject(parent)
{
    m_clock.start();
}

void ClipReaderWorker::openPath(const QString &path)
{
    QMutexLocker lock(&m_mutex);
    m_path = path;
}

void ClipReaderWorker::closePath()
{
    QMutexLocker lock(&m_mutex);
    m_readers.clear();
    QMutexLocker prefetchLock(&m_prefetchMutex);
    m_prefetchPending.clear();
}

// Runs on the worker thread, so opening and closing readers here never blocks a decode request's
// caller — the audio callback or the compositor — on an avformat operation.
ClipReader *ClipReaderWorker::readerFor(quint64 streamId, int audioStreamOrdinal)
{
    auto it = m_readers.find(streamId);
    if (it == m_readers.end()) {
        if (m_path.isEmpty())
            return nullptr;
        auto reader = std::make_unique<ClipReader>();
        if (!reader->open(m_path, audioStreamOrdinal))
            return nullptr;
        it = m_readers.emplace(streamId, ReaderEntry{std::move(reader)}).first;
    } else if (it->second.reader->audioStreamOrdinal() != audioStreamOrdinal) {
        it->second.reader->setAudioStreamOrdinal(audioStreamOrdinal);
    }

    const qint64 now = m_clock.elapsed();
    it->second.lastUseMs = now;

    size_t active = 0;
    for (auto entry = m_readers.begin(); entry != m_readers.end();) {
        const qint64 idleMs = now - entry->second.lastUseMs;
        if (idleMs >= kIdleEvictMs) {
            entry = m_readers.erase(entry);
            continue;
        }
        if (idleMs < kActiveWindowMs)
            ++active;
        ++entry;
    }
    // The one just used is active, so it is never the least recently used while over the cap.
    const size_t cap = std::max(kMinStreams, active + 1);
    while (m_readers.size() > cap) {
        m_readers.erase(std::min_element(m_readers.begin(), m_readers.end(),
                                         [](const auto &a, const auto &b) {
                                             return a.second.lastUseMs < b.second.lastUseMs;
                                         }));
    }

    // The read-ahead budget belongs to the path, not to any one reader on it.
    const int shares = static_cast<int>(m_readers.size());
    for (auto &entry : m_readers)
        entry.second.reader->setPreviewCacheShare(shares);

    return it->second.reader.get();
}

QImage ClipReaderWorker::decodeVideo(quint64 streamId, drift::TimeUs sourceUs, int maxWidth, int maxHeight,
                                     const QString &stabilizePath, int stabilizeSmoothing, bool stabilizeTripod,
                                     int rotationCorrection)
{
    QMutexLocker lock(&m_mutex);
    ClipReader *reader = readerFor(streamId);
    if (reader) {
        reader->setStabilizeParams(stabilizePath, stabilizeSmoothing, stabilizeTripod);
        reader->setRotationCorrection(rotationCorrection);
    }
    QImage frame;
    if (!reader || !reader->readVideoFrameAt(sourceUs, frame, maxWidth, maxHeight))
        return {};
    return frame;
}

PreviewVideoFrame ClipReaderWorker::decodePreviewVideo(quint64 streamId, drift::TimeUs sourceUs,
                                                       int maxWidth, int maxHeight,
                                                       const QString &stabilizePath,
                                                       int stabilizeSmoothing, bool stabilizeTripod,
                                                       int rotationCorrection)
{
    QMutexLocker lock(&m_mutex);
    ClipReader *reader = readerFor(streamId);
    if (reader) {
        reader->setStabilizeParams(stabilizePath, stabilizeSmoothing, stabilizeTripod);
        reader->setRotationCorrection(rotationCorrection);
    }
    PreviewVideoFrame frame;
    if (!reader || !reader->readPreviewVideoFrame(sourceUs, frame, maxWidth, maxHeight))
        return {};
    return frame;
}

int ClipReaderWorker::decodeAudio(quint64 streamId, drift::TimeUs sourceStartUs, int sampleCount,
                                  int outputSampleRate, float *interleavedStereoOut, int audioStreamOrdinal)
{
    QMutexLocker lock(&m_mutex);

    if (m_audioRepositionPending.fetchAndStoreAcquire(0) != 0) {
        for (auto &entry : m_readers)
            entry.second.reader->invalidateAudioPosition();
    }

    ClipReader *reader = readerFor(streamId, audioStreamOrdinal);
    if (!reader)
        return 0;
    return reader->readAudioInterleaved(sourceStartUs, sampleCount, outputSampleRate,
                                        interleavedStereoOut);
}

void ClipReaderWorker::prefetchNextVideo(quint64 streamId, int maxWidth, int maxHeight)
{
    QMutexLocker lock(&m_mutex);
    if (ClipReader *reader = readerFor(streamId))
        reader->prefetchNextVideoFrame(maxWidth, maxHeight);
}

void ClipReaderWorker::requestPrefetchPreview(quint64 streamId, int maxWidth, int maxHeight,
                                              drift::TimeUs readAheadUs)
{
    {
        QMutexLocker lock(&m_prefetchMutex);
        if (m_prefetchPending.contains(streamId))
            return;
        m_prefetchPending.insert(streamId);
    }

    QMetaObject::invokeMethod(this, "prefetchNextPreviewVideo", Qt::QueuedConnection,
                              Q_ARG(quint64, streamId), Q_ARG(int, maxWidth), Q_ARG(int, maxHeight),
                              Q_ARG(drift::TimeUs, readAheadUs));
}

void ClipReaderWorker::prefetchNextPreviewVideo(quint64 streamId, int maxWidth, int maxHeight,
                                                drift::TimeUs readAheadUs)
{
    {
        QMutexLocker lock(&m_prefetchMutex);
        m_prefetchPending.remove(streamId);
    }

    bool more = false;
    {
        QMutexLocker lock(&m_mutex);
        auto it = m_readers.find(streamId);
        if (it != m_readers.end())
            more = it->second.reader->prefetchNextPreviewVideoFrame(maxWidth, maxHeight, readAheadUs);
    }

    if (more)
        requestPrefetchPreview(streamId, maxWidth, maxHeight, readAheadUs);
}

void ClipReaderWorker::resetVideoDecoders()
{
    QMutexLocker lock(&m_mutex);
    for (auto &entry : m_readers)
        entry.second.reader->resetVideoDecoder();
}
