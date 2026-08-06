#pragma once

#include "EffectCatalog.h"
#include "FaceLandmarker.h"
#include "GpuEffectDefinition.h"
#include "core/Time.h"

#include <QImage>
#include <QList>
#include <QMap>
#include <QVariant>

// Offscreen OpenGL executor for file-based GPU packages (effects and transitions).
// Grace mode: on init/compile/draw failure, returns the first source image unchanged.
class GpuEffectExecutor
{
public:
    static GpuEffectExecutor &instance();

    // One effect in a chain. `gpu` must outlive the applyChain call (catalog entries do).
    // modelDef is set for "backend": "model3d" steps; gpu stays null for those.
    struct ChainStep
    {
        QString cacheKey;
        const drift::GpuEffectDefinition *gpu = nullptr;
        const EffectPresetEntry *modelDef = nullptr;
        QMap<QString, QVariant> parameters;
        QList<drift::FaceAnchors> faceSlots;
    };

    // Run a whole effect chain with a single upload and a single readback, keeping
    // intermediate results in GPU framebuffers. Applying effects one at a time
    // costs an upload plus a blocking glReadPixels per effect, which is what made
    // stacked effects collapse playback.
    QImage applyChain(const QList<ChainStep> &steps, const QImage &input, drift::TimeUs timeUs);

    // Run a GPU pipeline over N source images. sources[i] is bound wherever a pass declares
    // a source_texture with index i; sources[0] is also u_currentTexture / u_fromTexture and
    // sources[1] is u_toTexture. cacheKey namespaces the compiled-program cache — callers must
    // keep effect and transition ids from colliding (see kTransitionCacheKeyPrefix).
    // okOut is set false whenever the pipeline could not run (no GL, compile/draw failure), in
    // which case sources[0] is returned and callers may substitute their own fallback.
    QImage apply(const QString &cacheKey, const drift::GpuEffectDefinition &gpu,
                 const QList<QImage> &sources, const QMap<QString, QVariant> &parameters,
                 drift::TimeUs timeUs, double progress = 0.0, bool *okOut = nullptr);

    // Apply one GPU catalog effect. Returns input unchanged on any failure.
    QImage apply(const EffectPresetEntry &def, const QImage &input,
                 const QMap<QString, QVariant> &parameters, drift::TimeUs timeUs);

    // GPU blend for time_echo history (newest-first). Falls back to empty on GL failure
    // so callers can use a CPU path.
    QImage blendTimeEcho(const QList<QImage> &framesNewestFirst, double decay, int blendMode);

    bool isAvailable();

private:
    GpuEffectExecutor() = default;
    bool ensureContext();
    void releaseGl();

    bool m_triedInit = false;
    bool m_available = false;
};

// Transition ids share a namespace with effect ids in the program cache.
constexpr const char *kTransitionCacheKeyPrefix = "transition:";
