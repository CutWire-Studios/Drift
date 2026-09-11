#include "FilmstripTileCache.h"

#include "MediaThumbnail.h"

#include <QFileInfo>
#include <QTimer>

namespace {

// Deep enough to cover a screenful of tiles a few times over; beyond that the oldest
// requests are stale scroll history and get dropped.
constexpr int kMaxQueued = 256;
// One decoder pass per batch. Small enough that a zoom change is served promptly,
// large enough that the seeks in it stay close together in the file.
constexpr int kBatchSize = 24;
constexpr qint64 kTileCacheMaxBytes = 128LL * 1024 * 1024;
constexpr int kPruneEvery = 512;
// Far more tiles than any viewport holds. Past that the whole memo goes rather than tracking
// LRU order for it — a miss costs one stat, and the paths are rebuilt on the next lookup.
constexpr int kMaxReadyEntries = 4096;
// Idle long enough that panning, pausing to look, and panning again doesn't reopen the file.
constexpr int kDecoderIdleMs = 30'000;

} // namespace

FilmstripTileCache::FilmstripTileCache(QObject *parent)
    : QObject(parent)
{
    m_decodeContext = new QObject;
    m_idleTimer = new QTimer(m_decodeContext);
    m_idleTimer->setSingleShot(true);
    m_idleTimer->setInterval(kDecoderIdleMs);
    // m_idleTimer as the context object, so this runs on the decode thread — the only thread
    // allowed to touch m_decoder.
    connect(m_idleTimer, &QTimer::timeout, m_idleTimer, [this] { m_decoder.close(); });

    // Moves the timer with it.
    m_decodeContext->moveToThread(&m_decodeThread);
    // Destroys the context and its timer on their own thread, as part of thread teardown.
    connect(&m_decodeThread, &QThread::finished, m_decodeContext, &QObject::deleteLater);

    m_decodeThread.setObjectName(QStringLiteral("drift-filmstrip"));
    m_decodeThread.start();
}

FilmstripTileCache::~FilmstripTileCache()
{
    // Decode lambdas capture `this`, so the thread has to be stopped before any member is
    // destroyed. wait() returns only once the batch in flight has finished.
    m_decodeThread.quit();
    m_decodeThread.wait();
    m_decoder.close();
}

void FilmstripTileCache::clear()
{
    m_ready.clear();
    m_failed.clear();
    m_emptyBatches.clear();
    m_queued.clear();
    m_queue.clear();
}

QString FilmstripTileCache::keyFor(const QString &sourcePath, int level, qint64 index,
                                   int rotationCorrection)
{
    return sourcePath + QLatin1Char('|') + QString::number(level) + QLatin1Char('|')
           + QString::number(index) + QLatin1Char('|') + QString::number(rotationCorrection);
}

QString FilmstripTileCache::tile(const QString &sourcePath, int level, qint64 index,
                                 int rotationCorrection)
{
    if (sourcePath.isEmpty() || m_failed.contains(sourcePath))
        return {};

    const QString key = keyFor(sourcePath, level, index, rotationCorrection);
    const auto cached = m_ready.constFind(key);
    if (cached != m_ready.constEnd())
        return cached.value();

    if (m_queued.contains(key))
        return {};

    // A tile written by an earlier session is on disk but not in m_ready yet.
    const QString path = MediaThumbnail::tilePath(sourcePath, level, index, rotationCorrection);
    if (QFileInfo::exists(path)) {
        if (m_ready.size() >= kMaxReadyEntries)
            m_ready.clear();
        m_ready.insert(key, path);
        return path;
    }

    m_queued.insert(key);
    m_queue.append({sourcePath, level, index, rotationCorrection});
    while (m_queue.size() > kMaxQueued) {
        const Request &first = m_queue.first();
        m_queued.remove(keyFor(first.sourcePath, first.level, first.index, first.rotationCorrection));
        m_queue.removeFirst();
    }

    scheduleBatch();
    return {};
}

void FilmstripTileCache::scheduleBatch()
{
    if (m_busy || m_batchScheduled || m_queue.isEmpty())
        return;

    // Let the whole burst of requests from one layout pass land before picking a batch,
    // so the batch reflects the final viewport rather than the first tile that asked.
    m_batchScheduled = true;
    QTimer::singleShot(0, this, &FilmstripTileCache::runBatch);
}

void FilmstripTileCache::runBatch()
{
    m_batchScheduled = false;
    if (m_busy || m_queue.isEmpty())
        return;

    // Newest first: those are the tiles the viewport asked for most recently.
    const Request newest = m_queue.last();
    QList<qint64> indices;
    for (int i = m_queue.size() - 1; i >= 0 && indices.size() < kBatchSize; --i) {
        const Request &request = m_queue.at(i);
        if (request.sourcePath != newest.sourcePath || request.level != newest.level
            || request.rotationCorrection != newest.rotationCorrection)
            continue;
        indices.append(request.index);
        m_queue.removeAt(i);
    }
    if (indices.isEmpty())
        return;

    m_busy = true;
    const QString sourcePath = newest.sourcePath;
    const int level = newest.level;
    const int rotationCorrection = newest.rotationCorrection;
    QMetaObject::invokeMethod(
        m_decodeContext,
        [this, sourcePath, level, rotationCorrection, indices] {
            m_idleTimer->stop();
            const QList<qint64> produced =
                m_decoder.generateTiles(sourcePath, level, indices, rotationCorrection);
            m_idleTimer->start();
            QMetaObject::invokeMethod(
                this,
                [this, sourcePath, level, rotationCorrection, produced, indices] {
                    applyBatch(sourcePath, level, rotationCorrection, produced, indices);
                },
                Qt::QueuedConnection);
        },
        Qt::QueuedConnection);
}

void FilmstripTileCache::applyBatch(const QString &sourcePath, int level, int rotationCorrection,
                                    const QList<qint64> &produced, const QList<qint64> &requested)
{
    m_busy = false;

    for (const qint64 index : requested)
        m_queued.remove(keyFor(sourcePath, level, index, rotationCorrection));

    if (m_ready.size() + produced.size() > kMaxReadyEntries)
        m_ready.clear();
    for (const qint64 index : produced)
        m_ready.insert(keyFor(sourcePath, level, index, rotationCorrection),
                       MediaThumbnail::tilePath(sourcePath, level, index, rotationCorrection));

    if (produced.isEmpty()) {
        // Repeatedly empty means no video stream, or the file is gone. Stop asking; a single
        // empty batch can just be seeks landing past the end of a short source.
        if (++m_emptyBatches[sourcePath] >= 3)
            m_failed.insert(sourcePath);
    } else {
        m_emptyBatches.remove(sourcePath);
        emit tileReady(sourcePath);
    }

    m_producedSincePrune += produced.size();
    if (m_producedSincePrune >= kPruneEvery) {
        m_producedSincePrune = 0;
        QMetaObject::invokeMethod(
            m_decodeContext,
            [this] {
                MediaThumbnail::pruneTileCache(kTileCacheMaxBytes);
                // Pruning may have deleted tiles we have memoized; drop the lot and let the
                // next lookup re-check the disk rather than hand out paths that are gone.
                QMetaObject::invokeMethod(this, [this] { m_ready.clear(); }, Qt::QueuedConnection);
            },
            Qt::QueuedConnection);
    }

    scheduleBatch();
}
