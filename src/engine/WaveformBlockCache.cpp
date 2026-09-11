#include "WaveformBlockCache.h"

#include "MediaWaveform.h"

#include <QThreadPool>
#include <QTimer>

#include <cmath>
#include <limits>

namespace {

// One batch is a single contiguous decode, so this is how much audio a job chews through
// before the timeline gets to draw it. Eight minutes fills a viewport in one pass while
// still landing often enough to look progressive.
constexpr int kBatchBlocks = 16;
// Deeper than a viewport needs, so panning back and forth doesn't lose queued work; older
// entries beyond this are scroll history nobody is looking at any more.
constexpr int kMaxQueued = 512;
// Output slots per query. A canvas is viewport-sized, so this is only ever a ceiling.
constexpr int kMaxOutCount = 8192;

} // namespace

WaveformBlockCache::WaveformBlockCache(QObject *parent)
    : QObject(parent)
{
}

QString WaveformBlockCache::streamKeyFor(const QString &sourcePath, int streamOrdinal)
{
    return sourcePath + QLatin1Char('#') + QString::number(streamOrdinal);
}

QString WaveformBlockCache::blockKeyFor(const QString &sourcePath, int block, int streamOrdinal)
{
    return streamKeyFor(sourcePath, streamOrdinal) + QLatin1Char('|') + QString::number(block);
}

QString WaveformBlockCache::keyFor(const QString &sourcePath, int block, int streamOrdinal,
                                   int channelIndex)
{
    // The merged envelope keeps the historical key shape; channels get a suffix, so the two
    // never collide and a merged-only stream stores exactly what it always did.
    const QString base = blockKeyFor(sourcePath, block, streamOrdinal);
    return channelIndex < 0 ? base : base + QLatin1Char(':') + QString::number(channelIndex);
}

int WaveformBlockCache::channelCount(const QString &sourcePath, int streamOrdinal) const
{
    const auto it = m_streams.constFind(streamKeyFor(sourcePath, streamOrdinal));
    return it == m_streams.constEnd() ? 0 : it.value().channelCount;
}

QStringList WaveformBlockCache::channelNames(const QString &sourcePath, int streamOrdinal) const
{
    const auto it = m_streams.constFind(streamKeyFor(sourcePath, streamOrdinal));
    return it == m_streams.constEnd() ? QStringList{} : it.value().channelNames;
}

void WaveformBlockCache::forgetStream(const QString &sourcePath, int streamOrdinal)
{
    const QString streamKey = streamKeyFor(sourcePath, streamOrdinal);
    const auto it = m_streams.find(streamKey);
    if (it == m_streams.end())
        return;

    StreamState &state = it.value();
    for (const int block : state.blocks) {
        const QString blockKey = blockKeyFor(sourcePath, block, streamOrdinal);
        m_storedPeaks -= m_blocks.take(blockKey).size();
        for (int c = 0; c < state.channelCount; ++c)
            m_storedPeaks -= m_blocks.take(keyFor(sourcePath, block, streamOrdinal, c)).size();
        // Both have to go together: m_attempted is what stops a block being re-queued, so
        // leaving it behind while dropping the data would leave the lane permanently blank.
        m_attempted.remove(blockKey);
    }
    state.blocks.clear();
    m_storedPeaks = qMax<qint64>(0, m_storedPeaks);
}

void WaveformBlockCache::forgetSource(const QString &sourcePath)
{
    // Every stream ordinal this source has decoded under, not just 0.
    const QString prefix = sourcePath + QLatin1Char('#');
    QList<int> ordinals;
    for (auto it = m_streams.constBegin(); it != m_streams.constEnd(); ++it) {
        if (!it.key().startsWith(prefix))
            continue;
        bool ok = false;
        const int ordinal = it.key().mid(prefix.size()).toInt(&ok);
        if (ok)
            ordinals.append(ordinal);
    }
    for (const int ordinal : ordinals) {
        forgetStream(sourcePath, ordinal);
        m_streams.remove(streamKeyFor(sourcePath, ordinal));
    }
}

void WaveformBlockCache::clear()
{
    m_blocks.clear();
    m_attempted.clear();
    m_streams.clear();
    m_queued.clear();
    m_queue.clear();
    m_storedPeaks = 0;
}

void WaveformBlockCache::trimToBudget(const QString &keepStreamKey)
{
    while (m_storedPeaks > kMaxStoredPeaks) {
        QString oldestKey;
        quint64 oldestTouch = std::numeric_limits<quint64>::max();
        for (auto it = m_streams.constBegin(); it != m_streams.constEnd(); ++it) {
            if (it.key() == keepStreamKey || it.value().blocks.isEmpty())
                continue;
            if (it.value().lastTouch < oldestTouch) {
                oldestTouch = it.value().lastTouch;
                oldestKey = it.key();
            }
        }
        // Only the stream being served is left holding anything; evicting it would throw away
        // what the viewport is drawing right now, so the budget gives way instead.
        if (oldestKey.isEmpty())
            return;

        const int split = oldestKey.lastIndexOf(QLatin1Char('#'));
        const QString path = oldestKey.left(split);
        const int ordinal = oldestKey.mid(split + 1).toInt();
        forgetStream(path, ordinal);
    }
}

QVector<float> WaveformBlockCache::range(const QString &sourcePath, double startSeconds,
                                         double durSeconds, int outCount, int streamOrdinal,
                                         int channelIndex)
{
    if (sourcePath.isEmpty() || durSeconds <= 0.0 || outCount <= 0)
        return {};

    {
        StreamState &state = m_streams[streamKeyFor(sourcePath, streamOrdinal)];
        state.lastTouch = ++m_tick;
        if (channelIndex >= 0 && !state.perChannel) {
            state.perChannel = true;
            forgetStream(sourcePath, streamOrdinal);
        }
    }

    outCount = qMin(outCount, kMaxOutCount);
    QVector<float> out(outCount, -1.0f);

    const double start = qMax(0.0, startSeconds);
    const double end = start + durSeconds;
    const qint64 firstPeak = static_cast<qint64>(std::floor(start * kPeaksPerSecond));
    const qint64 lastPeak = static_cast<qint64>(std::ceil(end * kPeaksPerSecond)) - 1;
    if (lastPeak < firstPeak)
        return {};

    const int firstBlock = static_cast<int>(firstPeak / kPeaksPerBlock);
    const int lastBlock = static_cast<int>(lastPeak / kPeaksPerBlock);
    bool queuedAny = false;

    for (int b = firstBlock; b <= lastBlock; ++b) {
        const QString blockKey = blockKeyFor(sourcePath, b, streamOrdinal);
        if (m_attempted.contains(blockKey) || m_queued.contains(blockKey))
            continue;
        m_queued.insert(blockKey);
        m_queue.append({sourcePath, b, streamOrdinal});
        queuedAny = true;
    }

    // Gather per output slot rather than scattering per peak, so zooming in past one peak
    // per pixel repeats the nearest peak instead of leaving gaps between hairlines. Slots
    // advance monotonically, so the block lookup only changes once per block.
    const qint64 spanPeaks = lastPeak - firstPeak + 1;
    const QVector<float> *block = nullptr;
    int blockIndex = -1;
    auto peakAt = [&](qint64 g) -> float {
        const int b = static_cast<int>(g / kPeaksPerBlock);
        if (b != blockIndex) {
            blockIndex = b;
            const auto it = m_blocks.constFind(keyFor(sourcePath, b, streamOrdinal, channelIndex));
            block = it == m_blocks.constEnd() ? nullptr : &it.value();
        }
        if (!block)
            return -1.0f;
        const int i = static_cast<int>(g - static_cast<qint64>(b) * kPeaksPerBlock);
        return i >= 0 && i < block->size() ? (*block)[i] : -1.0f;
    };

    for (int j = 0; j < outCount; ++j) {
        const qint64 g0 = firstPeak
                          + static_cast<qint64>(static_cast<double>(j) * spanPeaks / outCount);
        qint64 g1 = firstPeak
                    + static_cast<qint64>(static_cast<double>(j + 1) * spanPeaks / outCount);
        if (g1 <= g0)
            g1 = g0 + 1;
        float peak = -1.0f;
        for (qint64 g = g0; g < g1; ++g)
            peak = qMax(peak, peakAt(g));
        out[j] = peak;
    }

    if (queuedAny) {
        while (m_queue.size() > kMaxQueued) {
            const QueueItem &oldest = m_queue.first();
            m_queued.remove(blockKeyFor(oldest.sourcePath, oldest.block, oldest.streamOrdinal));
            m_queue.removeFirst();
        }
        scheduleBatch();
    }

    return out;
}

void WaveformBlockCache::scheduleBatch()
{
    if (m_busy || m_batchScheduled || m_queue.isEmpty())
        return;

    // Let the whole burst of requests from one layout pass land first, so the batch reflects
    // the settled viewport rather than the first block that asked.
    m_batchScheduled = true;
    QTimer::singleShot(0, this, &WaveformBlockCache::runBatch);
}

void WaveformBlockCache::runBatch()
{
    m_batchScheduled = false;
    if (m_busy || m_queue.isEmpty())
        return;

    // Newest first: that is what the viewport asked for most recently. Take a contiguous run
    // from there so the decode is one seek followed by a straight read.
    const QString sourcePath = m_queue.last().sourcePath;
    const int anchor = m_queue.last().block;
    const int streamOrdinal = m_queue.last().streamOrdinal;

    QSet<int> wanted;
    for (int i = m_queue.size() - 1; i >= 0 && wanted.size() < kBatchBlocks; --i) {
        if (m_queue.at(i).sourcePath != sourcePath || m_queue.at(i).streamOrdinal != streamOrdinal)
            continue;
        const int block = m_queue.at(i).block;
        if (block < anchor - kBatchBlocks || block > anchor + kBatchBlocks)
            continue;
        wanted.insert(block);
    }
    if (wanted.isEmpty())
        return;

    // Grow outward from the anchor while blocks are adjacent, so gaps aren't decoded through.
    int first = anchor;
    int last = anchor;
    while (wanted.contains(first - 1))
        --first;
    while (wanted.contains(last + 1))
        ++last;

    QList<int> requested;
    for (int block = first; block <= last; ++block) {
        requested.append(block);
        m_queued.remove(blockKeyFor(sourcePath, block, streamOrdinal));
    }
    for (int i = m_queue.size() - 1; i >= 0; --i) {
        if (m_queue.at(i).sourcePath == sourcePath && m_queue.at(i).streamOrdinal == streamOrdinal
            && m_queue.at(i).block >= first && m_queue.at(i).block <= last)
            m_queue.removeAt(i);
    }

    m_busy = true;
    const double startSeconds = static_cast<double>(first) * kBlockSeconds;
    const double endSeconds = static_cast<double>(last + 1) * kBlockSeconds;
    QThreadPool::globalInstance()->start([this, sourcePath, first, streamOrdinal, startSeconds, endSeconds,
                                          requested] {
        // Always decode per-channel, whatever this stream is currently storing: the samples
        // have to be read either way, the merged envelope folds out of the result for free,
        // and it means a lane turning on mid-decode still finds its channels in this batch.
        const MediaWaveform::PerChannel decoded = MediaWaveform::peaksForRangePerChannel(
            sourcePath, startSeconds, endSeconds, kPeaksPerSecond, streamOrdinal);

        QMetaObject::invokeMethod(
            this,
            [this, sourcePath, first, streamOrdinal, decoded, requested] {
                applyBatch(sourcePath, first, streamOrdinal, decoded, requested);
            },
            Qt::QueuedConnection);
    });
}

void WaveformBlockCache::applyBatch(const QString &sourcePath, int firstBlock, int streamOrdinal,
                                    const MediaWaveform::PerChannel &decoded,
                                    const QList<int> &requested)
{
    m_busy = false;

    StreamState &state = m_streams[streamKeyFor(sourcePath, streamOrdinal)];
    const int channels = decoded.channels.size();
    if (channels > 0) {
        state.channelCount = channels;
        state.channelNames = decoded.channelNames;
    }

    bool storedAny = false;
    for (int i = 0; i < requested.size(); ++i) {
        const int block = requested.at(i);
        // Marked whatever the outcome. A block the decode never reached is past the end of
        // the media (or unreadable), and this is what stops it being asked for forever.
        m_attempted.insert(blockKeyFor(sourcePath, block, streamOrdinal));

        const int from = i * kPeaksPerBlock;
        if (channels == 0 || from >= decoded.channels.first().size())
            continue;

        // Split the one decode back into per-block entries so panning reuses them piecemeal.
        QVector<float> merged = decoded.channels.first().mid(from, kPeaksPerBlock);
        for (int c = 1; c < channels; ++c) {
            const QVector<float> &channel = decoded.channels.at(c);
            for (int j = 0; j < merged.size() && from + j < channel.size(); ++j)
                merged[j] = qMax(merged[j], channel.at(from + j));
        }
        m_storedPeaks += merged.size();
        m_blocks.insert(keyFor(sourcePath, block, streamOrdinal, -1), merged);

        // Read the mode here rather than capturing it in runBatch(): a lane can turn on while
        // this batch was in flight, and the flip already dropped this stream's blocks, so
        // storing merged-only now would strand the lane with nothing to draw.
        if (state.perChannel) {
            for (int c = 0; c < channels; ++c) {
                const QVector<float> lane = decoded.channels.at(c).mid(from, kPeaksPerBlock);
                m_storedPeaks += lane.size();
                m_blocks.insert(keyFor(sourcePath, block, streamOrdinal, c), lane);
            }
        }
        state.blocks.insert(block);
        storedAny = true;
    }

    // After the insert, not before: the stream just served is the one to keep.
    trimToBudget(streamKeyFor(sourcePath, streamOrdinal));

    if (storedAny)
        emit rangeReady(sourcePath);

    scheduleBatch();
}
