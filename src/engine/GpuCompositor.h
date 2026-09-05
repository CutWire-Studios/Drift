#pragma once

#include "core/Clip.h"
#include "core/Effect.h"
#include "core/Mask.h"
#include "core/Time.h"
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

namespace drift {
struct GpuEffectDefinition;
}

// A single textured layer: the clip's source pixels plus everything needed to
// place it on the canvas. Prefer `video` when set (hardware frames stay on the
// GPU until the importer); otherwise `source` is CPU RGBA (still image, or a
// QPainter raster of a text/shape clip). The GPU does the scaling, rotation,
// masking and blending.
struct GpuLayer
{
    QImage source; // null => fully transparent layer (unless video is set)
    PreviewVideoFrame video;
    QList<drift::Effect> effects;
    drift::Mask mask;
    QImage matte; // MaskShape::Matte only: this frame's coverage map, decoded by FrameCompositor
    QRectF rect;             // destination rect on the canvas, in canvas pixels
    double rotation = 0.0;   // degrees, clockwise, about the rect centre
    bool flipH = false;
    bool flipV = false;
    double opacity = 1.0;
    drift::TimeUs clipTimeUs = 0; // effect time base (relative to clip start)
    // This frame's baked face anchors, one per tracked slot, sampled by FrameCompositor. Carried
    // by value: the scene outlives the cache lookup that produced them.
    QList<drift::FaceAnchors> faceSlots;
    bool valid = false;

    bool hasPixels() const { return video.isValid() || !source.isNull(); }
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
inline constexpr int kExportNv12Slots = 2;
bool beginExportNv12(const GpuScene &scene, int outW, int outH, int slot);
bool finishExportNv12(int slot, uint8_t *y, int yStride, uint8_t *uv, int uvStride, int width,
                      int height);

bool isAvailable();

// Why the compositor is unusable, for the preview's error state and the debug
// report. A snapshot: unlike isAvailable() it never forces a bring-up attempt,
// so call that first when you want one made.
drift::gl::GlStatusInfo status();

// How the last preview video frame reached the GPU: "cuda-interop", "vaapi-dmabuf",
// "cpu-roundtrip", or "none". A stable untranslated id, like drift::gl::statusId() — each
// presentation site maps it to its own catalogue. Exposed here rather than from GlRuntime so
// the playback layer can read it without pulling in the engine-private runtime header.
QString previewUploadPathId();

// How many preview composites may be in flight at once. The presentation ring holds one
// target per in-flight frame plus the one the scene graph is still sampling, so this is the
// ring depth less one — going past it would have a worker draw into the target on screen.
// GpuCompositor.cpp static_asserts the ring against this so the two cannot drift apart.
inline constexpr int kMaxPreviewComposites = 2;

} // namespace GpuCompositor
