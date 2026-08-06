#include "GpuEffectExecutor.h"

#include "FaceModelTransform.h"
#include "GlModelRenderer.h"
#include "GlRuntime.h"
#include "GpuEffectDefinition.h"

#include <QMutexLocker>
#include <QOpenGLShaderProgram>

#include <cmath>
#include <vector>

using namespace drift::gl;

namespace {

constexpr const char *kTimeEchoBlendFrag = R"(#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;
uniform sampler2D u_currentTexture; // overlay (newer)
uniform sampler2D u_texture1;       // base (accumulated)
uniform float opacity;
uniform float blendMode; // 0 normal, 1 add, 2 screen
void main() {
    vec4 over = texture(u_currentTexture, v_texCoord);
    vec4 base = texture(u_texture1, v_texCoord);
    float a = clamp(opacity, 0.0, 1.0) * over.a;
    vec3 rgb;
    if (blendMode > 1.5) {
        // screen
        rgb = 1.0 - (1.0 - base.rgb) * (1.0 - over.rgb);
    } else if (blendMode > 0.5) {
        rgb = base.rgb + over.rgb;
    } else {
        rgb = mix(base.rgb, over.rgb, a);
    }
    float outA = clamp(base.a + a, 0.0, 1.0);
    fragColor = vec4(clamp(rgb, 0.0, 1.0), outA);
}
)";

} // namespace

GpuEffectExecutor &GpuEffectExecutor::instance()
{
    static GpuEffectExecutor exec;
    return exec;
}

bool GpuEffectExecutor::ensureContext()
{
    return runtime().available();
}

bool GpuEffectExecutor::isAvailable()
{
    return ensureContext();
}

void GpuEffectExecutor::releaseGl()
{
    // Kept for future shutdown hooks; context lives for process lifetime.
}

QImage GpuEffectExecutor::apply(const EffectPresetEntry &def, const QImage &input,
                                const QMap<QString, QVariant> &parameters, drift::TimeUs timeUs)
{
    if (!def.isGpu)
        return input;
    return apply(def.meta.id, def.gpu, {input}, parameters, timeUs, 0.0);
}

QImage GpuEffectExecutor::applyChain(const QList<ChainStep> &steps, const QImage &input,
                                     drift::TimeUs timeUs)
{
    if (input.isNull() || steps.isEmpty() || !ensureContext())
        return input;

    GlRuntime &rt = runtime();
    QImage result = input;

    rt.exec([&] {
        auto *gl = rt.functions();
        if (!gl)
            return;

        const QSize canvasSize = input.size();
        GlTarget current = promoteImageToTarget(rt, gl, input, canvasSize);
        if (!current.isValid())
            return;

        // Each step reads the previous step's framebuffer and writes a new one.
        // The image only crosses the CPU boundary once, at the end.
        for (const ChainStep &step : steps) {
            if (step.modelDef && step.modelDef->isModel3d) {
                const drift::FaceModelParams modelParams =
                    drift::faceModelParamsFromMap(step.parameters);
                GlTarget next =
                    drawFaceModelEffect(rt, gl, modelParams, step.faceSlots, current);
                if (!next.isValid())
                    continue;
                rt.releaseTarget(std::move(current));
                current = std::move(next);
                continue;
            }

            if (!step.gpu || !step.gpu->valid)
                continue;

            const std::vector<const GlTarget *> sources{&current};
            GlTarget next = runPipeline(rt, gl, step.cacheKey, *step.gpu, sources, step.parameters,
                                        timeUs, 0.0, canvasSize);
            if (!next.isValid())
                continue; // grace mode: skip this effect, keep the chain going

            rt.releaseTarget(std::move(current));
            current = std::move(next);
        }

        result = current.fbo->toImage(false).convertToFormat(QImage::Format_RGBA8888);
        if (result.size() != canvasSize)
            result = result.scaled(canvasSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);

        rt.releaseTarget(std::move(current));
    });

    return result;
}

QImage GpuEffectExecutor::apply(const QString &cacheKey, const drift::GpuEffectDefinition &gpu,
                                const QList<QImage> &sources, const QMap<QString, QVariant> &parameters,
                                drift::TimeUs timeUs, double progress, bool *okOut)
{
    if (okOut)
        *okOut = false;

    const QImage fallback = sources.isEmpty() ? QImage() : sources.first();
    if (fallback.isNull() || !gpu.valid)
        return fallback;
    if (!ensureContext())
        return fallback;

    GlRuntime &rt = runtime();
    QImage result = fallback;
    bool ok = false;

    rt.exec([&] {
        auto *gl = rt.functions();
        if (!gl)
            return;

        const QSize canvasSize = fallback.size();
        std::vector<GlTarget> sourceTargets;
        sourceTargets.reserve(sources.size());
        bool promoteFailed = false;
        for (const QImage &src : sources) {
            // A null source (e.g. a transition side with no media) becomes
            // transparent black, which is what a shader sampling it should see.
            GlTarget target = promoteImageToTarget(rt, gl, src, canvasSize);
            if (!target.isValid()) {
                qWarning("GpuEffectExecutor: source FBO promote failed for %s", qPrintable(cacheKey));
                promoteFailed = true;
                break;
            }
            sourceTargets.push_back(std::move(target));
        }

        if (!promoteFailed) {
            std::vector<const GlTarget *> sourceRefs;
            sourceRefs.reserve(sourceTargets.size());
            for (const GlTarget &target : sourceTargets)
                sourceRefs.push_back(&target);

            GlTarget canvas =
                runPipeline(rt, gl, cacheKey, gpu, sourceRefs, parameters, timeUs, progress, canvasSize);
            if (canvas.isValid()) {
                // Sources were promoted into FBOs; all samples share that Y layout.
                // toImage(false) keeps QImage top matching the original frame top.
                result = canvas.fbo->toImage(false).convertToFormat(QImage::Format_RGBA8888);
                if (result.size() != canvasSize)
                    result = result.scaled(canvasSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
                ok = true;
                rt.releaseTarget(std::move(canvas));
            }
        }

        for (GlTarget &target : sourceTargets)
            rt.releaseTarget(std::move(target));
    });

    if (okOut)
        *okOut = ok;
    return result;
}

QImage GpuEffectExecutor::blendTimeEcho(const QList<QImage> &framesNewestFirst, double decay,
                                        int blendMode)
{
    if (framesNewestFirst.isEmpty() || !ensureContext())
        return {};

    GlRuntime &rt = runtime();
    QImage result;

    rt.exec([&] {
        auto *gl = rt.functions();
        if (!gl)
            return;

        QOpenGLShaderProgram *program = rt.builtinProgram(QStringLiteral("__time_echo_blend__"),
                                                          kQuadVertexShader, kTimeEchoBlendFrag);
        if (!program)
            return;

        const double clampedDecay = qBound(0.0, decay, 1.0);

        // Accumulate oldest → newest entirely in framebuffers. The old loop read
        // the accumulator back to a QImage and re-uploaded it on every layer,
        // which is a full pipeline stall per echo frame.
        GlTarget accumulated;
        QSize accumulatedSize;

        for (int age = framesNewestFirst.size() - 1; age >= 0; --age) {
            const QImage layer = framesNewestFirst.at(age).convertToFormat(QImage::Format_RGBA8888);
            if (layer.isNull())
                continue;
            const double opacity = age == 0 ? 1.0 : std::pow(clampedDecay, static_cast<double>(age));

            if (!accumulated.isValid()) {
                accumulated = promoteImageToTarget(rt, gl, layer, layer.size());
                if (!accumulated.isValid())
                    return;
                accumulatedSize = layer.size();
                continue;
            }
            if (accumulatedSize != layer.size())
                continue;

            const GLuint overTex = uploadTexture(gl, layer);
            GlTarget canvas = rt.acquireTarget(layer.width(), layer.height());
            if (!canvas.isValid()) {
                gl->glDeleteTextures(1, &overTex);
                rt.releaseTarget(std::move(accumulated));
                return;
            }

            canvas.fbo->bind();
            gl->glViewport(0, 0, canvas.width, canvas.height);
            gl->glDisable(GL_BLEND);
            gl->glClearColor(0.f, 0.f, 0.f, 0.f);
            gl->glClear(GL_COLOR_BUFFER_BIT);

            program->bind();
            program->setUniformValue("u_currentTexture", 0);
            program->setUniformValue("u_texture1", 1);
            program->setUniformValue("opacity", float(opacity));
            program->setUniformValue("blendMode", float(blendMode));
            gl->glActiveTexture(GL_TEXTURE0);
            gl->glBindTexture(GL_TEXTURE_2D, overTex);
            gl->glActiveTexture(GL_TEXTURE1);
            gl->glBindTexture(GL_TEXTURE_2D, accumulated.texture());
            gl->glBindVertexArray(rt.vao);
            gl->glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
            gl->glBindVertexArray(0);
            program->release();
            canvas.fbo->release();

            gl->glDeleteTextures(1, &overTex);
            rt.releaseTarget(std::move(accumulated));
            accumulated = std::move(canvas);
        }

        if (accumulated.isValid()) {
            result = accumulated.fbo->toImage(false).convertToFormat(QImage::Format_RGBA8888);
            rt.releaseTarget(std::move(accumulated));
        }
    });

    return result;
}
