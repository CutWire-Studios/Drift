#pragma once

// Internal to the engine's GL code. Owns the process-wide offscreen OpenGL
// context, the compiled-program cache, the framebuffer pool and the static
// texture cache, and runs one GPU package's passes.
//
// Shared by GpuEffectExecutor (effects/transitions as QImage -> QImage) and
// GpuCompositor (whole-frame compositing that never leaves the GPU). Not a
// public API — do not include from outside src/engine.

#include "GpuEffectDefinition.h"
#include "GpuStatus.h"
#include "ModelAsset.h"
#include "PreviewVideoFrame.h"
#include "core/Time.h"

#include <QByteArray>
#include <QElapsedTimer>
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
struct SwsContext;

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
    // Presentation targets rotated between the compositor and the scene graph. Public so
    // GpuCompositor can static_assert the in-flight composite cap against it: every composite
    // that may be running at once needs a slot, plus one for the frame still on screen.
    static constexpr int kPresentRingSize = 3;

    std::unique_ptr<QOpenGLContext> context;
    std::unique_ptr<QOffscreenSurface> surface;
    GLuint vao = 0;
    GLuint vbo = 0;
    std::unique_ptr<QOpenGLShaderProgram> copyProgram;
    std::map<QString, CompiledEffect> programs;
    std::map<QString, GLuint> staticTextures; // absolute path -> GL texture

    // Face Swap source photos. Keyed on "<absolutePath>|<mtimeMs>|<size>", so editing a photo in
    // place rebuilds the textures instead of serving a stale one. Destroyed in shutdown()
    // alongside staticTextures.
    //
    // Kept apart from staticTextures because those are package assets: staticTexture() uploads
    // them flipped and wrapping, and a photo needs neither — its v axis has to line up with the
    // landmark uv the mesh carries.
    struct FaceSwapPhotoGpu
    {
        GLuint texture = 0; // the photo, unflipped, clamped
        GLuint lowFreq = 0; // face-relative low-frequency field for the lighting match
        float aspect = 1.f; // photo height / width, to turn width-normalized landmarks into uv
    };
    std::map<QString, FaceSwapPhotoGpu> faceSwapPhotos;

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

    // Warped MediaPipe face mesh. VBO is STREAM-updated every frame; IBO is STATIC and rebuilt
    // when the rest-pose .bin path changes. Destroyed with the other model GL objects.
    struct FaceMeshGpu
    {
        GLuint vao = 0;
        GLuint vbo = 0;
        GLuint ibo = 0;
        int indexCount = 0;
        int vertexCount = 0;
        QString path;
    };
    FaceMeshGpu faceMesh;

    // Face Swap's mesh. A separate slot rather than sharing faceMesh: the two carry different
    // topologies and different vertex layouts, so one clip using both effects would rebuild the
    // buffers twice per frame.
    FaceMeshGpu faceSwapMesh;

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

    // True when this context landed in the Qt Quick scene graph's share group, so a
    // texture *name* from here resolves in the scene graph's context.
    bool sharesWithGuiContext();

    QOpenGLExtraFunctions *functions();

    // Compiled programs are cached by key + source signature.
    CompiledEffect *compile(const QString &cacheKey, const drift::GpuEffectDefinition &gpu);

    // Framebuffers are recycled by size (and depth attachment): allocating a fresh FBO per
    // effect per frame churns GPU memory during steady-state playback. wantDepth is only set
    // for the model3d overlay — every other caller keeps the default.
    GlTarget acquireTarget(int width, int height, bool wantDepth = false);
    void releaseTarget(GlTarget &&target);

    // glFinish then read the FBO. Apple's GL can hand toImage() a half-resolved
    // tile without this, so two scrubs of the same timestamp would not match.
    QImage readTarget(const GlTarget &target);

    // Export NV12 ring. Convert the composited (premultiplied) canvas to BT.709
    // limited NV12 and pack into a PIXEL_PACK_BUFFER without waiting. `slot` is
    // 0 .. kExportNv12Slots-1. The GL context must be current (call from exec()).
    static constexpr int kExportNv12Slots = 2;
    bool packCanvasToNv12Slot(const GlTarget &canvas, int outW, int outH, int slot);
    // Wait for packCanvasToNv12Slot(slot), then copy Y and interleaved UV. Strides
    // are bytes per row. The GL context must be current.
    bool mapNv12Slot(int slot, uint8_t *y, int yStride, uint8_t *uv, int uvStride, int width,
                     int height);

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
    // `geom` may be nullptr. Cache key is `id` alone — do not reuse an id with different sources.
    QOpenGLShaderProgram *builtinProgram(const QString &id, const char *vertexSource,
                                         const char *fragmentSource, const char *geom);

    // Drop the recyclable GPU memory — the uploaded-image texture cache and the framebuffer pool —
    // without touching the context, the compiled programs or the live present ring. For the Android
    // application-state handler; a no-op when GL was never brought up. Call from the GUI thread
    // (or any non-GL thread) — exec() blocks on the GL thread.
    void releaseCaches();

    // Tear down GL objects and stop the GL thread. Called at app exit.
    void shutdown();

    // Last preview import path and VAAPI zero-copy rejection, for the debug report.
    enum class PreviewUploadPath { None, CudaInterop, VaapiDmaBuf, CpuRoundTrip };
    static PreviewUploadPath lastPreviewUploadPath();
    static QString lastVaapiImportReason();

    // Outcome of the last bring-up attempt. Safe from any thread, and never starts
    // one itself — call available() first if you want an attempt made rather than a
    // snapshot of what already happened.
    static drift::gl::GlStatusInfo lastStatus();

private:
    bool ensureReady();
    bool initGlObjects();
    void waitPresentFence(int slotIndex);
    void destroyImageUploadCache();
    void destroyVideoUploadState();
    void destroyExportNv12State();
    void destroyExportNv12Slot(int slot);
    bool ensureExportNv12Slot(QOpenGLExtraFunctions *gl, int slot, int width, int height);
    bool ensureVideoUploadTextures(QOpenGLExtraFunctions *gl, int width, int height);
    bool uploadPlanePbo(QOpenGLExtraFunctions *gl, GLuint texture, int texW, int texH, GLenum internalFormat,
                        GLenum format, const uint8_t *src, int srcPitch, int packedWidth);
    void unregisterCudaResources();
    bool importCudaNv12(QOpenGLExtraFunctions *gl, const AVFrame *frame);
    // Texture names for importers whose storage comes from the imported surface rather than
    // from glTexImage2D. Kept apart from m_videoY/m_videoUV: a later PBO frame reusing those
    // would glTexSubImage2D straight into the decoder's dma-buf.
    bool ensureImportTextureNames(QOpenGLExtraFunctions *gl);
    bool importVaapiNv12(QOpenGLExtraFunctions *gl, const AVFrame *frame);
    AVFrame *ensureSoftwareNv12(const AVFrame *src);

    QMutex m_initMutex;
    // Only a driver's own verdict is final. A missing share context just means Qt
    // Quick has not built one yet, so that attempt is retried rather than latched.
    bool m_failedPermanently = false;
    QElapsedTimer m_lastAttempt;
    bool m_ok = false;
    bool m_sharesWithGui = false;
    QThread *m_glThread = nullptr;
    QObject *m_glOwner = nullptr; // lives on m_glThread; the invoke target

    std::multimap<uint64_t, std::unique_ptr<QOpenGLFramebufferObject>> m_targetPool;
    size_t m_pooledTargets = 0;
    static constexpr size_t kMaxPooledTargets = 32;

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

    GLuint m_videoY = 0;
    GLuint m_videoUV = 0;
    int m_videoTexW = 0;
    int m_videoTexH = 0;
    GLuint m_videoPbo[2] = {0, 0};
    int m_videoPboIndex = 0;
    AVFrame *m_hwImportStaging = nullptr;
    AVFrame *m_importNv12 = nullptr;
    ::SwsContext *m_importSws = nullptr;
    void *m_cudaYResource = nullptr;
    void *m_cudaUvResource = nullptr;
    int m_cudaTexW = 0;
    int m_cudaTexH = 0;
    bool m_cudaImportFailed = false;
    GLuint m_importY = 0;
    GLuint m_importUV = 0;
    bool m_vaapiImportFailed = false;
    // Auto-mode driver verdict: -1 unknown, 0 unverified, 1 verified. Cached because the
    // answer depends only on the driver, which does not change within a session.
    int m_vaapiAutoVerified = -1;

    struct ExportNv12Slot
    {
        GLuint yTex = 0;
        GLuint uvTex = 0;
        GLuint yFbo = 0;
        GLuint uvFbo = 0;
        GLuint pbo = 0;
        GLsync fence = 0;
        int width = 0;
        int height = 0;
    };
    ExportNv12Slot m_exportNv12[kExportNv12Slots];

    friend GLuint cachedUploadTexture(GlRuntime &rt, QOpenGLExtraFunctions *gl, const QImage &image);
    friend GlTarget promoteVideoFrameToTarget(GlRuntime &rt, QOpenGLExtraFunctions *gl,
                                              const PreviewVideoFrame &frame);
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

// Preview video → RGBA FBO. Hardware CUDA and VAAPI frames stay on the GPU — CUDA copies
// into the pooled textures on the decoder's own stream, VAAPI imports its dma-buf directly;
// everything else uploads NV12 through a pooled PBO. Colour and display rotation are applied
// in the convert shader.
GlTarget promoteVideoFrameToTarget(GlRuntime &rt, QOpenGLExtraFunctions *gl,
                                   const PreviewVideoFrame &frame);

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
