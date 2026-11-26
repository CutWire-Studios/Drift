#include "app/farm/ChunkJoin.h"

#include <QFile>

extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
}

namespace drift::farm {

namespace {

QString avError(int code)
{
    char buffer[AV_ERROR_MAX_STRING_SIZE] = {};
    av_strerror(code, buffer, sizeof buffer);
    return QString::fromUtf8(buffer);
}

struct InputFile
{
    AVFormatContext *ctx = nullptr;
    int stream = -1;
    ~InputFile() { avformat_close_input(&ctx); }

    bool open(const QString &path, AVMediaType type, QString *error)
    {
        const QByteArray utf8 = path.toUtf8();
        int rc = avformat_open_input(&ctx, utf8.constData(), nullptr, nullptr);
        if (rc >= 0)
            rc = avformat_find_stream_info(ctx, nullptr);
        if (rc < 0) {
            *error = QStringLiteral("%1: %2").arg(path, avError(rc));
            return false;
        }
        stream = av_find_best_stream(ctx, type, -1, -1, nullptr, 0);
        if (stream < 0) {
            *error = QStringLiteral("%1 has no %2 stream").arg(path, QString::fromUtf8(av_get_media_type_string(type)));
            return false;
        }
        return true;
    }
};

} // namespace

bool joinChunks(const QStringList &chunks, const QList<qint64> &chunkFrames, int fpsNum, int fpsDen,
                const QString &audioFile, const QString &output, QString *error)
{
    if (chunks.isEmpty() || chunks.size() != chunkFrames.size()) {
        *error = QStringLiteral("Nothing to join");
        return false;
    }

    InputFile first;
    if (!first.open(chunks.first(), AVMEDIA_TYPE_VIDEO, error))
        return false;
    InputFile audio;
    if (!audioFile.isEmpty() && !audio.open(audioFile, AVMEDIA_TYPE_AUDIO, error))
        return false;

    QFile::remove(output);
    AVFormatContext *out = nullptr;
    const QByteArray outUtf8 = output.toUtf8();
    int rc = avformat_alloc_output_context2(&out, nullptr, nullptr, outUtf8.constData());
    if (rc < 0 || !out) {
        *error = QStringLiteral("Could not determine the output format");
        return false;
    }
    bool ok = false;
    AVPacket *pkt = av_packet_alloc();
    {
        AVStream *vOut = avformat_new_stream(out, nullptr);
        AVStream *aOut = audio.ctx ? avformat_new_stream(out, nullptr) : nullptr;
        const AVStream *vIn = first.ctx->streams[first.stream];
        avcodec_parameters_copy(vOut->codecpar, vIn->codecpar);
        vOut->codecpar->codec_tag = 0;
        vOut->time_base = vIn->time_base;
        vOut->avg_frame_rate = vIn->avg_frame_rate;
        if (aOut) {
            const AVStream *aIn = audio.ctx->streams[audio.stream];
            avcodec_parameters_copy(aOut->codecpar, aIn->codecpar);
            aOut->codecpar->codec_tag = 0;
            aOut->time_base = aIn->time_base;
        }
        if (!(out->oformat->flags & AVFMT_NOFILE)
            && (rc = avio_open(&out->pb, outUtf8.constData(), AVIO_FLAG_WRITE)) < 0) {
            *error = QStringLiteral("Could not write %1: %2").arg(output, avError(rc));
            goto cleanup;
        }
        if ((rc = avformat_write_header(out, nullptr)) < 0) {
            *error = QStringLiteral("Could not start the output: %1").arg(avError(rc));
            goto cleanup;
        }

        // Interleaving is the muxer's job; packets go in per chunk and then the audio, which
        // av_interleaved_write_frame reorders by dts.
        qint64 frameOffset = 0;
        for (int i = 0; i < chunks.size(); ++i) {
            InputFile chunk;
            if (!chunk.open(chunks.at(i), AVMEDIA_TYPE_VIDEO, error))
                goto cleanup;
            const AVStream *in = chunk.ctx->streams[chunk.stream];
            if (in->time_base.num != vIn->time_base.num || in->time_base.den != vIn->time_base.den
                || in->codecpar->codec_id != vIn->codecpar->codec_id) {
                *error = QStringLiteral("Chunk %1 was encoded differently from the first").arg(i);
                goto cleanup;
            }
            const int64_t shift = av_rescale_q(frameOffset, AVRational{fpsDen, fpsNum}, in->time_base);
            int64_t firstPts = AV_NOPTS_VALUE;
            while (av_read_frame(chunk.ctx, pkt) >= 0) {
                if (pkt->stream_index != chunk.stream) {
                    av_packet_unref(pkt);
                    continue;
                }
                if (firstPts == AV_NOPTS_VALUE)
                    firstPts = pkt->pts != AV_NOPTS_VALUE ? pkt->pts : pkt->dts;
                const int64_t base = shift - firstPts;
                if (pkt->pts != AV_NOPTS_VALUE)
                    pkt->pts += base;
                if (pkt->dts != AV_NOPTS_VALUE)
                    pkt->dts += base;
                pkt->stream_index = vOut->index;
                av_packet_rescale_ts(pkt, in->time_base, vOut->time_base);
                pkt->pos = -1;
                if ((rc = av_interleaved_write_frame(out, pkt)) < 0) {
                    *error = QStringLiteral("Write failed: %1").arg(avError(rc));
                    goto cleanup;
                }
            }
            frameOffset += chunkFrames.at(i);
        }

        if (aOut) {
            const AVStream *aIn = audio.ctx->streams[audio.stream];
            while (av_read_frame(audio.ctx, pkt) >= 0) {
                if (pkt->stream_index != audio.stream) {
                    av_packet_unref(pkt);
                    continue;
                }
                pkt->stream_index = aOut->index;
                av_packet_rescale_ts(pkt, aIn->time_base, aOut->time_base);
                pkt->pos = -1;
                if ((rc = av_interleaved_write_frame(out, pkt)) < 0) {
                    *error = QStringLiteral("Write failed: %1").arg(avError(rc));
                    goto cleanup;
                }
            }
        }
        if ((rc = av_write_trailer(out)) < 0) {
            *error = QStringLiteral("Could not finish the output: %1").arg(avError(rc));
            goto cleanup;
        }
        ok = true;
    }
cleanup:
    av_packet_free(&pkt);
    if (out) {
        if (out->pb && !(out->oformat->flags & AVFMT_NOFILE))
            avio_closep(&out->pb);
        avformat_free_context(out);
    }
    if (!ok)
        QFile::remove(output);
    return ok;
}

} // namespace drift::farm
