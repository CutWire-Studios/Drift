#pragma once

#include "core/Clip.h"
#include "core/Effect.h"
#include "core/Mask.h"
#include "core/Time.h"
#include "engine/FaceLandmarker.h"

#include <QByteArray>
#include <QColor>
#include <QImage>
#include <QList>
#include <QMap>
#include <QRectF>
#include <QSize>
#include <QVariant>

namespace drift {
struct GpuEffectDefinition;
}

// A single textured layer: the clip's source pixels plus everything needed to
// place it on the canvas. Prefer `nv12` when set (half the upload bandwidth vs
// RGBA); otherwise `source` is CPU RGBA (decoded video, or a QPainter raster of
// a text/shape clip). The GPU does the scaling, rotation, masking and blending.
struct GpuLayer
{
    QImage source; // null => fully transparent layer (unless nv12 is set)
    // Semi-planar NV12: Y plane (w*h) then interleaved UV (w*(h/2)). When
    // non-empty, the compositor uploads Y/UV and converts on the GPU.
    QByteArray nv12;
    int nv12Width = 0;
    int nv12Height = 0;
    QList<drift::Effect> effects;
    QList<drift::Mask> masks;
    // Index-parallel with `masks`: this frame's decoded coverage map for each Matte entry, null
    // for parametric ones. Decoded by FrameCompositor, which is the only place that knows the time.
    QList<QImage> maskMedia;
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

    bool hasPixels() const { return !nv12.isEmpty() || !source.isNull(); }
};

// One drawable in the scene: either a plain layer, or a transition that mixes
// two isolated layers through a shader.
struct GpuItem
{
    bool isTransition = false;
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

    bool isValid() const { return textureId != 0 && !size.isEmpty(); }
};

// Composites a GpuScene entirely on the GPU. Returns a null image if OpenGL is
// unavailable, which lets FrameCompositor fall back to its CPU path.
namespace GpuCompositor {

// Composite and read back to a QImage. Used by export, thumbnails and tools.
QImage render(const GpuScene &scene);

// Composite and leave the result on the GPU. Used by the preview, which hands
// the texture straight to the scene graph — no readback, no re-upload.
GpuFrameTexture renderToTexture(const GpuScene &scene);

bool isAvailable();

} // namespace GpuCompositor
