#pragma once

#include "FaceLandmarker.h"
#include "FaceModelTransform.h"
#include "GlRuntime.h"

#include <QOpenGLExtraFunctions>
#include <QSize>
#include <QString>

#include <memory>

namespace drift::model3d {
struct ModelDrawRequest;
}

namespace drift::gl {

// Restores depth/cull/blend/colour-mask/depth-mask on every exit path. Nothing else in GlRuntime
// enables GL_DEPTH_TEST or GL_CULL_FACE, so a leak here makes later fullscreen quads vanish
// intermittently on some drivers.
struct GlStateGuard
{
    QOpenGLExtraFunctions *gl;
    GLboolean depthTest = GL_FALSE;
    GLboolean cullFace = GL_FALSE;
    GLboolean blend = GL_FALSE;
    GLboolean depthMask = GL_TRUE;
    GLboolean colorMask[4] = {GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE};
    GLint depthFunc = GL_LESS;
    GLint cullFaceMode = GL_BACK;
    GLint frontFace = GL_CCW;

    explicit GlStateGuard(QOpenGLExtraFunctions *g)
        : gl(g)
    {
        depthTest = gl->glIsEnabled(GL_DEPTH_TEST);
        cullFace = gl->glIsEnabled(GL_CULL_FACE);
        blend = gl->glIsEnabled(GL_BLEND);
        gl->glGetBooleanv(GL_DEPTH_WRITEMASK, &depthMask);
        gl->glGetBooleanv(GL_COLOR_WRITEMASK, colorMask);
        gl->glGetIntegerv(GL_DEPTH_FUNC, &depthFunc);
        gl->glGetIntegerv(GL_CULL_FACE_MODE, &cullFaceMode);
        gl->glGetIntegerv(GL_FRONT_FACE, &frontFace);
    }

    ~GlStateGuard()
    {
        if (depthTest)
            gl->glEnable(GL_DEPTH_TEST);
        else
            gl->glDisable(GL_DEPTH_TEST);
        if (cullFace)
            gl->glEnable(GL_CULL_FACE);
        else
            gl->glDisable(GL_CULL_FACE);
        if (blend)
            gl->glEnable(GL_BLEND);
        else
            gl->glDisable(GL_BLEND);
        gl->glDepthMask(depthMask);
        gl->glColorMask(colorMask[0], colorMask[1], colorMask[2], colorMask[3]);
        gl->glDepthFunc(GLenum(depthFunc));
        gl->glCullFace(GLenum(cullFaceMode));
        gl->glFrontFace(GLenum(frontFace));
    }
};

// Draw a model3d face-prop effect into a new pooled target composited over `source`.
// Returns an invalid target on any failure — callers treat that as grace mode (leave source).
//
// One implementation, two call sites (GpuCompositor::buildLayerTarget and GpuEffectExecutor::
// applyChain), reached from three routing points — EffectProcessor batches this backend into the
// GPU chain. All must land together or preview and facedetect diverge.
GlTarget drawFaceModelEffect(GlRuntime &rt, QOpenGLExtraFunctions *gl, const FaceModelParams &params,
                             const QList<FaceAnchors> &faceSlots, const GlTarget &source);

// Resolve a face overlay onto `source`: box-downsample it to the source size when it was drawn
// supersampled, then composite it over premultiplied. Consumes `overlay` either way, and returns
// an invalid target on failure (having released everything it took).
//
// Shared with the face-swap renderer so the two effects cannot drift apart on supersample
// resolution or blend maths. It reuses the same "face_model_downsample" / "face_model_composite"
// program ids, so both paths hit the same cached programs.
GlTarget resolveFaceOverlay(GlRuntime &rt, QOpenGLExtraFunctions *gl, GlTarget &&overlay,
                            const GlTarget &source);

// Look up or upload a model. Null when the CPU load fails. Called only on the GL thread.
GlModelGpu *acquireGlModel(GlRuntime &rt, QOpenGLExtraFunctions *gl, const QString &path);
// Same, from an asset the caller already resolved so the upload matches its parse exactly.
GlModelGpu *acquireGlModel(GlRuntime &rt, QOpenGLExtraFunctions *gl, const QString &path,
                           std::shared_ptr<const ModelAsset> cpu);

// Draw a Model3d clip into a new pooled, straight-alpha layer target the size of the canvas.
// Invalid on any failure (the compositor then draws nothing for the clip).
GlTarget drawModelClip(GlRuntime &rt, QOpenGLExtraFunctions *gl,
                       const model3d::ModelDrawRequest &request, const QSize &canvasSize);

void destroyGlModels(GlRuntime &rt, QOpenGLExtraFunctions *gl);

} // namespace drift::gl
