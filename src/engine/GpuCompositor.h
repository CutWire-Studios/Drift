#pragma once

#include "core/Clip.h"
#include "core/Effect.h"
#include "core/Mask.h"
#include "core/Time.h"
#include "engine/DepthSidecar.h"
#include "engine/FaceLandmarker.h"
#include "engine/GpuStatus.h"
#include "engine/PreviewVideoFrame.h"

#include <QColor>
#include <QImage>
#include <QList>
#include <QMap>
#include <QRectF>
#include <QSize>
#include <QVariant>

#include <cstdint>
#include <memory>

namespace drift {
struct GpuEffectDefinition;
}
namespace drift::skia {
class VectorPainter;
}
namespace drift::model3d {
struct ModelDrawRequest;
}

// One frame of a media mask's pixels. A video decodes through the preview path like any clip, so
// it uploads as YUV (or imports on the GPU) instead of being converted to RGBA on the CPU first,
// and it shares the reader cache the compositor's warm-up filled. A still stays an image.
struct MaskMediaFrame
{
    PreviewVideoFrame video;
    QImage image;

    bool isNull() const { return !video.isValid() && image.isNull(); }
    QSize size() const
    {
        return video.isValid() ? QSize(video.displayWidth(), video.displayHeight()) : image.size();
    }
};

// A single textured layer: the clip's source pixels plus everything needed to
// place it on the canvas. Prefer `video` when set (hardware frames stay on the
// GPU until the importer); then `vector`, drawn by Skia straight into the layer
// target; otherwise `source` is CPU RGBA (still image, or a QPainter raster of a
// text/shape clip). The GPU does the scaling, rotation, masking and blending.
struct GpuLayer
{
    QImage source; // null => fully transparent layer (unless video or vector is set)
    PreviewVideoFrame video;
    // Skia-drawn content (text, shapes, Lottie). Declared whether or not Skia is compiled in so
    // the struct has one layout; without DRIFT_WITH_SKIA nothing ever sets it. When Skia cannot
    // draw it, `source` is the fallback if the builder filled one.
    std::shared_ptr<const drift::skia::VectorPainter> vector;
    // A glTF model drawn by GlModelRenderer straight into a full-canvas layer target.
    std::shared_ptr<const drift::model3d::ModelDrawRequest> model3d;
    // A composite clip's nested timeline, composed into a canvas-sized layer target.
    std::shared_ptr<const struct GpuScene> nested;
    QList<drift::Effect> effects;
    QList<drift::Mask> masks;
    // Index-parallel with `masks`: this frame's decoded coverage map for each Media entry, null
    // for parametric ones. Decoded by FrameCompositor, which is the only place that knows the time.
    QList<MaskMediaFrame> maskMedia;
    // The decontaminated foreground, when a lone media mask carries one. Single rather than
    // index-parallel: it replaces the layer's colour outright, which only makes sense when one
    // media mask owns the coverage — see soleMediaIndex.
    MaskMediaFrame fgr;
    QRectF rect;             // destination rect on the canvas, in canvas pixels
    double rotation = 0.0;   // degrees, clockwise, about the rect centre
    bool flipH = false;
    bool flipV = false;
    double opacity = 1.0;
    drift::TimeUs clipTimeUs = 0; // effect time base (relative to clip start)
    // This frame's baked face anchors, one per tracked slot, sampled by FrameCompositor. Carried
    // by value: the scene outlives the cache lookup that produced them.
    QList<drift::FaceAnchors> faceSlots;
    // This frame's depth map, when the chain has a "requires": "depth" effect and the clip has
    // been estimated. Shared, not copied: the sidecar's frame cache owns it.
    std::shared_ptr<const drift::DepthFrame> depth;
    // Depth occlusion ("depth.occlude"). On an occluder, emitDepthCanvas asks the compositor to
    // lay `depth` out on the canvas once the layer is drawn. On an occluded layer, occluderItem
    // is the scene index of that occluder, and wherever it is nearer than occludeDepth this layer
    // gives way.
    bool emitDepthCanvas = false;
    int occluderItem = -1;
    float occludeDepth = 0.f;
    float occludeSoftness = 0.f;
    bool occludeCutout = false;
    bool valid = false;

    bool hasPixels() const
    {
        return video.isValid() || vector != nullptr || model3d != nullptr || nested != nullptr
               || !source.isNull();
    }
};

// One drawable in the scene: either a plain layer, or a transition that mixes
// two isolated layers through a shader.
struct GpuItem
{
    bool isTransition = false;
    bool isAdjustment = false;
    drift::BlendMode blend = drift::BlendMode::Normal;

    GpuLayer layer; // when !isTransition

    GpuLayer from; // when isTransition
    GpuLayer to;
    QString transitionKey;
    const drift::GpuEffectDefinition *transitionGpu = nullptr;
    QMap<QString, QVariant> transitionParams;
    double progress = 0.0;
    drift::TimeUs transitionTimeUs = 0;
};

// Everything needed to render one composited frame, with no reference to the
// project model. FrameCompositor builds this (pure data, no pixels touched);
// GpuCompositor renders it.
struct GpuScene
{
    QSize canvasSize;
    QColor backgroundColor = Qt::black;
    bool backgroundBlur = false;
    double blurStrengthPx = 20.0;
    QImage blurSource; // bottommost visual frame, already decoded
    QList<GpuItem> items; // back-to-front
    // Paused preview: keep converted video sources on the GPU between composites.
    bool cacheVideoSources = false;
};

// A composited frame still living in GPU memory. The texture belongs to the GL
// runtime's presentation ring — the receiver must not delete it, and must stop
// using it once a few more frames have been composited.
struct GpuFrameTexture
{
    unsigned int textureId = 0;
    QSize size;
    // Readback fallback, only ever filled on Android and only when the driver
    // refused to put the compositor's context in the scene graph's share group,
    // which makes textureId unusable there. Premultiplied ARGB32, ready to upload.
    QImage image;

    bool isValid() const { return !size.isEmpty() && (textureId != 0 || !image.isNull()); }
};

// Composites a GpuScene entirely on the GPU. Returns a null image if OpenGL is
// unavailable, which lets FrameCompositor fall back to its CPU path.
namespace GpuCompositor {

// Composite and read back to a QImage. Used by export, thumbnails and tools.
QImage render(const GpuScene &scene);

// Composite and leave the result on the GPU. Used by the preview, which hands
// the texture straight to the scene graph — no readback, no re-upload.
GpuFrameTexture renderToTexture(const GpuScene &scene);

// Export path: compose, convert to BT.709 limited NV12, pack into an async
// PBO. `slot` is 0 .. kExportNv12Slots-1. finishExportNv12 waits and copies.
inline constexpr int kExportNv12Slots = 3;
// `forCuda` leaves the planes in GL textures for finishExportNv12ToCuda instead of packing
// them for readback; the two finishers are not interchangeable for a given slot.
bool beginExportNv12(const GpuScene &scene, int outW, int outH, int slot, bool forCuda = false);
bool finishExportNv12(int slot, uint8_t *y, int yStride, uint8_t *uv, int uvStride, int width,
                      int height);
// NVENC's frame filled device-side from the slot's planes: no readback, no re-upload. `dst`
// is an AV_PIX_FMT_CUDA/NV12 frame from the encoder's frames context.
bool finishExportNv12ToCuda(int slot, AVFrame *dst);

bool isAvailable();

// Why the compositor is unusable, for the preview's error state and the debug
// report. A snapshot: unlike isAvailable() it never forces a bring-up attempt,
// so call that first when you want one made.
drift::gl::GlStatusInfo status();

// How the last preview video frame reached the GPU: "cuda-interop", "vaapi-dmabuf",
// "d3d11-interop", "mediacodec-image", "cpu-roundtrip", or "none". A stable untranslated id, like drift::gl::statusId() — each
// presentation site maps it to its own catalogue. Exposed here rather than from GlRuntime so
// the playback layer can read it without pulling in the engine-private runtime header.
QString previewUploadPathId();

// Why a zero-copy importer turned the last preview frame down, in the importer's own words, or
// empty when none was asked. Paired with previewUploadPathId(): "cpu-roundtrip" with a reason
// is an importer declining, without one it is simply a frame no importer was offered. Exposed
// here for the same reason as the id above.
QString zeroCopyDeclineReason();

// How many preview composites may be in flight at once. The presentation ring holds one
// target per in-flight frame plus the one the scene graph is still sampling, so this is the
// ring depth less one — going past it would have a worker draw into the target on screen.
// GpuCompositor.cpp static_asserts the ring against this so the two cannot drift apart.
inline constexpr int kMaxPreviewComposites = 2;

// True when the render GPU is a Sandy/Ivy Intel iGPU: Auto decode stays on
// software and the compositor runs one frame at a time. PCI of the sole adapter
// answers before GL is up; the renderer string answers after.
bool previewGpuIsLimited();

} // namespace GpuCompositor
