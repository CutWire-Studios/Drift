#pragma once

#include "ClipReader.h"

#include "core/Time.h"

#include <QAtomicInt>
#include <QImage>
#include <QMutex>
#include <QObject>
#include <QSet>
#include <QString>

#include <map>
#include <memory>
#include <vector>

// Owns the ClipReaders for one media path on a dedicated thread; all decode calls are serialized
// here. Readers keep their own frame caches, so this class holds no cache of its own.
//
// There is one reader per stream id rather than one per path. A ClipReader carries a decode
// position, and both its audio and video fast paths assume the next request continues where the
// last one left off. Two clips cut from the same file and overlapping on the timeline break that
// assumption: they interleave requests at positions seconds apart. On the audio side the reader
// then served the second clip the first one's stream outright; on the video side it stayed correct
// but paid a keyframe seek and a GOP decode per frame, for every frame of the overlap.
class ClipReaderWorker : public QObject
{
    Q_OBJECT

public:
    explicit ClipReaderWorker(QObject *parent = nullptr);

    // Callable from any thread. Queues one read-ahead step for this stream if none is pending;
    // that step re-arms itself until the reader has readAheadUs of decoded source buffered.
    // Keeping a single step in flight per stream is what bounds a decode request's wait to one
    // frame — a queue of them would serialize ahead of it.
    void requestPrefetchPreview(quint64 streamId, int maxWidth, int maxHeight, drift::TimeUs readAheadUs);

    // Callable from any thread. Marks every audio reader here as unpositioned, so the next decode
    // seeks to the position it is asked for instead of continuing its stream. Set as a flag rather
    // than applied directly: the GUI thread raises it on seek while the audio thread may be mid
    // decode, and a blocking call across that boundary would stall the seek behind the decode.
    void requestAudioReposition() { m_audioRepositionPending.storeRelease(1); }

public slots:
    void openPath(const QString &path);
    void closePath();
    QImage decodeVideo(quint64 streamId, drift::TimeUs sourceUs, int maxWidth, int maxHeight,
                       const QString &stabilizePath = QString(), int stabilizeSmoothing = 15,
                       bool stabilizeTripod = false);
    PreviewVideoFrame decodePreviewVideo(quint64 streamId, drift::TimeUs sourceUs, int maxWidth,
                                         int maxHeight, const QString &stabilizePath = QString(),
                                         int stabilizeSmoothing = 15, bool stabilizeTripod = false);
    int decodeAudio(quint64 streamId, drift::TimeUs sourceStartUs, int sampleCount,
                    int outputSampleRate, float *interleavedStereoOut, int audioStreamOrdinal = 0);
    void prefetchNextVideo(quint64 streamId, int maxWidth, int maxHeight);
    void prefetchNextPreviewVideo(quint64 streamId, int maxWidth, int maxHeight,
                                  drift::TimeUs readAheadUs);
    void resetVideoDecoders();

private:
    // Only clips overlapping right now need concurrent readers, and that is a handful at most.
    // Past the cap the least recently used one is closed, which costs it a seek if it comes back.
    static constexpr size_t kMaxStreams = 4;

    // Call with m_mutex held.
    ClipReader *readerFor(quint64 streamId, int audioStreamOrdinal = 0);

    QString m_path;
    std::map<quint64, std::unique_ptr<ClipReader>> m_readers;
    std::vector<quint64> m_lru; // most recently used last
    QMutex m_mutex;

    QMutex m_prefetchMutex;
    QSet<quint64> m_prefetchPending;

    QAtomicInt m_audioRepositionPending{0};
};
