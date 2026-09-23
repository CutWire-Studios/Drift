#include "MediaProbe.h"

#include <algorithm>
#include <cmath>
#include <vector>

extern "C" {
#include <libavcodec/codec_desc.h>
#include <libavcodec/codec_id.h>
#include <libavcodec/defs.h>
#include <libavformat/avformat.h>
#include <libavutil/display.h>
#include <libavutil/dict.h>
#include <libavutil/pixdesc.h>
}

int displayRotationOf(const AVStream *stream)
{
    const AVPacketSideData *sd = av_packet_side_data_get(stream->codecpar->coded_side_data,
                                                          stream->codecpar->nb_coded_side_data,
                                                          AV_PKT_DATA_DISPLAYMATRIX);
    if (!sd)
        return 0;

    double angle = av_display_rotation_get(reinterpret_cast<const int32_t *>(sd->data));
    if (std::isnan(angle))
        angle = 0;

    // Normalize to one of 0/90/180/270, matching how players interpret it.
    int rounded = static_cast<int>(std::lround(-angle));
    rounded %= 360;
    if (rounded < 0)
        rounded += 360;
    return rounded;
}

bool videoStreamHasAlpha(const AVStream *stream)
{
    if (!stream || !stream->codecpar)
        return false;
    const AVCodecParameters *par = stream->codecpar;

    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(static_cast<AVPixelFormat>(par->format));
    if (desc && (desc->flags & AV_PIX_FMT_FLAG_ALPHA))
        return true;

    if (const AVDictionaryEntry *alpha = av_dict_get(stream->metadata, "alpha_mode", nullptr, 0)) {
        if (alpha->value && alpha->value[0] && alpha->value[0] != '0')
            return true;
    }

    // ProRes 4444 / 4444 XQ carry an alpha plane even when pix_fmt is still unset at probe.
    if (par->codec_id == AV_CODEC_ID_PRORES
        && (par->profile == AV_PROFILE_PRORES_4444 || par->profile == AV_PROFILE_PRORES_XQ))
        return true;

    return false;
}

namespace {

StreamInfo describeStream(const AVFormatContext *fmt, const AVStream *stream)
{
    StreamInfo info;
    info.streamIndex = stream->index;
    const AVCodecParameters *par = stream->codecpar;

    const AVCodecDescriptor *desc = avcodec_descriptor_get(par->codec_id);
    info.codecName = desc ? QString::fromUtf8(desc->name) : QStringLiteral("unknown");

    if (const AVDictionaryEntry *titleTag = av_dict_get(stream->metadata, "title", nullptr, 0)) {
        if (titleTag->value)
            info.title = QString::fromUtf8(titleTag->value).trimmed();
    }
    if (const AVDictionaryEntry *langTag = av_dict_get(stream->metadata, "language", nullptr, 0)) {
        if (langTag->value)
            info.language = QString::fromUtf8(langTag->value).trimmed();
    }

    if (stream->duration != AV_NOPTS_VALUE) {
        info.durationUs = av_rescale_q(stream->duration, stream->time_base, {1, AV_TIME_BASE});
    } else if (fmt->duration != AV_NOPTS_VALUE) {
        info.durationUs = fmt->duration;
    }

    switch (par->codec_type) {
    case AVMEDIA_TYPE_VIDEO:
        info.type = StreamInfo::Type::Video;
        info.width = par->width;
        info.height = par->height;
        if (stream->avg_frame_rate.den != 0)
            info.fps = av_q2d(stream->avg_frame_rate);
        info.rotationDegrees = displayRotationOf(stream);
        info.attachedPicture = (stream->disposition & AV_DISPOSITION_ATTACHED_PIC) != 0;
        info.hasAlpha = videoStreamHasAlpha(stream);
        if (const AVPixFmtDescriptor *pixDesc =
                av_pix_fmt_desc_get(static_cast<AVPixelFormat>(par->format)))
            info.bitDepth = pixDesc->comp[0].depth;
        break;
    case AVMEDIA_TYPE_AUDIO:
        info.type = StreamInfo::Type::Audio;
        info.sampleRate = par->sample_rate;
        info.channels = par->ch_layout.nb_channels;
        break;
    case AVMEDIA_TYPE_SUBTITLE:
        info.type = StreamInfo::Type::Subtitle;
        break;
    default:
        info.type = StreamInfo::Type::Other;
        break;
    }

    return info;
}

} // namespace

MediaInfo MediaProbe::probe(const QString &path)
{
    MediaInfo result;
    result.path = path;

    AVFormatContext *fmt = nullptr;
    const QByteArray pathUtf8 = path.toUtf8();

    int rc = avformat_open_input(&fmt, pathUtf8.constData(), nullptr, nullptr);
    if (rc < 0) {
        char errBuf[AV_ERROR_MAX_STRING_SIZE] = {};
        av_strerror(rc, errBuf, sizeof(errBuf));
        result.errorString = QString::fromUtf8(errBuf);
        return result;
    }

    rc = avformat_find_stream_info(fmt, nullptr);
    if (rc < 0) {
        char errBuf[AV_ERROR_MAX_STRING_SIZE] = {};
        av_strerror(rc, errBuf, sizeof(errBuf));
        result.errorString = QString::fromUtf8(errBuf);
        avformat_close_input(&fmt);
        return result;
    }

    result.durationUs = fmt->duration != AV_NOPTS_VALUE ? fmt->duration : 0;

    int audioOrdinal = 0;
    for (unsigned i = 0; i < fmt->nb_streams; ++i) {
        StreamInfo s = describeStream(fmt, fmt->streams[i]);
        if (s.type == StreamInfo::Type::Audio) {
            s.audioStreamOrdinal = audioOrdinal++;
        }
        result.streams.append(s);
    }

    result.ok = true;
    avformat_close_input(&fmt);
    return result;
}

QList<StreamInfo> MediaProbe::audioStreams(const QString &path)
{
    const MediaInfo info = probe(path);
    if (!info.ok)
        return {};
    QList<StreamInfo> out;
    for (const StreamInfo &s : info.streams) {
        if (s.type == StreamInfo::Type::Audio)
            out.append(s);
    }
    return out;
}

bool MediaProbe::isVariableFrameRate(const QString &path)
{
    // Enough packets to see a phone's rate wander, few enough that import stays quick on 4K.
    constexpr int kMaxPackets = 300;
    constexpr int64_t kMaxScanUs = 10 * int64_t(AV_TIME_BASE);

    AVFormatContext *fmt = nullptr;
    const QByteArray pathUtf8 = path.toUtf8();
    if (avformat_open_input(&fmt, pathUtf8.constData(), nullptr, nullptr) < 0)
        return false;
    if (avformat_find_stream_info(fmt, nullptr) < 0) {
        avformat_close_input(&fmt);
        return false;
    }
    const int index = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (index < 0 || (fmt->streams[index]->disposition & AV_DISPOSITION_ATTACHED_PIC)) {
        avformat_close_input(&fmt);
        return false;
    }
    const AVStream *stream = fmt->streams[index];
    const int64_t maxScanTs = av_rescale_q(kMaxScanUs, {1, AV_TIME_BASE}, stream->time_base);

    std::vector<int64_t> pts;
    AVPacket *packet = av_packet_alloc();
    while (packet && int(pts.size()) < kMaxPackets && av_read_frame(fmt, packet) >= 0) {
        if (packet->stream_index == index && packet->pts != AV_NOPTS_VALUE) {
            pts.push_back(packet->pts);
            if (packet->pts - pts.front() > maxScanTs) {
                av_packet_unref(packet);
                break;
            }
        }
        av_packet_unref(packet);
    }
    av_packet_free(&packet);
    avformat_close_input(&fmt);

    if (pts.size() < 10)
        return false;
    // Sorted to undo B-frame reordering, which scrambles packet order but not frame spacing.
    std::sort(pts.begin(), pts.end());
    std::vector<int64_t> deltas;
    deltas.reserve(pts.size() - 1);
    for (size_t i = 1; i < pts.size(); ++i) {
        if (pts[i] > pts[i - 1])
            deltas.push_back(pts[i] - pts[i - 1]);
    }
    if (deltas.size() < 9)
        return false;
    std::vector<int64_t> sorted = deltas;
    std::nth_element(sorted.begin(), sorted.begin() + sorted.size() / 2, sorted.end());
    const double median = double(sorted[sorted.size() / 2]);
    // A mux rounding to the time base's grid jitters every delta by a tick or two; only a real
    // gap or burst moves one by a quarter of a frame.
    const auto irregular = std::count_if(deltas.begin(), deltas.end(), [median](int64_t delta) {
        return std::abs(double(delta) - median) > median * 0.25;
    });
    return irregular * 20 > int64_t(deltas.size());
}
