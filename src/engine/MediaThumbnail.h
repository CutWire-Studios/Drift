#pragma once

#include "core/VectorSource.h"

#include <QList>
#include <QString>

struct AVCodecContext;
struct AVFormatContext;
struct SwsContext;

class MediaThumbnail
{
public:
    static constexpr int kFilmstripFrameWidth = 120;
    static constexpr int kFilmstripFrameHeight = 68;
    static constexpr int kFilmstripFrameCount = 8;

    static QString generate(const QString &sourcePath, const QString &kind);
    // A vector clip's poster frame, keyed by the file or, for an inline document, by its hash.
    static QString generateVector(const drift::VectorSource &source);
    static QString generateFilmstrip(const QString &sourcePath, const QString &kind);
    static QString generateAtTime(const QString &sourcePath, double sourceSeconds);

    // On-demand filmstrip tiles. The coarse strip above only ever holds 8 frames, so a long
    // clip repeats the same image for thousands of px; these fill in the real frame for a
    // given moment. A tile covers `2^level` source seconds starting at `index * 2^level`,
    // so zooming picks a finer level and panning reuses everything already cached.
    static QString tilePath(const QString &sourcePath, int level, qint64 index);

    // Drops the oldest tile files until the tile cache fits in `maxBytes`.
    static void pruneTileCache(qint64 maxBytes);

    // A decoder held open across batches of tiles.
    //
    // Opening a decoder is the expensive part: even at 1080p libavcodec allocates a full
    // decoded-picture buffer, and doing that per batch — on a different pool thread each
    // time, so glibc cannot reuse the arena the last one freed — is what makes panning the
    // timeline ratchet RSS up. One instance, pinned to one thread, holds a single working
    // set for as long as tiles keep coming.
    //
    // Not thread-safe: own it from a single thread.
    class TileDecoder
    {
    public:
        TileDecoder() = default;
        ~TileDecoder();

        TileDecoder(const TileDecoder &) = delete;
        TileDecoder &operator=(const TileDecoder &) = delete;

        // Decodes the missing tiles among `indices`, in ascending time order so seeking stays
        // near-sequential. Returns the indices that are now on disk. Reopens only when
        // `sourcePath` differs from the file already open.
        QList<qint64> generateTiles(const QString &sourcePath, int level,
                                    const QList<qint64> &indices);

        // Releases the decoder and its scaler. Safe to call when nothing is open; the next
        // generateTiles() reopens on demand.
        void close();

    private:
        bool ensureOpen(const QString &absolutePath);

        QString m_path;
        AVFormatContext *m_fmt = nullptr;
        AVCodecContext *m_codecCtx = nullptr;
        SwsContext *m_sws = nullptr;
        int m_videoStreamIndex = -1;
    };
};
