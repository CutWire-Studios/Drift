#pragma once

#include "MediaThumbnail.h"

#include <QHash>
#include <QList>
#include <QObject>
#include <QSet>
#include <QString>
#include <QThread>

class QTimer;

// On-demand filmstrip tiles for the timeline.
//
// Asking for a tile is a lookup: it returns the cached file straight away, or an empty string
// and queues a decode. Only tiles the timeline actually instantiates are ever requested, so a
// three-hour clip costs a screenful of frames rather than a full-length strip.
//
// The queue is served newest-first and capped, because scrolling a long timeline asks for far
// more tiles than it keeps on screen — the ones that scrolled away fall off the back rather
// than blocking the ones under the cursor.
//
// Decoding runs on one thread this class owns, and the decoder stays open between batches. Both
// matter for memory: opening a decoder per batch allocates a fresh decoded-picture buffer, and
// doing it on a rotating set of pool threads leaves each of their allocator arenas holding on
// to a copy, so panning ratchets RSS up and never gives it back.
class FilmstripTileCache : public QObject
{
    Q_OBJECT

public:
    explicit FilmstripTileCache(QObject *parent = nullptr);
    ~FilmstripTileCache() override;

    // Cached tile file for `2^level`-second tile number `index` of `sourcePath`, or an empty
    // string if it still needs decoding — tileReady() fires for the source when it lands.
    QString tile(const QString &sourcePath, int level, qint64 index, int rotationCorrection = 0);

    // Forgets everything: memoized paths, the failure blacklist and anything still queued.
    // Call when the timeline's sources change wholesale — a new project, or a relink — so a
    // source that was missing gets another chance instead of staying blacklisted for the rest
    // of the session.
    void clear();

signals:
    void tileReady(const QString &sourcePath);

private:
    struct Request
    {
        QString sourcePath;
        int level = 0;
        qint64 index = 0;
        int rotationCorrection = 0;
    };

    void scheduleBatch();
    void runBatch();
    void applyBatch(const QString &sourcePath, int level, int rotationCorrection,
                    const QList<qint64> &produced, const QList<qint64> &requested);

    static QString keyFor(const QString &sourcePath, int level, qint64 index, int rotationCorrection);

    QHash<QString, QString> m_ready;
    // Sources whose decode failed, so a broken or audio-only file isn't retried every repaint.
    QSet<QString> m_failed;
    QHash<QString, int> m_emptyBatches;
    QSet<QString> m_queued;
    QList<Request> m_queue;
    bool m_busy = false;
    bool m_batchScheduled = false;
    int m_producedSincePrune = 0;

    QThread m_decodeThread;
    // Lives on m_decodeThread; the target every decode is posted to.
    QObject *m_decodeContext = nullptr;
    // Closes the decoder once the timeline has been still for a while, so a paused editor
    // doesn't sit on a decoder's working set. Owned by m_decodeContext.
    QTimer *m_idleTimer = nullptr;
    // Decode thread only.
    MediaThumbnail::TileDecoder m_decoder;
};
