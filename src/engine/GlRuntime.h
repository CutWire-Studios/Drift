#pragma once

// Internal to the engine's GL code. Owns the process-wide offscreen OpenGL
// context, the compiled-program cache, the framebuffer pool and the static
// texture cache, and runs one GPU package's passes.
//
// Shared by GpuEffectExecutor (effects/transitions as QImage -> QImage) and
// GpuCompositor (whole-frame compositing that never leaves the GPU). Not a
// public API — do not include from outside src/engine.

#include "GpuEffectDefinition.h"
#include "ModelAsset.h"
#include "core/Time.h"

#include <QByteArray>
#include <QImage>
#include <QMap>
#include <QMutex>
#include <QOpenGLExtraFunctions>
#include <QOpenGLFramebufferObject>
#include <QOpenGLShaderProgram>
#include <QSize>
#include <QString>
#include <QVariant>
#include <QVector>

#include <QThread>

#include <cstdint>
#include <functional>
#include <list>
#include <map>
#include <memory>
#include <unordered_map>
#include <vector>

class QOffscreenSurface;
class QOpenGLContext;

namespace drift::gl {

// A framebuffer plus its size. Owns the FBO; hand it back to GlRuntime with
// releaseTarget() so it can be recycled rather than freed.
struct GlTarget
{
    std::unique_ptr<QOpenGLFramebufferObject> fbo;
    int width = 0;
    int height = 0;
    bool hasDepth = false;

    GlTarget() = default;
    GlTarget(GlTarget &&) noexcept = default;
    GlTarget &operator=(GlTarget &&) noexcept = default;
    GlTarget(const GlTarget &) = delete;
    GlTarget &operator=(const GlTarget &) = delete;

    bool isValid() const { return fbo && fbo->isValid(); }
    GLuint texture() const { return fbo ? fbo->texture() : 0; }
    QSize size() const { return QSize(width, height); }
};

// GPU upload of a ModelAsset. Owned by GlRuntime::models; torn down in shutdown().
struct GlModelGpu
{
    QString key; // "absolutePath:mtimeMs"
    GLuint vao = 0;
    GLuint vbo = 0;
    GLuint ibo = 0;
    QVector<GLuint> textures; // parallel to ModelAsset::images
    std::shared_ptr<const ModelAsset> cpu;
    size_t vramBytes = 0;
};

struct CompiledPass
{
    std::unique_ptr<QOpenGLShaderProgram> program;

    CompiledPass() = default;
    CompiledPass(CompiledPass &&) noexcept = default;
    CompiledPass &operator=(CompiledPass &&) noexcept = default;
    CompiledPass(const CompiledPass &) = delete;
    CompiledPass &operator=(const CompiledPass &) = delete;
};

struct CompiledEffect
{
    QString id;
    QString sourceSig;
    std::vector<CompiledPass> passes;
    bool ok = false;
};

class GlRuntime
{
public:
    std::unique_ptr<QOpenGLContext> context;
    std::unique_ptr<QOffscreenSurface> surface;
    GLuint vao = 0;
    GLuint vbo = 0;
    std::unique_ptr<QOpenGLShaderProgram> copyProgram;
    std::map<QString, CompiledEffect> programs;
    std::map<QString, GLuint> staticTextures; // absolute path -> GL texture

    // Face-prop GPU uploads. Bounded LRU; destroyed in shutdown() alongside staticTextures.
    struct ModelCache
    {
        std::list<GlModelGpu> lru;
        std::unordered_map<QString, std::list<GlModelGpu>::iterator> index;
        size_t totalBytes = 0;
        static constexpr size_t kMaxModels = 6;
        static constexpr size_t kMaxBytes = 192ull * 1024 * 1024;
    };
    ModelCache models;

    // Static UV sphere for head-proxy occlusion. Built once on first model3d draw.
    struct HeadProxy
    {
        GLuint vao = 0;
        GLuint vbo = 0;
        GLuint ibo = 0;
        int indexCount = 0;
    };
    HeadProxy headProxy;

    // A QOpenGLContext has thread affinity and can only be made current on the
    // thread it lives on, but GL work arrives from several threads (the
    // compositor worker, the export job, tools and tests). So the context lives
    // on one dedicated GL thread and every caller hands it a job to run there.
    // This also serializes GL, which is why there is no mutex around the context.
    //
    // Runs fn on the GL thread with the context current. Returns false if OpenGL
    // is unavailable, in which case fn does not run.
    bool exec(const std::function<void()> &fn);

    // True when a context could be created. Safe to call from any thread.
    bool available();

    QOpenGLExtraFunctions *functions();

    // Compiled programs are cached by key + source signature.
    CompiledEffect *compile(const QString &cacheKey, const drift::GpuEffectDefinition &gpu);

    // Framebuffers are recycled by size (and depth attachment): allocating a fresh FBO per
    // effect per frame churns GPU memory during steady-state playback. wantDepth is only set
    // for the model3d overlay — every other caller keeps the default.
    GlTarget acquireTarget(int width, int height, bool wantDepth = false);
    void releaseTarget(GlTarget &&target);

    // Presentation ring. The preview's composited frame is handed to the Qt Quick
    // scene graph as a live GL texture rather than read back, so the target it
    // lives in cannot go back to the general pool while the scene graph samples
    // it. Rotating through a few targets gives the scene graph time to finish
    // with one before it is drawn into again.
    // Waits on the slot's previous publish fence before returning it for redraw.
    GlTarget &acquirePresentTarget(int width, int height);

    // After composing into a present target: flush + fence. Call before publishing
    // the texture id so the scene graph never samples a half-drawn frame, without
    // the full-pipeline stall of glFinish.
    void markPresentReady(GlTarget &presentTarget);

    // Compile (once) and return an arbitrary fragment shader program, keyed by id.
    // Used for the compositor's own shaders, which are not package-defined.
    QOpenGLShaderProgram *builtinProgram(const QString &id, const char *vertexSource,
                                         const char *fragmentSource);

    // Tear down GL objects and stop the GL thread. Called at app exit.
    void shutdown();

private:
    bool ensureReady();
    bool initGlObjects();
    void waitPresentFence(int slotIndex);
    void destroyImageUploadCache();

    QMutex m_initMutex;
    bool m_initTried = false;
    bool m_ok = false;
    QThread *m_glThread = nullptr;
    QObject *m_glOwner = nullptr; // lives on m_glThread; the invoke target

    std::multimap<uint64_t, std::unique_ptr<QOpenGLFramebufferObject>> m_targetPool;
    size_t m_pooledTargets = 0;
    static constexpr size_t kMaxPooledTargets = 32;

    static constexpr int kPresentRingSize = 3;
    GlTarget m_presentRing[kPresentRingSize];
    GLsync m_presentFence[kPresentRingSize] = {};
    int m_presentNext = 0;

    // QImage::cacheKey() → uploaded texture. Stills/text/shapes and decoder cache
    // hits skip CPU→GPU upload; callers blit into a pooled FBO for exclusive use.
    struct CachedUpload
    {
        qint64 cacheKey = 0;
        GLuint texture = 0;
        int width = 0;
        int height = 0;
    };
    std::unordered_map<qint64, std::list<CachedUpload>::iterator> m_imageUploadIndex;
    std::list<CachedUpload> m_imageUploadLru;
    static constexpr size_t kMaxCachedUploads = 48;

    friend GLuint cachedUploadTexture(GlRuntime &rt, QOpenGLExtraFunctions *gl, const QImage &image);
};

GlRuntime &runtime();

// The fullscreen-quad vertex shader every package pass uses.
extern const char *const kQuadVertexShader;

GLuint uploadTexture(QOpenGLExtraFunctions *gl, const QImage &image, bool flipVertically = false);
bool blitTextureToTarget(GlRuntime &rt, QOpenGLExtraFunctions *gl, GLuint srcTex, GlTarget &dest);
GLuint staticTexture(GlRuntime &rt, QOpenGLExtraFunctions *gl, const QString &path);

// Upload a QImage into a pooled FBO, so sources, intermediate buffers and the
// canvas all share one texture orientation. A null image becomes transparent
// black at fallbackSize.
GlTarget promoteImageToTarget(GlRuntime &rt, QOpenGLExtraFunctions *gl, const QImage &image,
                              const QSize &fallbackSize);

// Like promoteImageToTarget, but reuses a cached GL texture when QImage::cacheKey
// matches a recent upload (decoder/still cache hits). Always returns a fresh
// pooled FBO the caller may mutate and release.
GlTarget promoteImageToTargetCached(GlRuntime &rt, QOpenGLExtraFunctions *gl, const QImage &image,
                                    const QSize &fallbackSize);

// NV12 (semi-planar Y + interleaved UV) → RGBA FBO via a convert shader. `nv12`
// is height*width Y bytes followed by height/2*width UV bytes.
GlTarget promoteNv12ToTarget(GlRuntime &rt, QOpenGLExtraFunctions *gl, const QByteArray &nv12,
                             int width, int height);

void setPackageUniforms(QOpenGLShaderProgram *program, const QMap<QString, QVariant> &parameters,
                        const QSize &resolution, drift::TimeUs timeUs, double progress);

// Run every pass of one GPU package, reading from `sources` and returning a new
// pooled target with the result. Nothing is read back to the CPU. Returns an
// invalid target on failure (grace mode).
GlTarget runPipeline(GlRuntime &rt, QOpenGLExtraFunctions *gl, const QString &cacheKey,
                     const drift::GpuEffectDefinition &gpu, const std::vector<const GlTarget *> &sources,
                     const QMap<QString, QVariant> &parameters, drift::TimeUs timeUs, double progress,
                     const QSize &canvasSize);

} // namespace drift::gl
