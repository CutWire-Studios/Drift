#pragma once

#include <QMetaType>

#include <memory>

extern "C" {
#include <libavutil/frame.h>
#include <libavutil/pixdesc.h>
#include <libavutil/pixfmt.h>
}

// A decoded preview frame still owned as an AVFrame: hardware surfaces stay in the
// decoder pool, software frames stay in their original (or NV12) system-memory
// buffers. The GL importer samples this on the compositor's GL thread — nothing
// here is packed for CPU upload.
struct PreviewVideoFrame
{
    std::shared_ptr<AVFrame> frame;
    // Display-matrix rotation the GL convert shader applies (0/90/180/270). The
    // stored frame is coded-orientation; displayWidth/Height swap on 90/270.
    int rotation = 0;
    int colorspace = AVCOL_SPC_UNSPECIFIED;
    int colorRange = AVCOL_RANGE_UNSPECIFIED;

    // VAAPI and VideoToolbox surfaces live in data[3] and leave data[0] null, so testing
    // data[0] alone rejected every one of them — and ClipReader reads that rejection as a
    // decoder failure and disables hardware decode for the rest of the reader's life.
    //
    // data[3] is not a pointer either: for VAAPI it is a VASurfaceID cast to one, and id 0 is
    // a legal surface that iHD hands out first. So what says a hardware frame holds a surface
    // is the buffer reference, not either data slot.
    bool isValid() const
    {
        if (!frame || frame->width <= 0 || frame->height <= 0)
            return false;
        if (isHardware())
            return frame->buf[0] != nullptr;
        return frame->data[0] != nullptr;
    }

    int codedWidth() const { return frame ? frame->width : 0; }
    int codedHeight() const { return frame ? frame->height : 0; }

    int displayWidth() const
    {
        return (rotation == 90 || rotation == 270) ? codedHeight() : codedWidth();
    }
    int displayHeight() const
    {
        return (rotation == 90 || rotation == 270) ? codedWidth() : codedHeight();
    }

    bool isHardware() const
    {
        if (!frame)
            return false;
        const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(static_cast<AVPixelFormat>(frame->format));
        return desc && (desc->flags & AV_PIX_FMT_FLAG_HWACCEL);
    }
};

inline bool pixelFormatHasAlpha(AVPixelFormat fmt)
{
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(fmt);
    return desc && (desc->flags & AV_PIX_FMT_FLAG_ALPHA);
}

inline void avFrameDeleter(AVFrame *f)
{
    av_frame_free(&f);
}

// Clones `src` (refs hardware buffers, copies nothing) and snapshots colour/rotation.
inline PreviewVideoFrame makePreviewFrame(const AVFrame *src, int rotation)
{
    PreviewVideoFrame out;
    if (!src)
        return out;
    AVFrame *clone = av_frame_clone(src);
    if (!clone)
        return out;
    out.frame.reset(clone, avFrameDeleter);
    out.rotation = rotation;
    out.colorspace = src->colorspace;
    out.colorRange = src->color_range;
    return out;
}

// Takes ownership of `owned`.
inline PreviewVideoFrame takePreviewFrame(AVFrame *owned, int rotation)
{
    PreviewVideoFrame out;
    if (!owned)
        return out;
    out.frame.reset(owned, avFrameDeleter);
    out.rotation = rotation;
    out.colorspace = owned->colorspace;
    out.colorRange = owned->color_range;
    return out;
}

Q_DECLARE_METATYPE(PreviewVideoFrame)
