#include "MediaProbe.h"

#include <cmath>

extern "C" {
#include <libavcodec/codec_desc.h>
#include <libavformat/avformat.h>
#include <libavutil/display.h>
#include <libavutil/dict.h>
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
