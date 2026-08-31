#include "ClipReaderWorker.h"

ClipReaderWorker::ClipReaderWorker(QObject *parent)
    : QObject(parent)
{
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
    m_lru.clear();
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
        it = m_readers.emplace(streamId, std::move(reader)).first;
    } else if (it->second->audioStreamOrdinal() != audioStreamOrdinal) {
        it->second->setAudioStreamOrdinal(audioStreamOrdinal);
    }

    std::erase(m_lru, streamId);
    m_lru.push_back(streamId);
    while (m_lru.size() > kMaxStreams) {
        m_readers.erase(m_lru.front());
        m_lru.erase(m_lru.begin());
    }

    // The read-ahead budget belongs to the path, not to any one reader on it.
    const int shares = static_cast<int>(m_readers.size());
    for (auto &entry : m_readers)
        entry.second->setPreviewCacheShare(shares);

    return it->second.get();
}

QImage ClipReaderWorker::decodeVideo(quint64 streamId, drift::TimeUs sourceUs, int maxWidth, int maxHeight,
                                     const QString &stabilizePath, int stabilizeSmoothing, bool stabilizeTripod)
{
    QMutexLocker lock(&m_mutex);
    ClipReader *reader = readerFor(streamId);
    if (reader)
        reader->setStabilizeParams(stabilizePath, stabilizeSmoothing, stabilizeTripod);
    QImage frame;
    if (!reader || !reader->readVideoFrameAt(sourceUs, frame, maxWidth, maxHeight))
        return {};
    return frame;
}

PreviewVideoFrame ClipReaderWorker::decodePreviewVideo(quint64 streamId, drift::TimeUs sourceUs,
                                                       int maxWidth, int maxHeight,
                                                       const QString &stabilizePath,
                                                       int stabilizeSmoothing, bool stabilizeTripod)
{
    QMutexLocker lock(&m_mutex);
    ClipReader *reader = readerFor(streamId);
    if (reader)
        reader->setStabilizeParams(stabilizePath, stabilizeSmoothing, stabilizeTripod);
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
            entry.second->invalidateAudioPosition();
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
            more = it->second->prefetchNextPreviewVideoFrame(maxWidth, maxHeight, readAheadUs);
    }

    if (more)
        requestPrefetchPreview(streamId, maxWidth, maxHeight, readAheadUs);
}

void ClipReaderWorker::resetVideoDecoders()
{
    QMutexLocker lock(&m_mutex);
    for (auto &entry : m_readers)
        entry.second->resetVideoDecoder();
}
