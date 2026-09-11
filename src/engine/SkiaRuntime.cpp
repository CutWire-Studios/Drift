#include "SkiaRuntime.h"

#include <QOpenGLContext>
#include <QtGui/qopenglcontext_platform.h>
#include <QOpenGLExtraFunctions>
#include <QOpenGLShaderProgram>

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

#ifndef GL_FRAMEBUFFER_SRGB
#define GL_FRAMEBUFFER_SRGB 0x8DB9
#endif

// Grayscale AA only: LCD subpixel coverage assumes an opaque background and produces colour
// fringes on the transparent layers everything here is drawn into.
SkSurfaceProps surfaceProps()
{
    return SkSurfaceProps(0, kUnknown_SkPixelGeometry);
}

} // namespace

struct SkiaRuntime::Impl
{
    sk_sp<GrDirectContext> ctx;
    Stats stats;
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
    return target;
}

void SkiaRuntime::releaseCaches()
{
    if (d->ctx)
        d->ctx->freeGpuResources();
}

void SkiaRuntime::shutdown()
{
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
