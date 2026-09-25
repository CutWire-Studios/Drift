#pragma once

#include "ClipReader.h"

#include "core/Time.h"

#include <QAtomicInt>
#include <QElapsedTimer>
#include <QHash>
#include <QImage>
#include <QMutex>
#include <QObject>
#include <QSet>
#include <QString>

#include <map>
#include <memory>

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
                       bool stabilizeTripod = false, int rotationCorrection = 0);
    PreviewVideoFrame decodePreviewVideo(quint64 streamId, drift::TimeUs sourceUs, int maxWidth,
                                         int maxHeight, const QString &stabilizePath = QString(),
                                         int stabilizeSmoothing = 15, bool stabilizeTripod = false,
                                         int rotationCorrection = 0, bool approximate = false,
                                         quintptr session = 0);
    // The streams on this path that `session` (one compositor thread) reads for the frame it is
    // building. A reader of that session outside the set is free to be handed to a new stream.
    void setActiveStreams(quintptr session, const QList<quint64> &streams);
    int decodeAudio(quint64 streamId, drift::TimeUs sourceStartUs, int sampleCount,
                    int outputSampleRate, float *interleavedStereoOut, int audioStreamOrdinal = 0);
    void prefetchNextVideo(quint64 streamId, int maxWidth, int maxHeight);
    void prefetchNextPreviewVideo(quint64 streamId, int maxWidth, int maxHeight,
                                  drift::TimeUs readAheadUs);
    void resetVideoDecoders();

private:
    // A reader nobody has asked for in this long belongs to a clip that is no longer playing. It
    // is closed, which frees its decoder and, for a hardware one, its surface pool; coming back
    // costs a reopen and a seek.
    static constexpr qint64 kIdleEvictMs = 10'000;
    // Only clips overlapping right now need concurrent readers. Past this many the least recently
    // used one is closed, unless more than that are in use right now: a reader asked for within
    // kActiveWindowMs is one of those, and evicting it would only thrash.
    static constexpr size_t kMinStreams = 4;
    static constexpr qint64 kActiveWindowMs = 1'000;

    struct ReaderEntry
    {
        std::unique_ptr<ClipReader> reader;
        qint64 lastUseMs = 0;
        // Compositor thread that last read through this reader, 0 for everything else.
        quintptr session = 0;
    };
    QHash<quintptr, QList<quint64>> m_activeStreams;

    // Call with m_mutex held.
    ClipReader *readerFor(quint64 streamId, int audioStreamOrdinal = 0, quintptr session = 0);

    QString m_path;
    std::map<quint64, ReaderEntry> m_readers;
    QElapsedTimer m_clock;
    QMutex m_mutex;

    QMutex m_prefetchMutex;
    QSet<quint64> m_prefetchPending;

    QAtomicInt m_audioRepositionPending{0};
};
