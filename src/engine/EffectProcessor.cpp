#include "EffectProcessor.h"

#include "EffectCatalog.h"
#include "FaceTrack.h"
#include "GpuEffectExecutor.h"

#include <cstring>

extern "C" {
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavutil/imgutils.h>
}

namespace {

AVFrame *imageToFrame(const QImage &image)
{
    if (image.isNull())
        return nullptr;

    const QImage rgba = image.convertToFormat(QImage::Format_RGBA8888);
    AVFrame *frame = av_frame_alloc();
    if (!frame)
        return nullptr;

    frame->format = AV_PIX_FMT_RGBA;
    frame->width = rgba.width();
    frame->height = rgba.height();
    if (av_frame_get_buffer(frame, 32) < 0) {
        av_frame_free(&frame);
        return nullptr;
    }

    for (int y = 0; y < rgba.height(); ++y) {
        std::memcpy(frame->data[0] + y * frame->linesize[0], rgba.constScanLine(y),
                    static_cast<size_t>(rgba.bytesPerLine()));
    }

    frame->pts = 0;
    return frame;
}

QImage frameToImage(const AVFrame *frame)
{
    if (!frame || frame->width <= 0 || frame->height <= 0)
        return {};

    QImage image(frame->width, frame->height, QImage::Format_RGBA8888);
    for (int y = 0; y < frame->height; ++y) {
        std::memcpy(image.scanLine(y), frame->data[0] + y * frame->linesize[0],
                    static_cast<size_t>(image.bytesPerLine()));
    }
    return image;
}

QString buildFilterGraph(const QList<drift::Effect> &effects)
{
    QStringList parts;
    for (const drift::Effect &effect : effects) {
        const QString graph = buildFilterGraphForEffect(effect);
        if (!graph.isEmpty())
            parts.append(graph);
    }
    if (parts.isEmpty())
        return {};

    return QStringLiteral("[in] %1,format=rgba [out]").arg(parts.join(QLatin1Char(',')));
}

QImage applyLibavFilterGraph(const QImage &input, const QString &filters)
{
    if (input.isNull() || filters.isEmpty())
        return input;

    AVFilterGraph *graph = avfilter_graph_alloc();
    if (!graph)
        return input;

    const AVFilter *buffersrc = avfilter_get_by_name("buffer");
    const AVFilter *buffersink = avfilter_get_by_name("buffersink");
    if (!buffersrc || !buffersink) {
        avfilter_graph_free(&graph);
        return input;
    }

    AVFilterContext *srcCtx = nullptr;
    AVFilterContext *sinkCtx = nullptr;

    const QByteArray args = QString::asprintf("video_size=%dx%d:pix_fmt=rgba:time_base=1/30:pixel_aspect=1/1",
                                              input.width(), input.height())
                                .toUtf8();
    if (avfilter_graph_create_filter(&srcCtx, buffersrc, "in", args.constData(), nullptr, graph) < 0
        || avfilter_graph_create_filter(&sinkCtx, buffersink, "out", nullptr, nullptr, graph) < 0) {
        avfilter_graph_free(&graph);
        return input;
    }

    AVFilterInOut *outputs = avfilter_inout_alloc();
    AVFilterInOut *inputs = avfilter_inout_alloc();
    outputs->name = av_strdup("in");
    outputs->filter_ctx = srcCtx;
    outputs->pad_idx = 0;
    outputs->next = nullptr;

    inputs->name = av_strdup("out");
    inputs->filter_ctx = sinkCtx;
    inputs->pad_idx = 0;
    inputs->next = nullptr;

    const QByteArray graphDesc = filters.toUtf8();
    if (avfilter_graph_parse_ptr(graph, graphDesc.constData(), &inputs, &outputs, nullptr) < 0
        || avfilter_graph_config(graph, nullptr) < 0) {
        avfilter_inout_free(&inputs);
        avfilter_inout_free(&outputs);
        avfilter_graph_free(&graph);
        return input;
    }

    avfilter_inout_free(&inputs);
    avfilter_inout_free(&outputs);

    AVFrame *srcFrame = imageToFrame(input);
    if (!srcFrame) {
        avfilter_graph_free(&graph);
        return input;
    }

    if (av_buffersrc_add_frame_flags(srcCtx, srcFrame, AV_BUFFERSRC_FLAG_KEEP_REF) < 0) {
        av_frame_free(&srcFrame);
        avfilter_graph_free(&graph);
        return input;
    }

    AVFrame *outFrame = av_frame_alloc();
    QImage result = input;
    if (outFrame && av_buffersink_get_frame(sinkCtx, outFrame) >= 0)
        result = frameToImage(outFrame);

    av_frame_free(&outFrame);
    av_frame_free(&srcFrame);
    avfilter_graph_free(&graph);
    return result;
}

} // namespace

QImage EffectProcessor::applyEffects(const QImage &input, const QList<drift::Effect> &effects,
                                     drift::TimeUs timeUs, const QList<drift::FaceAnchors> &faceSlots)
{
    if (input.isNull() || effects.isEmpty())
        return input;

    QImage result = input;
    QList<drift::Effect> legacyLibavBatch;
    QList<GpuEffectExecutor::ChainStep> gpuChain;

    auto flushLegacy = [&]() {
        if (legacyLibavBatch.isEmpty())
            return;
        const QString filters = buildFilterGraph(legacyLibavBatch);
        if (!filters.isEmpty())
            result = applyLibavFilterGraph(result, filters);
        legacyLibavBatch.clear();
    };

    // Consecutive GPU effects run as one chain: a single upload, FBO-to-FBO
    // between effects, and a single readback — instead of a round trip each.
    auto flushGpu = [&]() {
        if (gpuChain.isEmpty())
            return;
        result = GpuEffectExecutor::instance().applyChain(gpuChain, result, timeUs);
        gpuChain.clear();
    };

    for (const drift::Effect &effect : effects) {
        const EffectPresetEntry *def =
            effect.catalogId.isEmpty() ? nullptr : effectDefForId(effect.catalogId);

        // Multi-frame trail is assembled in FrameCompositor before this call.
        if (def && def->meta.id == QStringLiteral("time_echo"))
            continue;

        if (def && def->isModel3d) {
            flushLegacy();
            QMap<QString, QVariant> params = resolvedEffectParameters(effect, *def);
            GpuEffectExecutor::ChainStep step;
            step.cacheKey = def->meta.id;
            step.modelDef = def;
            step.parameters = params;
            step.faceSlots = faceSlots;
            gpuChain.append(step);
            continue;
        }

        if (def && def->isGpu && def->gpu.valid) {
            flushLegacy();
            QMap<QString, QVariant> params = resolvedEffectParameters(effect, *def);
            if (def->needsFace)
                drift::applyFaceUniforms(&params, faceSlots);
            gpuChain.append(GpuEffectExecutor::ChainStep{def->meta.id, &def->gpu, nullptr, params, {}});
            continue;
        }

        // Legacy: uncataloged raw libavfilter graphs only.
        if (!def) {
            flushGpu();
            legacyLibavBatch.append(effect);
        }
    }

    flushGpu();
    flushLegacy();
    return result;
}
