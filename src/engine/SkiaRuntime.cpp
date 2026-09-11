#include "SkiaRuntime.h"

#include <QOpenGLContext>
#include <QtGui/qopenglcontext_platform.h>
#include <QOpenGLExtraFunctions>
#include <QOpenGLShaderProgram>

#include <list>
#include <unordered_map>

#include "include/core/SkCanvas.h"
#include "include/core/SkColorSpace.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkSurface.h"
#include "include/core/SkSurfaceProps.h"
#include "include/gpu/GpuTypes.h"
#include "include/gpu/ganesh/GrBackendSurface.h"
#include "include/gpu/ganesh/GrContextOptions.h"
#include "include/gpu/ganesh/GrDirectContext.h"
#include "include/gpu/ganesh/SkSurfaceGanesh.h"
#include "include/gpu/ganesh/gl/GrGLAssembleInterface.h"
#include "include/gpu/ganesh/gl/GrGLBackendSurface.h"
#include "include/gpu/ganesh/gl/GrGLDirectContext.h"
#include "include/gpu/ganesh/gl/GrGLInterface.h"
#include "include/gpu/ganesh/gl/GrGLTypes.h"

namespace drift::skia {

namespace {

// Skia hands us premultiplied pixels; layer targets carry straight alpha because
// kLayerFragShader premultiplies when it draws them onto the canvas.
constexpr const char *kUnpremultiplyFragShader = R"(#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;
uniform sampler2D u_currentTexture;
void main() {
    vec4 c = texture(u_currentTexture, v_texCoord);
    fragColor = vec4(c.a > 0.0 ? c.rgb / c.a : vec3(0.0), c.a);
}
)";

// Qt's GLES2 headers stop short of these ES 3.0 constants; the functions exist on every
// context Drift creates.
#ifndef GL_FRAMEBUFFER_SRGB
#define GL_FRAMEBUFFER_SRGB 0x8DB9
#endif
#ifndef GL_UNPACK_ROW_LENGTH
#define GL_UNPACK_ROW_LENGTH 0x0CF2
#endif
#ifndef GL_PACK_ROW_LENGTH
#define GL_PACK_ROW_LENGTH 0x0D02
#endif

// Grayscale AA only: LCD subpixel coverage assumes an opaque background and produces colour
// fringes on the transparent layers everything here is drawn into.
SkSurfaceProps surfaceProps()
{
    return SkSurfaceProps(0, kUnknown_SkPixelGeometry);
}

} // namespace

// Painted layers kept on the GPU by cacheKey(). Bounded by count and by bytes because a handful
// of 4K text layers would otherwise pin more VRAM than the whole framebuffer pool. Entries hold
// pooled targets and hand them back to GlRuntime on eviction.
struct VectorCache
{
    struct Entry
    {
        quint64 key = 0;
        gl::GlTarget target;
    };
    std::list<Entry> lru; // front = most recent
    std::unordered_map<quint64, std::list<Entry>::iterator> index;
    size_t bytes = 0;
#ifdef Q_OS_ANDROID
    static constexpr size_t kMaxEntries = 12;
    static constexpr size_t kMaxBytes = 24ull * 1024 * 1024;
#else
    static constexpr size_t kMaxEntries = 32;
    static constexpr size_t kMaxBytes = 96ull * 1024 * 1024;
#endif

    static size_t sizeOf(const gl::GlTarget &t) { return size_t(t.width) * size_t(t.height) * 4; }

    gl::GlTarget *find(quint64 key)
    {
        auto it = index.find(key);
        if (it == index.end())
            return nullptr;
        lru.splice(lru.begin(), lru, it->second);
        return &lru.front().target;
    }

    void insert(gl::GlRuntime &rt, quint64 key, gl::GlTarget &&target)
    {
        bytes += sizeOf(target);
        lru.push_front({key, std::move(target)});
        index[key] = lru.begin();
        while (lru.size() > kMaxEntries || bytes > kMaxBytes) {
            Entry &victim = lru.back();
            bytes -= sizeOf(victim.target);
            index.erase(victim.key);
            rt.releaseTarget(std::move(victim.target));
            lru.pop_back();
        }
    }

    void clear(gl::GlRuntime *rt)
    {
        for (Entry &e : lru) {
            if (rt)
                rt->releaseTarget(std::move(e.target));
        }
        lru.clear();
        index.clear();
        bytes = 0;
    }
};

struct SkiaRuntime::Impl
{
    sk_sp<GrDirectContext> ctx;
    Stats stats;
    VectorCache cache;
    gl::GlRuntime *rt = nullptr;
};

SkiaRuntime::SkiaRuntime(std::unique_ptr<Impl> impl) : d(std::move(impl)) {}

SkiaRuntime::~SkiaRuntime() = default;

SkiaRuntime *SkiaRuntime::acquire(gl::GlRuntime &rt)
{
    if (rt.skia)
        return rt.skia.get();

    // Resolve entry points through Qt rather than linking libGL: the same code then serves
    // desktop GL and GLES, and Qt already knows which library the context came from.
    //
    // Skia also asks for eglGetCurrentDisplay/eglQueryString to probe EGL extensions. On GLX,
    // libglvnd's glXGetProcAddress hands back a dispatch stub for *any* unknown name, so those
    // would resolve to functions that return garbage — only answer egl* on a real EGL context.
    sk_sp<const GrGLInterface> iface = GrGLMakeAssembledInterface(
        nullptr, [](void *, const char name[]) -> GrGLFuncPtr {
            QOpenGLContext *ctx = QOpenGLContext::currentContext();
            if (!ctx)
                return nullptr;
            if (qstrncmp(name, "egl", 3) == 0) {
#if QT_CONFIG(egl)
                if (!ctx->nativeInterface<QNativeInterface::QEGLContext>())
                    return nullptr;
#else
                return nullptr;
#endif
            }
            return reinterpret_cast<GrGLFuncPtr>(ctx->getProcAddress(name));
        });
    if (!iface || !iface->validate()) {
        qWarning("SkiaRuntime: GL interface did not validate; Skia drawing unavailable");
        return nullptr;
    }

    GrContextOptions options;
    options.fSuppressPrints = true;
    sk_sp<GrDirectContext> ctx = GrDirectContexts::MakeGL(std::move(iface), options);
    if (!ctx) {
        qWarning("SkiaRuntime: GrDirectContext creation failed; Skia drawing unavailable");
        return nullptr;
    }
#ifdef Q_OS_ANDROID
    ctx->setResourceCacheLimit(48ull * 1024 * 1024);
#else
    ctx->setResourceCacheLimit(128ull * 1024 * 1024);
#endif

    auto impl = std::make_unique<Impl>();
    impl->ctx = std::move(ctx);
    impl->rt = &rt;
    rt.skia = std::shared_ptr<SkiaRuntime>(new SkiaRuntime(std::move(impl)));
    return rt.skia.get();
}

// Put back the fixed-function state Skia changes and GlRuntime's own passes never re-set. Every
// pass binds its program, textures, VAO, viewport and blend mode per draw, so this list is the
// complete delta — a leaked scissor or stencil test here would clip every later pass.
static void restoreGlState(QOpenGLExtraFunctions *gl)
{
    gl->glDisable(GL_SCISSOR_TEST);
    gl->glDisable(GL_STENCIL_TEST);
    gl->glDisable(GL_DEPTH_TEST);
    gl->glDepthMask(GL_TRUE);
    gl->glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    gl->glDisable(GL_BLEND);
    gl->glBlendEquation(GL_FUNC_ADD);
    gl->glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    gl->glBindVertexArray(0);
    gl->glActiveTexture(GL_TEXTURE0);
    gl->glBindTexture(GL_TEXTURE_2D, 0);
    gl->glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    gl->glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    gl->glPixelStorei(GL_PACK_ALIGNMENT, 4);
    gl->glPixelStorei(GL_PACK_ROW_LENGTH, 0);
    gl->glBindFramebuffer(GL_FRAMEBUFFER, 0);
    gl->glUseProgram(0);
    if (QOpenGLContext *ctx = QOpenGLContext::currentContext(); ctx && !ctx->isOpenGLES())
        gl->glDisable(GL_FRAMEBUFFER_SRGB);
}

gl::GlTarget SkiaRuntime::paintToTarget(gl::GlRuntime &rt, QOpenGLExtraFunctions *gl,
                                        const VectorPainter &painter)
{
    const QSize size = painter.size();
    if (size.isEmpty() || !d->ctx)
        return {};

    const quint64 key = painter.cacheKey();
    if (key != 0) {
        if (gl::GlTarget *cached = d->cache.find(key)) {
            ++d->stats.cacheHits;
            gl::GlTarget copy = rt.acquireTarget(cached->width, cached->height);
            if (copy.isValid() && gl::blitTextureToTarget(rt, gl, cached->texture(), copy))
                return copy;
            rt.releaseTarget(std::move(copy));
            return {};
        }
        ++d->stats.cacheMisses;
    }

    // Qt and our own passes changed GL state behind Skia's back since the last draw.
    d->ctx->resetContext();

    const SkSurfaceProps props = surfaceProps();
    sk_sp<SkSurface> surface = SkSurfaces::RenderTarget(
        d->ctx.get(), skgpu::Budgeted::kYes,
        SkImageInfo::Make(size.width(), size.height(), kRGBA_8888_SkColorType,
                          kPremul_SkAlphaType),
        0, kTopLeft_GrSurfaceOrigin, &props);
    if (!surface) {
        restoreGlState(gl);
        return {};
    }

    SkCanvas *canvas = surface->getCanvas();
    canvas->clear(SK_ColorTRANSPARENT);
    painter.paint(*canvas);
    skgpu::ganesh::FlushAndSubmit(surface.get());
    ++d->stats.paints;

    GrGLTextureInfo info;
    const GrBackendTexture backend =
        SkSurfaces::GetBackendTexture(surface.get(), SkSurface::BackendHandleAccess::kFlushRead);
    const bool haveTexture = GrBackendTextures::GetGLTextureInfo(backend, &info) && info.fID;

    restoreGlState(gl);
    if (!haveTexture)
        return {};

    QOpenGLShaderProgram *program = rt.builtinProgram(QStringLiteral("skia_unpremultiply"),
                                                      gl::kQuadVertexShader,
                                                      kUnpremultiplyFragShader);
    if (!program)
        return {};

    gl::GlTarget target = rt.acquireTarget(size.width(), size.height());
    if (!target.isValid())
        return {};

    target.fbo->bind();
    gl->glViewport(0, 0, target.width, target.height);
    gl->glDisable(GL_BLEND);
    gl->glClearColor(0.f, 0.f, 0.f, 0.f);
    gl->glClear(GL_COLOR_BUFFER_BIT);
    program->bind();
    program->setUniformValue("u_currentTexture", 0);
    gl->glActiveTexture(GL_TEXTURE0);
    gl->glBindTexture(GL_TEXTURE_2D, info.fID);
    gl->glBindVertexArray(rt.vao);
    gl->glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    gl->glBindVertexArray(0);
    gl->glBindTexture(GL_TEXTURE_2D, 0);
    program->release();
    target.fbo->release();

    if (key == 0)
        return target;

    // The cache keeps this target; the caller gets a copy it is free to mutate and release.
    gl::GlTarget copy = rt.acquireTarget(target.width, target.height);
    const bool copied = copy.isValid() && gl::blitTextureToTarget(rt, gl, target.texture(), copy);
    d->cache.insert(rt, key, std::move(target));
    if (!copied) {
        rt.releaseTarget(std::move(copy));
        return {};
    }
    return copy;
}

void SkiaRuntime::releaseCaches()
{
    d->cache.clear(d->rt);
    if (d->ctx)
        d->ctx->freeGpuResources();
}

void SkiaRuntime::shutdown()
{
    // Cached targets go back to the pool GlRuntime is about to clear, while the context is
    // still current; then Skia's own GL objects.
    d->cache.clear(d->rt);
    if (!d->ctx)
        return;
    d->ctx->flushAndSubmit(GrSyncCpu::kYes);
    d->ctx->releaseResourcesAndAbandonContext();
    d->ctx.reset();
}

QImage SkiaRuntime::rasterize(const VectorPainter &painter)
{
    const QSize size = painter.size();
    if (size.isEmpty())
        return {};

    // Qt's ARGB32 is one 0xAARRGGBB word per pixel, which in memory on little-endian hosts is
    // exactly Skia's kBGRA_8888 — so Skia draws straight into the QImage's bytes.
    QImage image(size, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    const SkSurfaceProps props = surfaceProps();
    sk_sp<SkSurface> surface = SkSurfaces::WrapPixels(
        SkImageInfo::Make(size.width(), size.height(), kBGRA_8888_SkColorType,
                          kPremul_SkAlphaType),
        image.bits(), image.bytesPerLine(), &props);
    if (!surface)
        return {};
    painter.paint(*surface->getCanvas());
    return image;
}

SkiaRuntime::Stats SkiaRuntime::stats() const
{
    return d->stats;
}

} // namespace drift::skia
