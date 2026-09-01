#pragma once

#include <QHash>
#include <QList>
#include <QObject>
#include <QSet>
#include <QString>
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
    QVector<float> range(const QString &sourcePath, double startSeconds, double durSeconds,
                         int outCount, int streamOrdinal = 0);

signals:
    void rangeReady(const QString &sourcePath);

private:
    struct QueueItem {
        QString sourcePath;
        int block = 0;
        int streamOrdinal = 0;
    };

    void scheduleBatch();
    void runBatch();
    void applyBatch(const QString &sourcePath, int firstBlock, int streamOrdinal,
                    const QList<QVector<float>> &blocks, const QList<int> &requested);

    static QString keyFor(const QString &sourcePath, int block, int streamOrdinal = 0);

    QHash<QString, QVector<float>> m_blocks;
    QSet<QString> m_queued;
    // path + block index + streamOrdinal, newest last.
    QList<QueueItem> m_queue;
    bool m_busy = false;
    bool m_batchScheduled = false;
};
