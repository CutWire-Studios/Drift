#include "engine/gpu/effects/GpuEffectExecutor.h"

#include "engine/face/props/FaceModelTransform.h"
#include "engine/face/swap/GlFaceSwapRenderer.h"
#include "engine/render3d/models/GlModelRenderer.h"
#include "engine/gpu/GlRuntime.h"
#include "engine/gpu/effects/GpuEffectDefinition.h"

#include <QMutexLocker>
#include <QOpenGLShaderProgram>

#include <cmath>
#include <vector>

using namespace drift::gl;

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
                                     drift::TimeUs timeUs, int frameRate)
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

            if (step.modelDef && step.modelDef->isFaceSwap) {
                const drift::FaceSwapParams swapParams =
                    drift::faceSwapParamsFromMap(step.parameters, step.modelDef->gpu.packageDir);
                GlTarget next = drawFaceSwapEffect(rt, gl, swapParams, step.faceSlots, current);
                if (!next.isValid())
                    continue;
                rt.releaseTarget(std::move(current));
                current = std::move(next);
                continue;
            }

            if (!step.gpu || !step.gpu->valid)
                continue;

            const std::vector<const GlTarget *> sources{&current};
            PipelineAux aux{step.depth};
            aux.frameRate = frameRate;
            GlTarget next = runPipeline(rt, gl, step.cacheKey, *step.gpu, sources, step.parameters,
                                        timeUs, 0.0, canvasSize, &aux);
            if (!next.isValid())
                continue; // grace mode: skip this effect, keep the chain going

            rt.releaseTarget(std::move(current));
            current = std::move(next);
        }

        result = rt.readTarget(current);
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
                result = rt.readTarget(canvas);
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

