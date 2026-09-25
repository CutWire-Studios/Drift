#pragma once

extern "C" {
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
#include <libswscale/swscale.h>
}

// Shared by every decode-side swscale user (ClipReader, MediaThumbnail, ProxyEncoder,
// GlRuntime) so the deprecated-pixel-format normalization and the color
// range it depends on can't drift out of sync between call sites again.
namespace drift {

// Decoders that emit a YUVJ* format (e.g. MJPEG) always set AVFrame.color_range to
// AVCOL_RANGE_JPEG in lockstep, so the actual range is recovered from color_range rather
// than the pixel format itself. Passing the deprecated YUVJ* enum straight into swscale
// gets nothing extra and makes it log "deprecated pixel format used" on every frame.
inline AVPixelFormat swsSourceFormat(AVPixelFormat fmt)
{
    switch (fmt) {
    case AV_PIX_FMT_YUVJ420P:
        return AV_PIX_FMT_YUV420P;
    case AV_PIX_FMT_YUVJ422P:
        return AV_PIX_FMT_YUV422P;
    case AV_PIX_FMT_YUVJ444P:
        return AV_PIX_FMT_YUV444P;
    case AV_PIX_FMT_YUVJ440P:
        return AV_PIX_FMT_YUV440P;
    case AV_PIX_FMT_YUVJ411P:
        return AV_PIX_FMT_YUV411P;
    default:
        return fmt;
    }
}

inline int swsColorspaceFromFrame(const AVFrame *frame)
{
    if (!frame)
        return SWS_CS_ITU709;
    switch (frame->colorspace) {
    case AVCOL_SPC_BT470BG:
    case AVCOL_SPC_SMPTE170M:
        return SWS_CS_ITU601;
    case AVCOL_SPC_SMPTE240M:
        return SWS_CS_SMPTE240M;
    case AVCOL_SPC_FCC:
        return SWS_CS_FCC;
    case AVCOL_SPC_BT709:
    case AVCOL_SPC_BT2020_NCL:
    case AVCOL_SPC_BT2020_CL:
        return SWS_CS_ITU709;
    case AVCOL_SPC_UNSPECIFIED:
    default:
        // Drift's SDR pipeline defaults to BT.709 when the bitstream is untagged.
        return SWS_CS_ITU709;
    }
}

// Tells an existing scaler the source's actual sample range — recovered from color_range
// since the deprecated YUVJ* pixel formats are normalized away via swsSourceFormat() before
// reaching swscale — so full-range MJPEG/YUVJ-sourced frames aren't silently treated as
// limited range. dstRange is 1 for full-range (e.g. an RGB destination meant for display)
// or 0 for limited range.
inline void configureSwsRange(SwsContext *sws, const AVFrame *src, int dstRange)
{
    if (!sws || !src)
        return;
    const int *coeff = sws_getCoefficients(swsColorspaceFromFrame(src));
    // Unspecified range is treated as limited (MPEG/TV) — the common case for camera footage —
    // unless the format is YUVJ*, which is full range by definition.
    const bool yuvj = swsSourceFormat(static_cast<AVPixelFormat>(src->format)) != src->format;
    const int srcRange = src->color_range == AVCOL_RANGE_JPEG
                                 || (src->color_range == AVCOL_RANGE_UNSPECIFIED && yuvj)
                             ? 1
                             : 0;
    sws_setColorspaceDetails(sws, coeff, srcRange, coeff, dstRange, 0, 1 << 16, 1 << 16);
}

// Range-preserving variant for a YUV/NV12 destination that keeps the source's own range tag
// (e.g. the caller copies src->color_range onto the output frame) rather than converting to
// RGB for display, so the destination range must match the source's, not always full.
inline void configureSwsRangePreserving(SwsContext *sws, const AVFrame *src)
{
    if (!src)
        return;
    const int srcRange = src->color_range == AVCOL_RANGE_JPEG ? 1 : 0;
    configureSwsRange(sws, src, srcRange);
}

} // namespace drift
