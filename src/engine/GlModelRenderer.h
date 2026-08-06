#pragma once

#include "FaceLandmarker.h"
#include "FaceModelTransform.h"
#include "GlRuntime.h"

#include <QOpenGLExtraFunctions>
#include <QString>

namespace drift::gl {

// Draw a model3d face-prop effect into a new pooled target composited over `source`.
// Returns an invalid target on any failure — callers treat that as grace mode (leave source).
//
// One implementation, two call sites (GpuCompositor::buildLayerTarget and GpuEffectExecutor::
// applyChain). Both must land together or preview and facedetect diverge.
GlTarget drawFaceModelEffect(GlRuntime &rt, QOpenGLExtraFunctions *gl, const FaceModelParams &params,
                             const QList<FaceAnchors> &faceSlots, const GlTarget &source);

// Look up or upload a model. Null when the CPU load fails. Called only on the GL thread.
GlModelGpu *acquireGlModel(GlRuntime &rt, QOpenGLExtraFunctions *gl, const QString &path);

void destroyGlModels(GlRuntime &rt, QOpenGLExtraFunctions *gl);

} // namespace drift::gl
