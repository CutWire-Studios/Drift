#pragma once

#include "MediaWaveform.h"

#include <QHash>
#include <QList>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVector>

// On-demand audio peaks for the timeline.
//
// Decoding a whole file up front costs minutes on a multi-hour source and nothing renders
// until it finishes. Instead the source is cut into fixed-length blocks and only the blocks
// the viewport actually covers get decoded — audio seeks are accurate, so a block costs time
// proportional to its own length rather than to the file's.
//
// Blocks already decoded are reused as you pan and zoom, and every clip cut from the same
// source shares them.
class WaveformBlockCache : public QObject
{
    Q_OBJECT

public:
    static constexpr int kPeaksPerSecond = 100;
    static constexpr int kBlockSeconds = 30;
    static constexpr int kPeaksPerBlock = kPeaksPerSecond * kBlockSeconds;

    explicit WaveformBlockCache(QObject *parent = nullptr);

    // [startSeconds, startSeconds + durSeconds) max-reduced into exactly `outCount` values,
    // assembled from whatever blocks are decoded. Reducing here rather than returning the
    // dense span keeps a zoomed-out query bounded by the pixels it will occupy instead of by
    // the source length. Slots still decoding come back as -1 so the caller can tell "not
    // loaded yet" from "silent"; missing blocks are queued and rangeReady() fires for the
    // source as they land.
    //
    // `channelIndex` < 0 is the merged max-across-channels envelope a single-lane clip draws.
    // >= 0 is that one channel, and switches this stream to keeping its channels separately
    // from then on — the merged envelope is folded back out of them, so nothing that asks for
    // -1 can tell the difference.
    QVector<float> range(const QString &sourcePath, double startSeconds, double durSeconds,
                         int outCount, int streamOrdinal = 0, int channelIndex = -1);

    // Channels and layout names the decoder found for a stream. Both are empty/0 until the
    // first block lands — the decode is where the container is already open, so this is what
    // the timeline asks instead of probing the file again on the GUI thread.
    int channelCount(const QString &sourcePath, int streamOrdinal = 0) const;
    QStringList channelNames(const QString &sourcePath, int streamOrdinal = 0) const;

    // Drop everything decoded for a source. For media leaving the project (or the project
    // closing): nothing else ever removes a block, so without this a session accumulates every
    // source it has ever scrolled past.
    void forgetSource(const QString &sourcePath);
    void clear();

signals:
    void rangeReady(const QString &sourcePath);

private:
    // One decode covers every channel of a block, so the queue stays one entry per block
    // however many lanes are asking. Keeping that identity is what leaves the eviction loop
    // and runBatch()'s contiguity scan below correct unchanged.
    struct QueueItem {
        QString sourcePath;
        int block = 0;
        int streamOrdinal = 0;
    };

    struct StreamState {
        int channelCount = 0;
        QStringList channelNames;
        // Bumped on every query, so the trim below can evict whole streams nobody is looking
        // at any more rather than blocks at random.
        quint64 lastTouch = 0;
        // Set by the first channelIndex >= 0 request and never cleared: whether the channels
        // are kept is a property of the stream, not of a block, so a batch can never contain
        // a mix of the two.
        bool perChannel = false;
        // Blocks stored for this stream. Exact indices rather than a key prefix scan — a
        // source path containing '#' or '|' already aliases in keyFor(), and making deletion
        // prefix-driven would turn that display collision into data loss.
        QSet<int> blocks;
    };

    void scheduleBatch();
    void runBatch();
    void applyBatch(const QString &sourcePath, int firstBlock, int streamOrdinal,
                    const MediaWaveform::PerChannel &decoded, const QList<int> &requested);
    // Drop a stream's stored blocks. Blocks decoded before the flip to per-channel hold only
    // the merged envelope and cannot be filled in afterwards, so they go and the viewport
    // asks once more.
    void forgetStream(const QString &sourcePath, int streamOrdinal);
    // Evict least-recently-touched streams until the stored peaks fit the budget. `keepStreamKey`
    // is the stream currently being served, which must survive even if it is the largest.
    void trimToBudget(const QString &keepStreamKey);

    static QString keyFor(const QString &sourcePath, int block, int streamOrdinal = 0,
                          int channelIndex = -1);
    static QString blockKeyFor(const QString &sourcePath, int block, int streamOrdinal = 0);
    static QString streamKeyFor(const QString &sourcePath, int streamOrdinal = 0);

    // ~24 MB of peaks. A 30 s block is 3000 floats (12 KB) per channel, so this holds roughly
    // 2000 mono blocks — 16 hours of audio — or a quarter of that with 7.1 lanes turned on.
    static constexpr qint64 kMaxStoredPeaks = 24 * 1024 * 1024 / static_cast<qint64>(sizeof(float));

    QHash<QString, QVector<float>> m_blocks;
    qint64 m_storedPeaks = 0;
    quint64 m_tick = 0;
    // Blocks a decode has been tried for. A block that produced nothing is past the end of
    // the media (or unreadable); without this the absence of an entry is indistinguishable
    // from "not decoded yet" and the block would be asked for forever.
    QSet<QString> m_attempted;
    QHash<QString, StreamState> m_streams;
    QSet<QString> m_queued; // block keys, one per block regardless of channel count
    // path + block index + streamOrdinal, newest last.
    QList<QueueItem> m_queue;
    bool m_busy = false;
    bool m_batchScheduled = false;
};
