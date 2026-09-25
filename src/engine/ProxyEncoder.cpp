#include "ProxyEncoder.h"

#include "SwsColorRange.h"

#include <QCoreApplication>
#include <QFile>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/display.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
}

namespace drift {

bool ProxyEncoder::open(const QString &path, const AVCodecContext *dec, AVRational timeBase,
                        AVRational frameRate, int rotationDegrees, const Options &options,
                        QString *errorOut)
{
    auto fail = [&](const QString &message) {
        if (errorOut)
            *errorOut = message;
        teardown();
        return false;
    };

    m_path = path;
    m_tmpPath = path + QStringLiteral(".part");
    if (QFile::exists(m_tmpPath))
        QFile::remove(m_tmpPath);

    const QByteArray tmpUtf8 = m_tmpPath.toUtf8();
    avformat_alloc_output_context2(&m_fmt, nullptr, "mp4", tmpUtf8.constData());
    if (!m_fmt)
        return fail(QCoreApplication::translate("ProxyEncoder", "Could not create the proxy container"));

    const AVCodec *codec = avcodec_find_encoder(AV_CODEC_ID_H264);
    if (!codec)
        return fail(QCoreApplication::translate("ProxyEncoder", "H.264 encoder not available"));

    m_stream = avformat_new_stream(m_fmt, nullptr);
    if (!m_stream)
        return fail(QCoreApplication::translate("ProxyEncoder", "Could not create the proxy stream"));

    m_ctx = avcodec_alloc_context3(codec);
    if (!m_ctx)
        return fail(QCoreApplication::translate("ProxyEncoder", "Could not allocate the proxy encoder"));

    m_ctx->width = options.size.isEmpty() ? dec->width : options.size.width();
    m_ctx->height = options.size.isEmpty() ? dec->height : options.size.height();
    m_ctx->pix_fmt = AV_PIX_FMT_YUV420P;
    m_ctx->time_base = timeBase;
    m_ctx->framerate = frameRate;
    m_ctx->gop_size = options.gopSize;
    // No reorder delay, so a seek into the proxy produces a frame immediately.
    m_ctx->max_b_frames = 0;
    // Carried across or the proxy comes back colour-shifted against the source it replaces.
    m_ctx->color_range = dec->color_range;
    m_ctx->colorspace = dec->colorspace;
    m_ctx->color_primaries = dec->color_primaries;
    m_ctx->color_trc = dec->color_trc;
    if (m_fmt->oformat->flags & AVFMT_GLOBALHEADER)
        m_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    av_opt_set(m_ctx->priv_data, "crf", options.crf, 0);
    av_opt_set(m_ctx->priv_data, "preset", options.preset, 0);
    av_opt_set(m_ctx->priv_data, "tune", "fastdecode", 0);

    if (avcodec_open2(m_ctx, codec, nullptr) < 0)
        return fail(QCoreApplication::translate("ProxyEncoder", "Could not open the proxy encoder"));

    avcodec_parameters_from_context(m_stream->codecpar, m_ctx);
    // The proxy keeps the source's pixels unrotated, so it has to keep the source's
    // display matrix too — otherwise a rotated clip decodes upright from the original
    // and sideways from its proxy. _set takes a clockwise angle while _get
    // (behind displayRotationOf) reports counterclockwise, so this passes it unnegated.
    if (rotationDegrees != 0) {
        AVPacketSideData *sd = av_packet_side_data_new(&m_stream->codecpar->coded_side_data,
                                                       &m_stream->codecpar->nb_coded_side_data,
                                                       AV_PKT_DATA_DISPLAYMATRIX,
                                                       sizeof(int32_t) * 9, 0);
        if (sd)
            av_display_rotation_set(reinterpret_cast<int32_t *>(sd->data), rotationDegrees);
    }
    m_stream->time_base = m_ctx->time_base;
    // Without this the muxer infers the rate from packet timestamps and lands on 29.97 for a
    // 30 fps source, the same trap MatteWriter documents.
    m_stream->avg_frame_rate = frameRate;
    m_stream->r_frame_rate = frameRate;

    if (!(m_fmt->oformat->flags & AVFMT_NOFILE)) {
        if (avio_open(&m_fmt->pb, tmpUtf8.constData(), AVIO_FLAG_WRITE) < 0)
            return fail(QCoreApplication::translate("ProxyEncoder", "Could not open the proxy file for writing"));
    }
    if (avformat_write_header(m_fmt, nullptr) < 0)
        return fail(QCoreApplication::translate("ProxyEncoder", "Could not write the proxy header"));

    m_pkt = av_packet_alloc();
    m_frame = av_frame_alloc();
    if (!m_pkt || !m_frame)
        return fail(QCoreApplication::translate("ProxyEncoder", "Could not allocate proxy frame buffers"));

    m_frame->format = AV_PIX_FMT_YUV420P;
    m_frame->width = m_ctx->width;
    m_frame->height = m_ctx->height;
    if (av_frame_get_buffer(m_frame, 0) < 0)
        return fail(QCoreApplication::translate("ProxyEncoder", "Could not allocate the proxy frame"));

    return true;
}

bool ProxyEncoder::writeFrame(const AVFrame *src, int64_t pts, QString *errorOut)
{
    if (!m_ctx || !m_frame) {
        if (errorOut)
            *errorOut = QCoreApplication::translate("ProxyEncoder", "Proxy writer is not open");
        return false;
    }

    m_sws = sws_getCachedContext(m_sws, src->width, src->height,
                                 swsSourceFormat(static_cast<AVPixelFormat>(src->format)),
                                 m_ctx->width, m_ctx->height, AV_PIX_FMT_YUV420P, SWS_BILINEAR,
                                 nullptr, nullptr, nullptr);
    if (!m_sws) {
        if (errorOut)
            *errorOut = QCoreApplication::translate("ProxyEncoder", "Could not convert a frame for the proxy encoder");
        return false;
    }
    // m_ctx->color_range was set from the source decoder's, so the proxy is encoded to keep
    // the original range rather than converting it.
    configureSwsRangePreserving(m_sws, src);
    if (av_frame_make_writable(m_frame) < 0) {
        if (errorOut)
            *errorOut = QCoreApplication::translate("ProxyEncoder", "Could not make the proxy frame writable");
        return false;
    }
    sws_scale(m_sws, src->data, src->linesize, 0, src->height, m_frame->data, m_frame->linesize);

    // x264 rejects a non-advancing timestamp. Sources with duplicate timestamps are rare but do
    // exist, and one of them must not abort a render that is otherwise fine.
    if (pts <= m_lastPts)
        pts = m_lastPts + 1;
    m_lastPts = pts;
    m_frame->pts = pts;

    if (avcodec_send_frame(m_ctx, m_frame) < 0) {
        if (errorOut)
            *errorOut = QCoreApplication::translate("ProxyEncoder", "Proxy encoder rejected a frame");
        return false;
    }
    return drainPackets(errorOut);
}

bool ProxyEncoder::drainPackets(QString *errorOut)
{
    for (;;) {
        const int rc = avcodec_receive_packet(m_ctx, m_pkt);
        if (rc == AVERROR(EAGAIN) || rc == AVERROR_EOF)
            return true;
        if (rc < 0) {
            if (errorOut)
                *errorOut = QCoreApplication::translate("ProxyEncoder", "Failed to read an encoded proxy packet");
            return false;
        }
        av_packet_rescale_ts(m_pkt, m_ctx->time_base, m_stream->time_base);
        m_pkt->stream_index = m_stream->index;
        const int wrc = av_interleaved_write_frame(m_fmt, m_pkt);
        av_packet_unref(m_pkt);
        if (wrc < 0) {
            if (errorOut)
                *errorOut = QCoreApplication::translate("ProxyEncoder", "Failed to write a proxy packet");
            return false;
        }
    }
}

bool ProxyEncoder::finish(QString *errorOut)
{
    if (m_finished)
        return true;
    if (!m_ctx || !m_fmt) {
        if (errorOut)
            *errorOut = QCoreApplication::translate("ProxyEncoder", "Proxy writer is not open");
        return false;
    }

    if (avcodec_send_frame(m_ctx, nullptr) < 0) {
        if (errorOut)
            *errorOut = QCoreApplication::translate("ProxyEncoder", "Could not flush the proxy encoder");
        return false;
    }
    if (!drainPackets(errorOut))
        return false;
    if (av_write_trailer(m_fmt) < 0) {
        if (errorOut)
            *errorOut = QCoreApplication::translate("ProxyEncoder", "Could not write the proxy trailer");
        return false;
    }

    teardown();

    if (QFile::exists(m_path))
        QFile::remove(m_path);
    if (!QFile::rename(m_tmpPath, m_path)) {
        if (errorOut)
            *errorOut = QCoreApplication::translate("ProxyEncoder", "Could not move the proxy into place");
        return false;
    }

    m_finished = true;
    return true;
}

void ProxyEncoder::abort()
{
    if (m_finished)
        return;
    teardown();
    if (!m_tmpPath.isEmpty() && QFile::exists(m_tmpPath))
        QFile::remove(m_tmpPath);
    m_tmpPath.clear();
}

void ProxyEncoder::teardown()
{
    if (m_sws) {
        sws_freeContext(m_sws);
        m_sws = nullptr;
    }
    if (m_frame)
        av_frame_free(&m_frame);
    if (m_pkt)
        av_packet_free(&m_pkt);
    if (m_ctx)
        avcodec_free_context(&m_ctx);
    if (m_fmt) {
        if (m_fmt->pb && !(m_fmt->oformat->flags & AVFMT_NOFILE))
            avio_closep(&m_fmt->pb);
        avformat_free_context(m_fmt);
        m_fmt = nullptr;
    }
    m_stream = nullptr;
}

ProxySource::~ProxySource()
{
    if (dec)
        avcodec_free_context(&dec);
    if (fmt)
        avformat_close_input(&fmt);
}

bool ProxySource::open(const QString &path, QString *errorOut)
{
    auto fail = [&](const char *message) {
        if (errorOut)
            *errorOut = QCoreApplication::translate("ProxyEncoder", message);
        return false;
    };

    const QByteArray pathUtf8 = path.toUtf8();
    if (avformat_open_input(&fmt, pathUtf8.constData(), nullptr, nullptr) < 0)
        return fail(QT_TRANSLATE_NOOP("ProxyEncoder", "Could not open the clip"));
    if (avformat_find_stream_info(fmt, nullptr) < 0)
        return fail(QT_TRANSLATE_NOOP("ProxyEncoder", "Could not read the clip's streams"));

    stream = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (stream < 0)
        return fail(QT_TRANSLATE_NOOP("ProxyEncoder", "The clip has no video"));

    const AVCodec *codec = avcodec_find_decoder(fmt->streams[stream]->codecpar->codec_id);
    if (!codec)
        return fail(QT_TRANSLATE_NOOP("ProxyEncoder", "No decoder for this clip"));

    dec = avcodec_alloc_context3(codec);
    if (!dec)
        return fail(QT_TRANSLATE_NOOP("ProxyEncoder", "Could not allocate the decoder"));
    if (avcodec_parameters_to_context(dec, fmt->streams[stream]->codecpar) < 0)
        return fail(QT_TRANSLATE_NOOP("ProxyEncoder", "Could not configure the decoder"));
    dec->thread_count = 0; // let libavcodec pick; this is a batch job, not playback
    if (avcodec_open2(dec, codec, nullptr) < 0)
        return fail(QT_TRANSLATE_NOOP("ProxyEncoder", "Could not open the decoder"));
    if (dec->width <= 0 || dec->height <= 0)
        return fail(QT_TRANSLATE_NOOP("ProxyEncoder", "The clip has no usable video size"));
    return true;
}

AVRational ProxySource::frameRate() const
{
    const AVStream *s = fmt->streams[stream];
    AVRational rate = s->avg_frame_rate;
    if (rate.num <= 0 || rate.den <= 0)
        rate = s->r_frame_rate;
    if (rate.num <= 0 || rate.den <= 0)
        rate = AVRational{30, 1};
    return rate;
}

} // namespace drift
