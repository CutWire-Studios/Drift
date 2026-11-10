#pragma once

#include "engine/gpu/GlRuntime.h"

#include <QMatrix4x4>
#include <QOpenGLExtraFunctions>
#include <QSize>

#include <array>

namespace drift {
struct ProceduralRequest;
}

namespace drift::gl {

// Draw a procedural clip into a new pooled, straight-alpha layer target the size of the canvas,
// sibling of drawModelClip.
//
// With a scene camera of the clip's own (request.ownCamera()) the scene is drawn through it, flat,
// and `viewProjection` and `depthOut` are ignored. Otherwise the geometry is project-world and is
// seen through `viewProjection` (world -> homogeneous canvas px at render scale, as
// cameraViewProjection and the 3D view give it); with `depthOut`, the depth the shaders produced
// (including any gl_FragDepth) is packed for the compositor's depth test like a model's.
//
// Invalid on failure, which the compositor reads as "draw nothing". Shader compile failures are
// recorded in ProceduralDiagnostics. GL thread only.
GlTarget drawProceduralClip(GlRuntime &rt, QOpenGLExtraFunctions *gl, const ProceduralRequest &request,
                            const QSize &canvasSize, const QMatrix4x4 &viewProjection,
                            GlTarget *depthOut = nullptr,
                            const std::array<float, 3> &globalAudio = {});

void destroyGlProcedurals(GlRuntime &rt, QOpenGLExtraFunctions *gl);

} // namespace drift::gl
