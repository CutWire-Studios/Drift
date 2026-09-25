#pragma once

#include <QSize>
#include <QString>

#include <cstdint>

extern "C" {
#include <libavutil/rational.h>
}

struct AVCodecContext;
struct AVFormatContext;
struct AVStream;
struct AVFrame;
struct AVPacket;
struct SwsContext;

namespace drift {

// H.264 writer for the decode-side proxies (reversed ranges and low-res preview copies). The
// caller supplies each frame's timestamp in the source stream's own time base, so the proxy can
// keep the exact timing of the file it stands in for.
//
// Structured like MatteWriter (open / writeFrame / finish / abort with the same .part-then-rename
// discipline) rather than reusing Exporter::run, which is a single goto-cleanup function and does
// not compose.
class ProxyEncoder
{
public:
    struct Options
    {
        // Empty keeps the decoder's size.
        QSize size;
        const char *crf = "16";
        const char *preset = "veryfast";
        // Short GOP rather than all-intra: a keyframe every twelve frames keeps scrubbing cheap at
        // roughly a third of the size.
        int gopSize = 12;
    };

    ~ProxyEncoder() { abort(); }

    bool open(const QString &path, const AVCodecContext *dec, AVRational timeBase,
              AVRational frameRate, int rotationDegrees, const Options &options,
              QString *errorOut);
    bool writeFrame(const AVFrame *src, int64_t pts, QString *errorOut);
    bool finish(QString *errorOut);
    void abort();

private:
    bool drainPackets(QString *errorOut);
    void teardown();

    AVFormatContext *m_fmt = nullptr;
    AVCodecContext *m_ctx = nullptr;
    AVStream *m_stream = nullptr;
    AVFrame *m_frame = nullptr;
    AVPacket *m_pkt = nullptr;
    SwsContext *m_sws = nullptr;
    QString m_path;
    QString m_tmpPath;
    int64_t m_lastPts = INT64_MIN;
    bool m_finished = false;
};

// Owns a demuxer and decoder for a proxy render. Its own context, not one from ClipReaderPool, so
// preview playback keeps running off the live path while a render is going.
struct ProxySource
{
    AVFormatContext *fmt = nullptr;
    AVCodecContext *dec = nullptr;
    int stream = -1;

    ~ProxySource();
    // Opens the best video stream of path with a batch-job decoder (all threads). On failure
    // errorOut says why; the caller adds any context of its own.
    bool open(const QString &path, QString *errorOut);
    // avg_frame_rate, else r_frame_rate, else 30.
    AVRational frameRate() const;
};

} // namespace drift
