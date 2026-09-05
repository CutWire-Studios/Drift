#include "GlRuntime.h"

#include "GlModelRenderer.h"
#include "VaapiZeroCopy.h"

#include <QColor>
#include <QCoreApplication>
#include <QMatrix3x3>
#include <QMutex>
#include <QMutexLocker>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QSettings>
#include <QSurfaceFormat>
#include <QVector2D>
#include <QVector3D>

#include <cmath>
#include <cstring>
#include <mutex>

extern "C" {
#include <libavutil/hwcontext.h>
#include <libavutil/pixdesc.h>
#include <libswscale/swscale.h>
}

#if defined(Q_OS_WIN)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#elif !defined(Q_OS_MACOS)
#include <dlfcn.h>
#endif

// VAAPI dma-buf import. Android is GLES on a MediaCodec decoder that never produces a VAAPI
// surface, so it is excluded along with the two platforms that have no libva at all.
#if !defined(Q_OS_WIN) && !defined(Q_OS_MACOS) && !defined(Q_OS_ANDROID)
#define DRIFT_VAAPI_IMPORT 1
#include <unistd.h>
#endif

// Qt for Android is built against the GLES 2.0 headers so it can still run on ES2-only devices, and
// qopengl.h includes <GLES2/gl2.h> accordingly. The ES 3.0 *functions* this file uses still resolve,
// because QOpenGLExtraFunctions looks them up at runtime — but the ES 3.0 *enum constants* are
// simply absent from the ES2 header, so they are supplied here.
#ifndef GL_RED
#define GL_RED 0x1903
#endif
#ifndef GL_RG
#define GL_RG 0x8227
#endif
#ifndef GL_R8
#define GL_R8 0x8229
#endif
#ifndef GL_RG8
#define GL_RG8 0x822B
#endif
#ifndef GL_SYNC_GPU_COMMANDS_COMPLETE
#define GL_SYNC_GPU_COMMANDS_COMPLETE 0x9117
#endif
#ifndef GL_SYNC_FLUSH_COMMANDS_BIT
#define GL_SYNC_FLUSH_COMMANDS_BIT 0x00000001
#endif
#ifndef GL_UNPACK_ROW_LENGTH
#define GL_UNPACK_ROW_LENGTH 0x0CF2
#endif
#ifndef GL_PIXEL_UNPACK_BUFFER
#define GL_PIXEL_UNPACK_BUFFER 0x88EC
#endif
#ifndef GL_STREAM_DRAW
#define GL_STREAM_DRAW 0x88E0
#endif
#ifndef GL_MAP_WRITE_BIT
#define GL_MAP_WRITE_BIT 0x0002
#endif
#ifndef GL_MAP_INVALIDATE_BUFFER_BIT
#define GL_MAP_INVALIDATE_BUFFER_BIT 0x0008
#endif
#ifndef GL_PIXEL_PACK_BUFFER
#define GL_PIXEL_PACK_BUFFER 0x88EB
#endif
#ifndef GL_STREAM_READ
#define GL_STREAM_READ 0x88E1
#endif
#ifndef GL_MAP_READ_BIT
#define GL_MAP_READ_BIT 0x0001
#endif
#ifndef GL_PACK_ROW_LENGTH
#define GL_PACK_ROW_LENGTH 0x0D02
#endif
#ifndef GL_PACK_ALIGNMENT
#define GL_PACK_ALIGNMENT 0x0D05
#endif
#ifndef GL_READ_FRAMEBUFFER
#define GL_READ_FRAMEBUFFER 0x8CA8
#endif
#ifndef GL_FRAMEBUFFER
#define GL_FRAMEBUFFER 0x8D40
#endif
#ifndef GL_COLOR_ATTACHMENT0
#define GL_COLOR_ATTACHMENT0 0x8CE0
#endif
#ifndef GL_FRAMEBUFFER_COMPLETE
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#endif
#ifndef GL_TIMEOUT_IGNORED
#define GL_TIMEOUT_IGNORED 0xFFFFFFFFFFFFFFFFull
#endif
#ifndef GL_ALREADY_SIGNALED
#define GL_ALREADY_SIGNALED 0x911A
#endif
#ifndef GL_CONDITION_SATISFIED
#define GL_CONDITION_SATISFIED 0x911C
#endif
#ifndef GL_WAIT_FAILED
#define GL_WAIT_FAILED 0x911D
#endif

namespace drift {

VaapiZeroCopyMode vaapiZeroCopyMode()
{
    // Env is read live so tests can qputenv after other imports have already
    // resolved the settings half. QSettings is what must not run per frame.
    if (qEnvironmentVariableIsSet("DRIFT_VAAPI_ZEROCOPY")) {
        return qgetenv("DRIFT_VAAPI_ZEROCOPY") != "0" ? VaapiZeroCopyMode::On
                                                      : VaapiZeroCopyMode::Off;
    }
    static VaapiZeroCopyMode mode = VaapiZeroCopyMode::Auto;
    static std::once_flag once;
    std::call_once(once, [] {
        const QSettings settings;
        const QString key = QStringLiteral("preview/vaapiZeroCopy");
        // Absent is Auto, not false: the setting having never been touched is what tells the
        // import it may decide for itself, and a stored false must still mean off.
        if (settings.contains(key)) {
            mode = settings.value(key).toBool() ? VaapiZeroCopyMode::On : VaapiZeroCopyMode::Off;
        }
    });
    return mode;
}

void applyVaapiZeroCopyXcbEgl()
{
#if !defined(Q_OS_WIN) && !defined(Q_OS_MACOS) && !defined(Q_OS_ANDROID)
    // Only on an explicit request. Auto must not rewrite QT_XCB_GL_INTEGRATION for every X11
    // user on first launch — that changes how the whole window is rendered, far past the
    // preview, and there is no way to fall back from it once the app is up.
    if (vaapiZeroCopyMode() != VaapiZeroCopyMode::On)
        return;
    if (!qEnvironmentVariableIsEmpty("QT_XCB_GL_INTEGRATION"))
        return;
    qputenv("QT_XCB_GL_INTEGRATION", "xcb_egl");
#endif
}

} // namespace drift

namespace drift::gl {

namespace {

// Every shader in the project is written as `#version 330 core`. Android has no desktop GL, so the
// version line is swapped for `#version 300 es` and the default precision qualifiers ES requires
// are prepended. Done at the compile sites so package .frag files stay shared with desktop.
QByteArray translateShaderSource(QByteArray body, bool fragment)
{
    if (body.startsWith("#version")) {
        const int newline = body.indexOf('\n');
        body = (newline < 0) ? QByteArray() : body.mid(newline + 1);
    }

    const QOpenGLContext *current = QOpenGLContext::currentContext();
    if (!current || !current->isOpenGLES())
        return QByteArray("#version 330 core\n") + body;

    QByteArray preamble("#version 300 es\n");
    preamble += "precision highp float;\n";
    preamble += "precision highp int;\n";
    if (fragment)
        preamble += "precision highp sampler2D;\n";
    return preamble + body;
}

QByteArray translateShader(const char *source, bool fragment)
{
    return translateShaderSource(QByteArray(source), fragment);
}

QByteArray translateShader(const QString &source, bool fragment)
{
    return translateShaderSource(source.toUtf8(), fragment);
}

constexpr const char *kCopyFragShader = R"(#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;
uniform sampler2D u_currentTexture;
void main() {
    fragColor = texture(u_currentTexture, v_texCoord);
}
)";

// YUV → RGBA with caller-supplied matrix, range and a UV affine for display rotation.
constexpr const char *kNv12FragShader = R"(#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;
uniform sampler2D u_y;
uniform sampler2D u_uv;
uniform mat3 u_yuvToRgb;
uniform vec3 u_yuvOffset;
uniform vec3 u_yuvScale;
uniform mat3 u_texMap;
void main() {
    vec2 src = (u_texMap * vec3(v_texCoord, 1.0)).xy;
    float y = texture(u_y, src).r;
    vec2 chroma = texture(u_uv, src).rg;
    vec3 yuv = (vec3(y, chroma) - u_yuvOffset) * u_yuvScale;
    vec3 rgb = u_yuvToRgb * yuv;
    fragColor = vec4(clamp(rgb, 0.0, 1.0), 1.0);
}
)";

// Premultiplied canvas RGBA → BT.709 limited luma. Matches libswscale
// SWS_CS_ITU709 with full-range RGB source and limited-range YUV dest.
constexpr const char *kRgbaToYFragShader = R"(#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;
uniform sampler2D u_src;
void main() {
    vec4 c = texture(u_src, v_texCoord);
    vec3 rgb = (c.a > 0.0001) ? clamp(c.rgb / c.a, 0.0, 1.0) : vec3(0.0);
    float yFull = dot(rgb, vec3(0.2126, 0.7152, 0.0722));
    float y = yFull * (219.0 / 255.0) + (16.0 / 255.0);
    fragColor = vec4(y, 0.0, 0.0, 1.0);
}
)";

// Half-res chroma: bilinear sample at the UV texel centre (centre of each 2×2).
constexpr const char *kRgbaToUvFragShader = R"(#version 330 core
in vec2 v_texCoord;
out vec4 fragColor;
uniform sampler2D u_src;
void main() {
    vec4 c = texture(u_src, v_texCoord);
    vec3 rgb = (c.a > 0.0001) ? clamp(c.rgb / c.a, 0.0, 1.0) : vec3(0.0);
    float yFull = dot(rgb, vec3(0.2126, 0.7152, 0.0722));
    float cb = (rgb.b - yFull) / (2.0 * (1.0 - 0.0722));
    float cr = (rgb.r - yFull) / (2.0 * (1.0 - 0.2126));
    fragColor = vec4(cb * (224.0 / 255.0) + (128.0 / 255.0),
                     cr * (224.0 / 255.0) + (128.0 / 255.0), 0.0, 1.0);
}
)";

// Fullscreen triangle strip with standard GL UVs (v=0 at bottom / NDC bottom).
// Source QImages are copied into an FBO once so every pass samples FBO-backed
// textures only (same Y layout). Readback uses toImage(false).
constexpr float kQuad[] = {
    // pos      // uv
    -1.f, -1.f, 0.f, 0.f,
     1.f, -1.f, 1.f, 0.f,
    -1.f,  1.f, 0.f, 1.f,
     1.f,  1.f, 1.f, 1.f,
};

QMatrix3x3 yuvToRgbMatrix(int colorspace)
{
    // Row-major, multiplies vec3(Y, Cb, Cr) after range expansion.
    float m[9] = {1.f, 0.f, 1.5748f, 1.f, -0.1873f, -0.4681f, 1.f, 1.8556f, 0.f};
    switch (colorspace) {
    case AVCOL_SPC_BT470BG:
    case AVCOL_SPC_SMPTE170M:
        m[2] = 1.402f;
        m[4] = -0.344f;
        m[5] = -0.714f;
        m[7] = 1.772f;
        break;
    case AVCOL_SPC_BT2020_NCL:
    case AVCOL_SPC_BT2020_CL:
        m[2] = 1.4746f;
        m[4] = -0.1646f;
        m[5] = -0.5714f;
        m[7] = 1.8814f;
        break;
    default:
        break;
    }
    return QMatrix3x3(m);
}

QMatrix3x3 texMapForRotation(int rotation)
{
    // codedUV = (mat * vec3(displayUV, 1)).xy. 90/270 are clockwise, matching Qt.
    float m[9] = {1.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 1.f};
    if (rotation == 90) {
        const float r[9] = {0.f, 1.f, 0.f, -1.f, 0.f, 1.f, 0.f, 0.f, 1.f};
        memcpy(m, r, sizeof(m));
    } else if (rotation == 180) {
        const float r[9] = {-1.f, 0.f, 1.f, 0.f, -1.f, 1.f, 0.f, 0.f, 1.f};
        memcpy(m, r, sizeof(m));
    } else if (rotation == 270) {
        const float r[9] = {0.f, -1.f, 1.f, 1.f, 0.f, 0.f, 0.f, 0.f, 1.f};
        memcpy(m, r, sizeof(m));
    }
    return QMatrix3x3(m);
}

void yuvRangeUniforms(int colorRange, QVector3D *offset, QVector3D *scale)
{
    if (colorRange == AVCOL_RANGE_JPEG) {
        *offset = QVector3D(0.f, 128.f / 255.f, 128.f / 255.f);
        *scale = QVector3D(1.f, 1.f, 1.f);
        return;
    }
    *offset = QVector3D(16.f / 255.f, 128.f / 255.f, 128.f / 255.f);
    *scale = QVector3D(255.f / 219.f, 255.f / 224.f, 255.f / 224.f);
}

bool isHwPixelFormat(AVPixelFormat fmt)
{
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(fmt);
    return desc && (desc->flags & AV_PIX_FMT_FLAG_HWACCEL);
}

#if !defined(Q_OS_MACOS)
using CUresult = int;
using CUdeviceptr = void *;
using CUarray = void *;
using CUcontext = void *;
using CUstream = void *;
using CUgraphicsResource = void *;

enum { kCuSuccess = 0, kCuMemoryDevice = 2, kCuMemoryArray = 3, kCuRegisterWriteDiscard = 0x02 };

// CUDA_MEMCPY2D as the *_v2 entry points expect it. The unversioned cuMemcpy2D symbol that
// libcuda still exports is the v1 ABI, whose equivalent fields are unsigned int rather than
// size_t — a completely different layout on 64-bit. cuda.h hides this behind
// `#define cuMemcpy2D cuMemcpy2D_v2`, so source that includes it gets v2 automatically and
// only a dlsym of the bare name lands on v1. Load the _v2 names below to match this struct.
struct CudaMemcpy2D
{
    size_t srcXInBytes = 0;
    size_t srcY = 0;
    int srcMemoryType = 0;
    int srcPad = 0;
    const void *srcHost = nullptr;
    CUdeviceptr srcDevice = nullptr;
    CUarray srcArray = nullptr;
    size_t srcPitch = 0;
    size_t dstXInBytes = 0;
    size_t dstY = 0;
    int dstMemoryType = 0;
    int dstPad = 0;
    void *dstHost = nullptr;
    CUdeviceptr dstDevice = nullptr;
    CUarray dstArray = nullptr;
    size_t dstPitch = 0;
    size_t WidthInBytes = 0;
    size_t Height = 0;
};

struct CudaGlApi
{
    void *lib = nullptr;
    CUresult (*cuInit)(unsigned int) = nullptr;
    CUresult (*cuCtxPushCurrent)(CUcontext) = nullptr;
    CUresult (*cuCtxPopCurrent)(CUcontext *) = nullptr;
    CUresult (*cuGraphicsGLRegisterImage)(CUgraphicsResource *, unsigned int, unsigned int,
                                          unsigned int) = nullptr;
    CUresult (*cuGraphicsUnregisterResource)(CUgraphicsResource) = nullptr;
    CUresult (*cuGraphicsMapResources)(unsigned int, CUgraphicsResource *, CUstream) = nullptr;
    CUresult (*cuGraphicsUnmapResources)(unsigned int, CUgraphicsResource *, CUstream) = nullptr;
    CUresult (*cuGraphicsSubResourceGetMappedArray)(CUarray *, CUgraphicsResource, unsigned int,
                                                    unsigned int) = nullptr;
    CUresult (*cuMemcpy2DAsync)(const CudaMemcpy2D *, CUstream) = nullptr;
    CUresult (*cuStreamSynchronize)(CUstream) = nullptr;
    bool ok = false;
};

CudaGlApi &cudaGlApi()
{
    static CudaGlApi api;
    static std::once_flag once;
    std::call_once(once, [] {
#if defined(Q_OS_WIN)
        api.lib = static_cast<void *>(LoadLibraryW(L"nvcuda.dll"));
        auto sym = [&](const char *name) -> void * {
            return api.lib ? static_cast<void *>(GetProcAddress(static_cast<HMODULE>(api.lib), name))
                           : nullptr;
        };
#else
        api.lib = dlopen("libcuda.so.1", RTLD_LAZY | RTLD_LOCAL);
        auto sym = [&](const char *name) -> void * { return api.lib ? dlsym(api.lib, name) : nullptr; };
#endif
        if (!api.lib)
            return;
#define DRIFT_CUDA_SYM(field, name) \
    api.field = reinterpret_cast<decltype(api.field)>(sym(name)); \
    if (!api.field) \
        return;
        // The _v2 suffixes are not optional: libcuda exports both ABIs, and the bare names
        // are the deprecated v1 ones. Anything that takes or returns a struct or a device
        // pointer must use the versioned symbol or it will read the arguments at the wrong
        // offsets. If a driver is old enough to lack them, ok stays false and the preview
        // falls back to the hardware-transfer path.
        DRIFT_CUDA_SYM(cuInit, "cuInit");
        DRIFT_CUDA_SYM(cuCtxPushCurrent, "cuCtxPushCurrent_v2");
        DRIFT_CUDA_SYM(cuCtxPopCurrent, "cuCtxPopCurrent_v2");
        DRIFT_CUDA_SYM(cuGraphicsGLRegisterImage, "cuGraphicsGLRegisterImage");
        DRIFT_CUDA_SYM(cuGraphicsUnregisterResource, "cuGraphicsUnregisterResource");
        DRIFT_CUDA_SYM(cuGraphicsMapResources, "cuGraphicsMapResources");
        DRIFT_CUDA_SYM(cuGraphicsUnmapResources, "cuGraphicsUnmapResources");
        DRIFT_CUDA_SYM(cuGraphicsSubResourceGetMappedArray, "cuGraphicsSubResourceGetMappedArray");
        DRIFT_CUDA_SYM(cuMemcpy2DAsync, "cuMemcpy2DAsync_v2");
        DRIFT_CUDA_SYM(cuStreamSynchronize, "cuStreamSynchronize");
#undef DRIFT_CUDA_SYM
        if (api.cuInit(0) != kCuSuccess)
            return;
        api.ok = true;
    });
    return api;
}

bool cudaContextOf(const AVFrame *frame, CUcontext *ctx, CUstream *stream)
{
    if (!frame || !frame->hw_frames_ctx)
        return false;
    const auto *fc = reinterpret_cast<const AVHWFramesContext *>(frame->hw_frames_ctx->data);
    if (!fc || !fc->device_ctx || fc->device_ctx->type != AV_HWDEVICE_TYPE_CUDA || !fc->device_ctx->hwctx)
        return false;
    const char *hwctx = static_cast<const char *>(fc->device_ctx->hwctx);
    *ctx = *reinterpret_cast<CUcontext const *>(hwctx);
    *stream = *reinterpret_cast<CUstream const *>(hwctx + sizeof(void *));
    return *ctx != nullptr;
}

bool cudaSwFormatIsNv12(const AVFrame *frame)
{
    if (!frame || !frame->hw_frames_ctx)
        return false;
    const auto *fc = reinterpret_cast<const AVHWFramesContext *>(frame->hw_frames_ctx->data);
    return fc && fc->sw_format == AV_PIX_FMT_NV12;
}

bool queueCudaPlaneCopy(CudaGlApi &api, CUstream stream, CUarray dst, CUdeviceptr src,
                        size_t srcPitch, size_t widthBytes, size_t height)
{
    CudaMemcpy2D op{};
    op.srcMemoryType = kCuMemoryDevice;
    op.srcDevice = src;
    op.srcPitch = srcPitch;
    op.dstMemoryType = kCuMemoryArray;
    op.dstArray = dst;
    op.WidthInBytes = widthBytes;
    op.Height = height;
    // On the frame's own stream, not the null stream. FFmpeg creates its decoder stream with
    // CU_STREAM_NON_BLOCKING, which by definition does not synchronise against the legacy
    // null stream — so a copy issued there was unordered with respect to the map and unmap
    // around it, and GL could sample the WRITE_DISCARD textures before the pixels arrived.
    return api.cuMemcpy2DAsync(&op, stream) == kCuSuccess;
}

// Both NV12 planes in one map/unmap pair and one stream wait, rather than a pair each: the
// two copies are independent, and the only thing that has to be true before GL samples is
// that both have landed.
bool copyCudaNv12ToTextures(CudaGlApi &api, CUstream stream, CUgraphicsResource yRes,
                            CUgraphicsResource uvRes, const AVFrame *frame, int width, int height)
{
    CUgraphicsResource resources[2] = {yRes, uvRes};
    if (api.cuGraphicsMapResources(2, resources, stream) != kCuSuccess)
        return false;

    CUarray yArray = nullptr;
    CUarray uvArray = nullptr;
    bool ok = api.cuGraphicsSubResourceGetMappedArray(&yArray, yRes, 0, 0) == kCuSuccess && yArray
        && api.cuGraphicsSubResourceGetMappedArray(&uvArray, uvRes, 0, 0) == kCuSuccess && uvArray;

    if (ok) {
        // The interleaved UV plane is full-width in bytes over half the rows: width/2 texels
        // of two bytes each.
        ok = queueCudaPlaneCopy(api, stream, yArray, frame->data[0],
                                size_t(qMax(0, frame->linesize[0])), size_t(width), size_t(height))
            && queueCudaPlaneCopy(api, stream, uvArray, frame->data[1],
                                  size_t(qMax(0, frame->linesize[1])), size_t(width),
                                  size_t(height / 2));
    }

    api.cuGraphicsUnmapResources(2, resources, stream);

    // Unmap only orders the copies on the stream; it does not wait for them. This does, and
    // it is the sync point that makes the textures safe for the convert shader. One wait on
    // one stream per frame, against a full hardware-transfer download plus PBO upload if this
    // path is not taken.
    if (ok && api.cuStreamSynchronize(stream) != kCuSuccess)
        ok = false;
    return ok;
}
#endif

QMutex g_previewImportMutex;
GlRuntime::PreviewUploadPath g_previewUploadPath = GlRuntime::PreviewUploadPath::None;
QString g_vaapiImportReason;

// Its own mutex, not m_initMutex: initGlObjects() records the outcome while the
// caller of ensureReady() still holds m_initMutex, and the debug report reads the
// status from the GUI thread while that attempt is in flight.
QMutex g_statusMutex;
drift::gl::GlStatusInfo g_glStatus;

void setGlStatus(const drift::gl::GlStatusInfo &info)
{
    QMutexLocker lock(&g_statusMutex);
    g_glStatus = info;
}

void setGlStatus(drift::gl::GlStatus status)
{
    drift::gl::GlStatusInfo info;
    info.status = status;
    setGlStatus(info);
}

// The vendor/renderer/version the context actually landed on. Needs the context
// current; `gl` may be null when the failure happened before functions resolved.
drift::gl::GlStatusInfo describeContext(const QOpenGLContext *context, QOpenGLExtraFunctions *gl,
                                        drift::gl::GlStatus status)
{
    drift::gl::GlStatusInfo info;
    info.status = status;
    if (context) {
        info.isEs = context->isOpenGLES();
        info.major = context->format().majorVersion();
        info.minor = context->format().minorVersion();
    }
    if (gl) {
        if (const char *vendor = reinterpret_cast<const char *>(gl->glGetString(GL_VENDOR)))
            info.vendor = QString::fromUtf8(vendor);
        if (const char *renderer = reinterpret_cast<const char *>(gl->glGetString(GL_RENDERER)))
            info.renderer = QString::fromUtf8(renderer);
    }
    info.software = drift::gl::isSoftwareRenderer(info.renderer);
    return info;
}

void recordPreviewUploadPath(GlRuntime::PreviewUploadPath path)
{
    QMutexLocker lock(&g_previewImportMutex);
    g_previewUploadPath = path;
}

void logVaapiImportOnce(const QString &reason)
{
    {
        QMutexLocker lock(&g_previewImportMutex);
        if (g_vaapiImportReason.isEmpty())
            g_vaapiImportReason = reason;
    }
    static std::once_flag once;
    std::call_once(once, [reason] {
        qWarning("GlRuntime: VAAPI zero-copy import unavailable (%s)", qUtf8Printable(reason));
    });
}

#if defined(DRIFT_VAAPI_IMPORT)
// libva and the EGL dma-buf import extension, resolved at runtime for the same reason CUDA is:
// the binary has to start on a host with neither. Only the handful of declarations the import
// needs are reproduced here — a compile-time dependency on va/va.h and EGL/eglext.h would buy
// nothing but a build-time failure on machines that never decode with VAAPI.

using VASurfaceID = unsigned int;
using VAStatus = int;

enum {
    kVaStatusSuccess = 0,
    kVaMemTypeDrmPrime2 = 0x40000000,
    kVaExportReadOnly = 0x0001,
    kVaExportSeparateLayers = 0x0004,
};

// Mirrors VADRMPRIMESurfaceDescriptor from va/va_drmcommon.h. The objects[] entry pads to 16
// bytes (int + uint32_t, then an 8-aligned uint64_t); getting that wrong silently shifts every
// field after it, which is what the sanity check in importVaapiNv12 exists to catch.
struct VaDrmPrimeSurfaceDescriptor
{
    uint32_t fourcc;
    uint32_t width;
    uint32_t height;
    uint32_t num_objects;
    struct
    {
        int fd;
        uint32_t size;
        uint64_t drm_format_modifier;
    } objects[4];
    uint32_t num_layers;
    struct
    {
        uint32_t drm_format;
        uint32_t num_planes;
        uint32_t object_index[4];
        uint32_t offset[4];
        uint32_t pitch[4];
    } layers[4];
};

constexpr uint32_t fourcc(char a, char b, char c, char d)
{
    return uint32_t(uint8_t(a)) | (uint32_t(uint8_t(b)) << 8) | (uint32_t(uint8_t(c)) << 16)
        | (uint32_t(uint8_t(d)) << 24);
}

constexpr uint32_t kDrmFormatR8 = fourcc('R', '8', ' ', ' ');
constexpr uint32_t kDrmFormatGr88 = fourcc('G', 'R', '8', '8');
constexpr uint32_t kDrmFormatNv12 = fourcc('N', 'V', '1', '2');
constexpr uint64_t kDrmFormatModInvalid = 0x00ffffffffffffffull;
constexpr uint64_t kDrmFormatModLinear = 0;

using EGLDisplayHandle = void *;
using EGLImageHandle = void *;
using EGLClientBufferHandle = void *;

enum {
    kEglExtensions = 0x3055,
    kEglWidth = 0x3057,
    kEglHeight = 0x3056,
    kEglNone = 0x3038,
    kEglLinuxDmaBufExt = 0x3270,
    kEglLinuxDrmFourccExt = 0x3271,
    kEglDmaBufPlane0FdExt = 0x3272,
    kEglDmaBufPlane0OffsetExt = 0x3273,
    kEglDmaBufPlane0PitchExt = 0x3274,
    kEglDmaBufPlane0ModifierLoExt = 0x3443,
    kEglDmaBufPlane0ModifierHiExt = 0x3444,
};

struct VaEglApi
{
    void *va = nullptr;
    void *egl = nullptr;
    VAStatus (*vaExportSurfaceHandle)(void *, VASurfaceID, uint32_t, uint32_t, void *) = nullptr;
    VAStatus (*vaSyncSurface)(void *, VASurfaceID) = nullptr;
    // Optional: only used to recognise a driver Auto mode trusts, so a libva without it
    // degrades to "not verified" rather than to no zero-copy path at all.
    const char *(*vaQueryVendorString)(void *) = nullptr;
    EGLDisplayHandle (*eglGetCurrentDisplay)() = nullptr;
    const char *(*eglQueryString)(EGLDisplayHandle, int) = nullptr;
    // The KHR entry point takes 32-bit EGLint attributes, not the EGLAttrib (intptr_t) list
    // EGL 1.5's eglCreateImage takes. That is why the modifier arrives as two halves.
    EGLImageHandle (*eglCreateImageKHR)(EGLDisplayHandle, void *, unsigned int, EGLClientBufferHandle,
                                        const int32_t *) = nullptr;
    unsigned int (*eglDestroyImageKHR)(EGLDisplayHandle, EGLImageHandle) = nullptr;
    void (*glEGLImageTargetTexture2DOES)(unsigned int, EGLImageHandle) = nullptr;
    bool ok = false;
};

VaEglApi &vaEglApi()
{
    static VaEglApi api;
    static std::once_flag once;
    std::call_once(once, [] {
        api.va = dlopen("libva.so.2", RTLD_LAZY | RTLD_LOCAL);
        api.egl = dlopen("libEGL.so.1", RTLD_LAZY | RTLD_LOCAL);
        if (!api.va || !api.egl)
            return;
#define DRIFT_VA_SYM(handle, field, name) \
    api.field = reinterpret_cast<decltype(api.field)>(dlsym(handle, name)); \
    if (!api.field) \
        return;
        api.vaQueryVendorString = reinterpret_cast<decltype(api.vaQueryVendorString)>(
            dlsym(api.va, "vaQueryVendorString"));
        DRIFT_VA_SYM(api.va, vaExportSurfaceHandle, "vaExportSurfaceHandle");
        DRIFT_VA_SYM(api.va, vaSyncSurface, "vaSyncSurface");
        DRIFT_VA_SYM(api.egl, eglGetCurrentDisplay, "eglGetCurrentDisplay");
        DRIFT_VA_SYM(api.egl, eglQueryString, "eglQueryString");
#undef DRIFT_VA_SYM

        // eglCreateImageKHR is an extension entry point, so it comes from eglGetProcAddress
        // rather than the library's symbol table. glEGLImageTargetTexture2DOES comes from Qt's
        // context, which knows which GL implementation is actually current.
        auto *getProc = reinterpret_cast<void *(*)(const char *)>(dlsym(api.egl, "eglGetProcAddress"));
        QOpenGLContext *ctx = QOpenGLContext::currentContext();
        if (!getProc || !ctx)
            return;
        api.eglCreateImageKHR =
            reinterpret_cast<decltype(api.eglCreateImageKHR)>(getProc("eglCreateImageKHR"));
        api.eglDestroyImageKHR =
            reinterpret_cast<decltype(api.eglDestroyImageKHR)>(getProc("eglDestroyImageKHR"));
        api.glEGLImageTargetTexture2DOES =
            reinterpret_cast<decltype(api.glEGLImageTargetTexture2DOES)>(
                ctx->getProcAddress("glEGLImageTargetTexture2DOES"));
        if (!api.eglCreateImageKHR || !api.eglDestroyImageKHR || !api.glEGLImageTargetTexture2DOES)
            return;

        // Under libglvnd every one of those pointers is a dispatch stub that exists whether or
        // not the driver implements the entry point, so the extension strings are the only real
        // availability test.
        if (!ctx->hasExtension(QByteArrayLiteral("GL_OES_EGL_image")))
            return;
        api.ok = true;
    });
    return api;
}

// Reads AVVAAPIDeviceContext::display, whose VADisplay is the first and only member.
void *vaapiDisplayOf(const AVFrame *frame)
{
    if (!frame || !frame->hw_frames_ctx)
        return nullptr;
    const auto *fc = reinterpret_cast<const AVHWFramesContext *>(frame->hw_frames_ctx->data);
    if (!fc || !fc->device_ctx || fc->device_ctx->type != AV_HWDEVICE_TYPE_VAAPI
        || !fc->device_ctx->hwctx)
        return nullptr;
    return *reinterpret_cast<void *const *>(fc->device_ctx->hwctx);
}

bool vaapiSwFormatIsNv12(const AVFrame *frame)
{
    if (!frame || !frame->hw_frames_ctx)
        return false;
    const auto *fc = reinterpret_cast<const AVHWFramesContext *>(frame->hw_frames_ctx->data);
    return fc && fc->sw_format == AV_PIX_FMT_NV12;
}

// vaExportSurfaceHandle hands over owned fds. They have to be closed on every path out,
// including the ones that reject the descriptor before an EGLImage is ever created — at 60 fps
// a leak there exhausts the process fd table in under a minute, and it surfaces as unrelated
// file-open failures elsewhere in the app.
struct ExportedSurfaceFds
{
    VaDrmPrimeSurfaceDescriptor *desc = nullptr;

    ~ExportedSurfaceFds()
    {
        if (!desc)
            return;
        const uint32_t count = qMin(desc->num_objects, 4u);
        for (uint32_t i = 0; i < count; ++i) {
            if (desc->objects[i].fd >= 0)
                ::close(desc->objects[i].fd);
        }
    }
};

QString fourccString(uint32_t value)
{
    char s[5] = {char(value), char(value >> 8), char(value >> 16), char(value >> 24), 0};
    for (char &c : s) {
        if (c != 0 && (c < 32 || c > 126))
            c = '?';
    }
    return QString::fromLatin1(s);
}

QString describePrime(const VaDrmPrimeSurfaceDescriptor &desc)
{
    QString text = QStringLiteral("fourcc=%1 objects=%2 layers=%3")
                       .arg(fourccString(desc.fourcc))
                       .arg(desc.num_objects)
                       .arg(desc.num_layers);
    const uint32_t n = qMin(desc.num_layers, 4u);
    for (uint32_t i = 0; i < n; ++i) {
        text += QStringLiteral(" layer%1={format=%2 planes=%3}")
                    .arg(i)
                    .arg(fourccString(desc.layers[i].drm_format))
                    .arg(desc.layers[i].num_planes);
    }
    return text;
}
#endif // DRIFT_VAAPI_IMPORT

uint64_t targetPoolKey(int width, int height, bool wantDepth)
{
    // w/h fit in 31 bits; the low bit tags depth so colour-only and depth targets never share.
    return (uint64_t(uint32_t(width) & 0x7fffffffu) << 33)
           | (uint64_t(uint32_t(height) & 0x7fffffffu) << 1)
           | (wantDepth ? 1u : 0u);
}

GlRuntime *g_runtime = nullptr;

void destroyGpuRuntime()
{
    if (!g_runtime)
        return;
    g_runtime->shutdown();
    delete g_runtime;
    g_runtime = nullptr;
}

} // namespace

const char *const kQuadVertexShader = R"(#version 330 core
layout(location = 0) in vec2 a_position;
layout(location = 1) in vec2 a_texCoord;
out vec2 v_texCoord;
void main() {
    v_texCoord = a_texCoord;
    gl_Position = vec4(a_position, 0.0, 1.0);
}
)";

GlRuntime &runtime()
{
    // Reached from the compositor thread, the export job and the GUI thread, so
    // the lazy construction has to be race-free — two runtimes would mean two GL
    // contexts and two post-routines.
    static std::once_flag once;
    std::call_once(once, [] {
        g_runtime = new GlRuntime;
        qAddPostRoutine(destroyGpuRuntime);
    });
    return *g_runtime;
}

bool GlRuntime::initGlObjects()
{
    QOpenGLContext *shared = QOpenGLContext::globalShareContext();
    const bool sharingRequired = QCoreApplication::testAttribute(Qt::AA_ShareOpenGLContexts);

    // In the application, use the share context's realized format: EGL may
    // adjust the requested format during creation. Command-line tools and
    // tests have no Qt Quick share context, so retain an independent context.
    if (shared && !shared->isValid())
        shared = nullptr;

    if (sharingRequired && !shared) {
        qWarning("GlRuntime: required global OpenGL share context is unavailable");
        setGlStatus(drift::gl::GlStatus::NoShareContext);
        return false;
    }

    QSurfaceFormat format;
    if (shared) {
        format = shared->format();
    } else if (QOpenGLContext::openGLModuleType() == QOpenGLContext::LibGLES) {
        // Asking for a 3.3 core profile on Android makes QOpenGLContext::create() fail outright.
        format.setRenderableType(QSurfaceFormat::OpenGLES);
        format.setVersion(3, 0);
        format.setProfile(QSurfaceFormat::NoProfile);
        format.setDepthBufferSize(0);
        format.setStencilBufferSize(0);
    } else {
        format.setRenderableType(QSurfaceFormat::OpenGL);
        format.setVersion(3, 3);
        format.setProfile(QSurfaceFormat::CoreProfile);
        format.setDepthBufferSize(0);
        format.setStencilBufferSize(0);
    }

    // Not every platform hands out a real offscreen surface. QOffscreenSurface
    // then falls back to a hidden QWindow, which cannot be created off the GUI
    // thread — Qt warns "Attempting to create QWindow-based QOffscreenSurface
    // outside the gui thread" and this fails. QtWayland takes that path when the
    // EGL client-buffer integration is missing, which would explain a preview that
    // is black on Wayland and fine on X11. Building it on the GUI thread instead
    // is not as simple as it looks: the GUI thread also calls ensureReady(), and
    // blocking on it from here while it waits on us deadlocks. So for now this
    // reports SurfaceFailed and the debug report says so, which is what confirms
    // the theory on an affected machine.
    surface = std::make_unique<QOffscreenSurface>();
    surface->setFormat(format);
    surface->create();
    if (!surface->isValid()) {
        qWarning("GlRuntime: failed to create offscreen surface");
        surface.reset();
        setGlStatus(drift::gl::GlStatus::SurfaceFailed);
        return false;
    }

    context = std::make_unique<QOpenGLContext>();
    if (shared)
        context->setShareContext(shared);
    context->setFormat(format);
    if (!context->create()) {
        qWarning("GlRuntime: failed to create OpenGL context");
        setGlStatus(describeContext(context.get(), nullptr, drift::gl::GlStatus::ContextFailed));
        context.reset();
        surface.reset();
        return false;
    }

    // create() succeeds even when the driver refuses to share, dropping the request
    // on the floor, so whether sharing actually happened has to be read back.
    m_sharesWithGui = shared && QOpenGLContext::areSharing(context.get(), shared);

    if (!context->makeCurrent(surface.get())) {
        qWarning("GlRuntime: makeCurrent failed on the GL thread");
        setGlStatus(
            describeContext(context.get(), nullptr, drift::gl::GlStatus::MakeCurrentFailed));
        context.reset();
        surface.reset();
        return false;
    }

    auto *gl = context->extraFunctions();
    if (!gl) {
        qWarning("GlRuntime: OpenGL extra functions unavailable");
        setGlStatus(describeContext(context.get(), nullptr, drift::gl::GlStatus::NoFunctions));
        context->doneCurrent();
        context.reset();
        surface.reset();
        return false;
    }

    // setVersion() above is a request; the driver decides. Everything after this line
    // assumes ES 3.0 / GL 3.3.
    const bool isEs = context->isOpenGLES();
    const int major = context->format().majorVersion();
    const int minor = context->format().minorVersion();
    if (isEs ? major < 3 : (major < 3 || (major == 3 && minor < 3))) {
        qCritical("GlRuntime: this device reports OpenGL%s %d.%d; Drift needs OpenGL ES 3.0 or "
                  "OpenGL 3.3. GPU rendering is unavailable. (vendor: %s, renderer: %s)",
                  isEs ? " ES" : "", major, minor,
                  reinterpret_cast<const char *>(gl->glGetString(GL_VENDOR)),
                  reinterpret_cast<const char *>(gl->glGetString(GL_RENDERER)));
        setGlStatus(describeContext(context.get(), gl, drift::gl::GlStatus::VersionTooLow));
        context->doneCurrent();
        context.reset();
        surface.reset();
        return false;
    }

    gl->glGenVertexArrays(1, &vao);
    gl->glBindVertexArray(vao);
    gl->glGenBuffers(1, &vbo);
    gl->glBindBuffer(GL_ARRAY_BUFFER, vbo);
    gl->glBufferData(GL_ARRAY_BUFFER, sizeof(kQuad), kQuad, GL_STATIC_DRAW);
    gl->glEnableVertexAttribArray(0);
    gl->glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), reinterpret_cast<void *>(0));
    gl->glEnableVertexAttribArray(1);
    gl->glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                              reinterpret_cast<void *>(2 * sizeof(float)));
    gl->glBindVertexArray(0);

    copyProgram = std::make_unique<QOpenGLShaderProgram>();
    if (!copyProgram->addShaderFromSourceCode(QOpenGLShader::Vertex,
                                              translateShader(kQuadVertexShader, false))
        || !copyProgram->addShaderFromSourceCode(QOpenGLShader::Fragment,
                                                 translateShader(kCopyFragShader, true))
        || !copyProgram->link()) {
        qWarning("GlRuntime: copy shader failed: %s", qPrintable(copyProgram->log()));
        drift::gl::GlStatusInfo info =
            describeContext(context.get(), gl, drift::gl::GlStatus::ShaderLinkFailed);
        info.detail = copyProgram->log();
        setGlStatus(info);
        copyProgram.reset();
        context->doneCurrent();
        context.reset();
        surface.reset();
        return false;
    }

    setGlStatus(describeContext(context.get(), gl, drift::gl::GlStatus::Ready));
    context->doneCurrent();
    return true;
}

bool GlRuntime::ensureReady()
{
    QMutexLocker lock(&m_initMutex);
    if (m_ok)
        return true;
    if (m_failedPermanently)
        return false;

    if (!QCoreApplication::instance()) {
        qWarning("GlRuntime: no QCoreApplication; OpenGL unavailable");
        setGlStatus(drift::gl::GlStatus::NoApplication);
        return false;
    }

    // Qt Quick builds the global share context when the first QQuickWindow
    // initialises, so a composite requested during startup can arrive before one
    // exists. That is not a verdict on the machine, and it used to end the
    // session's preview: check it here, before spending a thread and a context on
    // an attempt that cannot succeed yet, and leave nothing latched.
    if (QCoreApplication::testAttribute(Qt::AA_ShareOpenGLContexts)) {
        const QOpenGLContext *shared = QOpenGLContext::globalShareContext();
        if (!shared || !shared->isValid()) {
            setGlStatus(drift::gl::GlStatus::NoShareContext);
            return false;
        }
    }

    // Bring-up is expensive and callers ask once per composite request. If it
    // fails for a reason worth retrying, do not retry it at frame rate.
    constexpr int kRetryIntervalMs = 250;
    if (m_lastAttempt.isValid() && m_lastAttempt.elapsed() < kRetryIntervalMs)
        return false;
    m_lastAttempt.start();

    m_glThread = new QThread;
    m_glThread->setObjectName(QStringLiteral("DriftGL"));
    m_glThread->start();

    m_glOwner = new QObject;
    m_glOwner->moveToThread(m_glThread);

    bool initialized = false;
    QMetaObject::invokeMethod(
        m_glOwner, [this] { return initGlObjects(); }, Qt::BlockingQueuedConnection, &initialized);

    if (!initialized) {
        // Stop the thread before deleting the object that lives on it — deleting a
        // QObject from a thread other than its own is undefined behaviour.
        m_glThread->quit();
        m_glThread->wait();
        delete m_glOwner;
        m_glOwner = nullptr;
        delete m_glThread;
        m_glThread = nullptr;
        m_failedPermanently = !drift::gl::isTransient(GlRuntime::lastStatus().status);
        return false;
    }

    m_ok = true;
    return m_ok;
}

drift::gl::GlStatusInfo GlRuntime::lastStatus()
{
    QMutexLocker lock(&g_statusMutex);
    return g_glStatus;
}

bool GlRuntime::available()
{
    return ensureReady();
}

bool GlRuntime::sharesWithGuiContext()
{
    return ensureReady() && m_sharesWithGui;
}

bool GlRuntime::exec(const std::function<void()> &fn)
{
    if (!ensureReady())
        return false;

    bool ran = false;
    QMetaObject::invokeMethod(
        m_glOwner,
        [this, &fn]() -> bool {
            if (!context->makeCurrent(surface.get())) {
                qWarning("GlRuntime: makeCurrent failed");
                return false;
            }
            fn();
            context->doneCurrent();
            return true;
        },
        Qt::BlockingQueuedConnection, &ran);
    return ran;
}

QOpenGLExtraFunctions *GlRuntime::functions()
{
    return context ? context->extraFunctions() : nullptr;
}

void GlRuntime::releaseCaches()
{
    {
        QMutexLocker lock(&m_initMutex);
        if (!m_ok)
            return;
    }

    exec([this] {
        destroyImageUploadCache();
        destroyVideoUploadState();
        destroyExportNv12State();
        m_targetPool.clear();
        m_pooledTargets = 0;
        if (auto *gl = functions())
            destroyGlModels(*this, gl);
    });
}

void GlRuntime::shutdown()
{
    {
        QMutexLocker lock(&m_initMutex);
        if (!m_ok || !m_glOwner)
            return;
    }

    QMetaObject::invokeMethod(
        m_glOwner,
        [this] {
            if (!context->makeCurrent(surface.get()))
                return;
            if (auto *gl = context->extraFunctions()) {
                for (int i = 0; i < kPresentRingSize; ++i) {
                    if (m_presentFence[i]) {
                        gl->glDeleteSync(m_presentFence[i]);
                        m_presentFence[i] = nullptr;
                    }
                }
                destroyImageUploadCache();
                destroyVideoUploadState();
                destroyExportNv12State();
            }
            for (GlTarget &target : m_presentRing)
                target.fbo.reset();
            m_targetPool.clear();
            m_pooledTargets = 0;
            programs.clear();
            copyProgram.reset();
            if (auto *gl = context->extraFunctions()) {
                destroyGlModels(*this, gl);
                for (const auto &entry : staticTextures) {
                    GLuint tex = entry.second;
                    gl->glDeleteTextures(1, &tex);
                }
                staticTextures.clear();
                for (const auto &entry : faceSwapPhotos) {
                    if (entry.second.texture)
                        gl->glDeleteTextures(1, &entry.second.texture);
                    if (entry.second.lowFreq)
                        gl->glDeleteTextures(1, &entry.second.lowFreq);
                }
                faceSwapPhotos.clear();
                if (vbo) {
                    gl->glDeleteBuffers(1, &vbo);
                    vbo = 0;
                }
                if (vao) {
                    gl->glDeleteVertexArrays(1, &vao);
                    vao = 0;
                }
            }
            context->doneCurrent();
        },
        Qt::BlockingQueuedConnection);

    // Stop the thread before deleting the object that lives on it — deleting a
    // QObject from a thread other than its own is undefined behaviour.
    m_glThread->quit();
    m_glThread->wait();
    delete m_glOwner;
    m_glOwner = nullptr;
    delete m_glThread;
    m_glThread = nullptr;

    context.reset();
    surface.reset();
    m_ok = false;
    setGlStatus(drift::gl::GlStatus::NotAttempted);
}


GlTarget GlRuntime::acquireTarget(int width, int height, bool wantDepth)
{
    GlTarget target;
    target.width = qMax(1, width);
    target.height = qMax(1, height);
    target.hasDepth = wantDepth;

    const uint64_t key = targetPoolKey(target.width, target.height, wantDepth);
    const auto it = m_targetPool.find(key);
    if (it != m_targetPool.end()) {
        target.fbo = std::move(it->second);
        m_targetPool.erase(it);
        --m_pooledTargets;
        return target;
    }

    QOpenGLFramebufferObjectFormat fmt;
    fmt.setAttachment(wantDepth ? QOpenGLFramebufferObject::Depth
                                : QOpenGLFramebufferObject::NoAttachment);
    target.fbo = std::make_unique<QOpenGLFramebufferObject>(target.width, target.height, fmt);
    return target;
}

void GlRuntime::releaseTarget(GlTarget &&target)
{
    if (!target.isValid())
        return;
    if (m_pooledTargets >= kMaxPooledTargets)
        return; // let it drop

    const uint64_t key = targetPoolKey(target.width, target.height, target.hasDepth);
    m_targetPool.emplace(key, std::move(target.fbo));
    ++m_pooledTargets;
}

QImage GlRuntime::readTarget(const GlTarget &target)
{
    if (!target.isValid())
        return {};
    if (auto *gl = functions())
        gl->glFinish();
    return target.fbo->toImage(false).convertToFormat(QImage::Format_RGBA8888);
}

void GlRuntime::destroyExportNv12State()
{
    for (int i = 0; i < kExportNv12Slots; ++i)
        destroyExportNv12Slot(i);
}

void GlRuntime::destroyExportNv12Slot(int slot)
{
    if (slot < 0 || slot >= kExportNv12Slots)
        return;
    auto *gl = functions();
    ExportNv12Slot &s = m_exportNv12[slot];
    if (gl) {
        if (s.fence) {
            gl->glDeleteSync(s.fence);
            s.fence = nullptr;
        }
        if (s.pbo)
            gl->glDeleteBuffers(1, &s.pbo);
        if (s.yFbo)
            gl->glDeleteFramebuffers(1, &s.yFbo);
        if (s.uvFbo)
            gl->glDeleteFramebuffers(1, &s.uvFbo);
        if (s.yTex)
            gl->glDeleteTextures(1, &s.yTex);
        if (s.uvTex)
            gl->glDeleteTextures(1, &s.uvTex);
    }
    s = ExportNv12Slot{};
}

bool GlRuntime::ensureExportNv12Slot(QOpenGLExtraFunctions *gl, int slot, int width, int height)
{
    if (!gl || slot < 0 || slot >= kExportNv12Slots || width < 2 || height < 2 || (width % 2)
        || (height % 2))
        return false;

    ExportNv12Slot &s = m_exportNv12[slot];
    const GLsizeiptr pboBytes = GLsizeiptr(width) * height + GLsizeiptr(width) * (height / 2);
    if (s.yTex && s.uvTex && s.yFbo && s.uvFbo && s.pbo && s.width == width && s.height == height)
        return true;

    auto deleteTex = [gl](GLuint &t) {
        if (t) {
            gl->glDeleteTextures(1, &t);
            t = 0;
        }
    };
    auto deleteFbo = [gl](GLuint &f) {
        if (f) {
            gl->glDeleteFramebuffers(1, &f);
            f = 0;
        }
    };
    if (s.fence) {
        gl->glDeleteSync(s.fence);
        s.fence = nullptr;
    }
    if (s.pbo) {
        gl->glDeleteBuffers(1, &s.pbo);
        s.pbo = 0;
    }
    deleteFbo(s.yFbo);
    deleteFbo(s.uvFbo);
    deleteTex(s.yTex);
    deleteTex(s.uvTex);
    s.width = 0;
    s.height = 0;

    auto makePlane = [gl](GLuint *tex, GLuint *fbo, GLenum internal, GLenum format, int w, int h) {
        gl->glGenTextures(1, tex);
        gl->glBindTexture(GL_TEXTURE_2D, *tex);
        gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        gl->glTexImage2D(GL_TEXTURE_2D, 0, internal, w, h, 0, format, GL_UNSIGNED_BYTE, nullptr);

        gl->glGenFramebuffers(1, fbo);
        gl->glBindFramebuffer(GL_FRAMEBUFFER, *fbo);
        gl->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, *tex, 0);
        const GLenum status = gl->glCheckFramebufferStatus(GL_FRAMEBUFFER);
        gl->glBindFramebuffer(GL_FRAMEBUFFER, 0);
        gl->glBindTexture(GL_TEXTURE_2D, 0);
        return status == GL_FRAMEBUFFER_COMPLETE && *tex && *fbo;
    };

    if (!makePlane(&s.yTex, &s.yFbo, GL_R8, GL_RED, width, height)
        || !makePlane(&s.uvTex, &s.uvFbo, GL_RG8, GL_RG, width / 2, height / 2)) {
        destroyExportNv12Slot(slot);
        return false;
    }

    gl->glGenBuffers(1, &s.pbo);
    gl->glBindBuffer(GL_PIXEL_PACK_BUFFER, s.pbo);
    gl->glBufferData(GL_PIXEL_PACK_BUFFER, pboBytes, nullptr, GL_STREAM_READ);
    gl->glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    if (!s.pbo)
        return false;

    s.width = width;
    s.height = height;
    return true;
}

bool GlRuntime::packCanvasToNv12Slot(const GlTarget &canvas, int outW, int outH, int slot)
{
    auto *gl = functions();
    if (!gl || !canvas.isValid() || !ensureExportNv12Slot(gl, slot, outW, outH))
        return false;

    ExportNv12Slot &s = m_exportNv12[slot];
    if (s.fence) {
        gl->glClientWaitSync(s.fence, GL_SYNC_FLUSH_COMMANDS_BIT, GLuint64(1'000'000'000));
        gl->glDeleteSync(s.fence);
        s.fence = nullptr;
    }

    auto drawPlane = [&](QOpenGLShaderProgram *program, GLuint fbo, int w, int h) {
        if (!program)
            return false;
        gl->glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        gl->glViewport(0, 0, w, h);
        gl->glDisable(GL_BLEND);
        program->bind();
        program->setUniformValue("u_src", 0);
        gl->glActiveTexture(GL_TEXTURE0);
        gl->glBindTexture(GL_TEXTURE_2D, canvas.texture());
        gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        gl->glBindVertexArray(vao);
        gl->glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        gl->glBindVertexArray(0);
        program->release();
        gl->glBindFramebuffer(GL_FRAMEBUFFER, 0);
        return true;
    };

    QOpenGLShaderProgram *yProg =
        builtinProgram(QStringLiteral("__rgba_to_y__"), kQuadVertexShader, kRgbaToYFragShader);
    QOpenGLShaderProgram *uvProg =
        builtinProgram(QStringLiteral("__rgba_to_uv__"), kQuadVertexShader, kRgbaToUvFragShader);
    if (!drawPlane(yProg, s.yFbo, outW, outH) || !drawPlane(uvProg, s.uvFbo, outW / 2, outH / 2))
        return false;

    gl->glBindBuffer(GL_PIXEL_PACK_BUFFER, s.pbo);
    gl->glPixelStorei(GL_PACK_ALIGNMENT, 1);
    gl->glPixelStorei(GL_PACK_ROW_LENGTH, 0);

    gl->glBindFramebuffer(GL_READ_FRAMEBUFFER, s.yFbo);
    gl->glReadPixels(0, 0, outW, outH, GL_RED, GL_UNSIGNED_BYTE, nullptr);

    gl->glBindFramebuffer(GL_READ_FRAMEBUFFER, s.uvFbo);
    gl->glReadPixels(0, 0, outW / 2, outH / 2, GL_RG, GL_UNSIGNED_BYTE,
                     reinterpret_cast<void *>(GLintptr(outW) * outH));

    gl->glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    gl->glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    gl->glPixelStorei(GL_PACK_ALIGNMENT, 4);

    gl->glFlush();
    s.fence = gl->glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    return s.fence != nullptr;
}

bool GlRuntime::mapNv12Slot(int slot, uint8_t *y, int yStride, uint8_t *uv, int uvStride, int width,
                            int height)
{
    auto *gl = functions();
    if (!gl || !y || !uv || yStride < width || uvStride < width || slot < 0
        || slot >= kExportNv12Slots)
        return false;

    ExportNv12Slot &s = m_exportNv12[slot];
    if (!s.pbo || s.width != width || s.height != height)
        return false;

    if (s.fence) {
        const GLenum wait =
            gl->glClientWaitSync(s.fence, GL_SYNC_FLUSH_COMMANDS_BIT, GLuint64(1'000'000'000));
        gl->glDeleteSync(s.fence);
        s.fence = nullptr;
        if (wait == GL_WAIT_FAILED)
            return false;
    }

    const GLsizeiptr yBytes = GLsizeiptr(width) * height;
    gl->glBindBuffer(GL_PIXEL_PACK_BUFFER, s.pbo);
    auto *mapped = static_cast<const uint8_t *>(
        gl->glMapBufferRange(GL_PIXEL_PACK_BUFFER, 0, yBytes + GLsizeiptr(width) * (height / 2),
                             GL_MAP_READ_BIT));
    if (!mapped) {
        gl->glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
        return false;
    }

    const uint8_t *srcY = mapped;
    const uint8_t *srcUv = mapped + yBytes;
    for (int row = 0; row < height; ++row)
        memcpy(y + row * yStride, srcY + row * width, size_t(width));
    for (int row = 0; row < height / 2; ++row)
        memcpy(uv + row * uvStride, srcUv + row * width, size_t(width));

    gl->glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
    gl->glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    return true;
}

void GlRuntime::waitPresentFence(int slotIndex)
{
    if (slotIndex < 0 || slotIndex >= kPresentRingSize)
        return;
    GLsync &fence = m_presentFence[slotIndex];
    if (!fence)
        return;
    auto *gl = functions();
    if (!gl) {
        fence = nullptr;
        return;
    }
    // Only wait for this slot's prior publish — not the whole GPU pipeline.
    gl->glClientWaitSync(fence, GL_SYNC_FLUSH_COMMANDS_BIT, GLuint64(100'000'000)); // 100 ms
    gl->glDeleteSync(fence);
    fence = nullptr;
}

void GlRuntime::destroyImageUploadCache()
{
    auto *gl = functions();
    for (CachedUpload &entry : m_imageUploadLru) {
        if (gl && entry.texture)
            gl->glDeleteTextures(1, &entry.texture);
        entry.texture = 0;
    }
    m_imageUploadLru.clear();
    m_imageUploadIndex.clear();
}

void GlRuntime::destroyVideoUploadState()
{
    unregisterCudaResources();
    auto *gl = functions();
    if (gl) {
        if (m_videoY) {
            gl->glDeleteTextures(1, &m_videoY);
            m_videoY = 0;
        }
        if (m_videoUV) {
            gl->glDeleteTextures(1, &m_videoUV);
            m_videoUV = 0;
        }
        if (m_importY) {
            gl->glDeleteTextures(1, &m_importY);
            m_importY = 0;
        }
        if (m_importUV) {
            gl->glDeleteTextures(1, &m_importUV);
            m_importUV = 0;
        }
        if (m_videoPbo[0] || m_videoPbo[1]) {
            gl->glDeleteBuffers(2, m_videoPbo);
            m_videoPbo[0] = m_videoPbo[1] = 0;
        }
    }
    m_videoTexW = 0;
    m_videoTexH = 0;
    m_videoPboIndex = 0;
    av_frame_free(&m_hwImportStaging);
    av_frame_free(&m_importNv12);
    sws_freeContext(m_importSws);
    m_importSws = nullptr;
}

bool GlRuntime::ensureVideoUploadTextures(QOpenGLExtraFunctions *gl, int width, int height)
{
    if (!gl || width < 2 || height < 2 || (width % 2) || (height % 2))
        return false;
    if (m_videoY && m_videoUV && m_videoTexW == width && m_videoTexH == height)
        return true;

    unregisterCudaResources();
    if (m_videoY)
        gl->glDeleteTextures(1, &m_videoY);
    if (m_videoUV)
        gl->glDeleteTextures(1, &m_videoUV);
    m_videoY = m_videoUV = 0;

    gl->glGenTextures(1, &m_videoY);
    gl->glBindTexture(GL_TEXTURE_2D, m_videoY);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    gl->glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, width, height, 0, GL_RED, GL_UNSIGNED_BYTE, nullptr);

    gl->glGenTextures(1, &m_videoUV);
    gl->glBindTexture(GL_TEXTURE_2D, m_videoUV);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    gl->glTexImage2D(GL_TEXTURE_2D, 0, GL_RG8, width / 2, height / 2, 0, GL_RG, GL_UNSIGNED_BYTE,
                     nullptr);

    m_videoTexW = width;
    m_videoTexH = height;
    return m_videoY != 0 && m_videoUV != 0;
}

bool GlRuntime::uploadPlanePbo(QOpenGLExtraFunctions *gl, GLuint texture, int texW, int texH,
                               GLenum internalFormat, GLenum format, const uint8_t *src, int srcPitch,
                               int packedWidth)
{
    Q_UNUSED(internalFormat);
    if (!gl || !texture || !src || texW <= 0 || texH <= 0 || packedWidth <= 0 || srcPitch <= 0)
        return false;
    const qsizetype packed = qsizetype(packedWidth) * texH;
    if (!m_videoPbo[0])
        gl->glGenBuffers(2, m_videoPbo);
    const GLuint pbo = m_videoPbo[m_videoPboIndex];
    m_videoPboIndex ^= 1;
    gl->glBindBuffer(GL_PIXEL_UNPACK_BUFFER, pbo);
    gl->glBufferData(GL_PIXEL_UNPACK_BUFFER, packed, nullptr, GL_STREAM_DRAW);
    void *dst = gl->glMapBufferRange(GL_PIXEL_UNPACK_BUFFER, 0, packed,
                                     GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT);
    if (!dst) {
        gl->glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
        return false;
    }
    if (srcPitch == packedWidth) {
        memcpy(dst, src, size_t(packed));
    } else {
        auto *out = static_cast<uint8_t *>(dst);
        const int rowBytes = qMin(packedWidth, srcPitch);
        for (int y = 0; y < texH; ++y)
            memcpy(out + size_t(y) * packedWidth, src + size_t(y) * srcPitch, size_t(rowBytes));
    }
    gl->glUnmapBuffer(GL_PIXEL_UNPACK_BUFFER);
    gl->glBindTexture(GL_TEXTURE_2D, texture);
    gl->glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    gl->glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    gl->glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, texW, texH, format, GL_UNSIGNED_BYTE, nullptr);
    gl->glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
    return true;
}

void GlRuntime::unregisterCudaResources()
{
#if !defined(Q_OS_MACOS)
    CudaGlApi &api = cudaGlApi();
    if (!api.ok)
        return;
    if (m_cudaYResource) {
        api.cuGraphicsUnregisterResource(static_cast<CUgraphicsResource>(m_cudaYResource));
        m_cudaYResource = nullptr;
    }
    if (m_cudaUvResource) {
        api.cuGraphicsUnregisterResource(static_cast<CUgraphicsResource>(m_cudaUvResource));
        m_cudaUvResource = nullptr;
    }
    m_cudaTexW = 0;
    m_cudaTexH = 0;
#endif
}

// NVDEC surface -> the same R8 + RG8 pair the convert shader samples, GPU to GPU. Replaces a
// full av_hwframe_transfer_data download and PBO re-upload of every displayed frame.
//
// This was disabled for black flicker on NVIDIA. Two things were wrong: the copy ran on the
// null stream while map/unmap ran on FFmpeg's non-blocking decoder stream, and it went through
// the v1 cuMemcpy2D entry point with a v2-layout descriptor. Both are fixed above; the failure
// flag below stays sticky so any driver that still refuses drops back to the transfer path for
// the rest of the session rather than flickering.
bool GlRuntime::importCudaNv12(QOpenGLExtraFunctions *gl, const AVFrame *frame)
{
#if defined(Q_OS_MACOS)
    Q_UNUSED(gl);
    Q_UNUSED(frame);
    return false;
#else
    if (m_cudaImportFailed || !frame || frame->format != AV_PIX_FMT_CUDA || !cudaSwFormatIsNv12(frame))
        return false;
    CudaGlApi &api = cudaGlApi();
    if (!api.ok) {
        m_cudaImportFailed = true;
        return false;
    }
    CUcontext ctx = nullptr;
    CUstream stream = nullptr;
    if (!cudaContextOf(frame, &ctx, &stream))
        return false;

    const int w = frame->width;
    const int h = frame->height;
    if (!ensureVideoUploadTextures(gl, w, h))
        return false;

    if (api.cuCtxPushCurrent(ctx) != kCuSuccess)
        return false;

    bool ok = false;
    if (m_cudaTexW != w || m_cudaTexH != h || !m_cudaYResource || !m_cudaUvResource) {
        unregisterCudaResources();
        CUgraphicsResource yRes = nullptr;
        CUgraphicsResource uvRes = nullptr;
        if (api.cuGraphicsGLRegisterImage(&yRes, m_videoY, GL_TEXTURE_2D, kCuRegisterWriteDiscard)
                == kCuSuccess
            && api.cuGraphicsGLRegisterImage(&uvRes, m_videoUV, GL_TEXTURE_2D, kCuRegisterWriteDiscard)
                == kCuSuccess) {
            m_cudaYResource = yRes;
            m_cudaUvResource = uvRes;
            m_cudaTexW = w;
            m_cudaTexH = h;
        } else {
            if (yRes)
                api.cuGraphicsUnregisterResource(yRes);
            if (uvRes)
                api.cuGraphicsUnregisterResource(uvRes);
            m_cudaImportFailed = true;
        }
    }

    if (m_cudaYResource && m_cudaUvResource) {
        ok = copyCudaNv12ToTextures(api, stream, static_cast<CUgraphicsResource>(m_cudaYResource),
                                    static_cast<CUgraphicsResource>(m_cudaUvResource), frame, w, h);
        if (!ok)
            m_cudaImportFailed = true;
    }

    CUcontext popped = nullptr;
    api.cuCtxPopCurrent(&popped);
    return ok;
#endif
}

bool GlRuntime::ensureImportTextureNames(QOpenGLExtraFunctions *gl)
{
    if (!gl)
        return false;
    if (m_importY && m_importUV)
        return true;

    // Lazily, not once at init: destroyVideoUploadState() deletes these, and releaseCaches()
    // calls it mid-session. Creating them up front would leave the next import binding name 0.
    for (GLuint *tex : {&m_importY, &m_importUV}) {
        if (*tex)
            continue;
        gl->glGenTextures(1, tex);
        gl->glBindTexture(GL_TEXTURE_2D, *tex);
        gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    // No glTexImage2D: glEGLImageTargetTexture2DOES supplies the storage.
    return m_importY != 0 && m_importUV != 0;
}

// VAAPI surface -> dma-buf -> two EGLImages -> the same R8 + RG8 pair the convert shader already
// samples. Replaces an av_hwframe_transfer_data of the displayed frame on every Intel/AMD host.
namespace {

// Auto engages zero-copy only where the whole chain — surface export, dma-buf import and
// sampling — has been checked end to end. Everything else in this function can detect its own
// failure and fall back; sampling an imported surface wrongly cannot, because it produces a
// picture that looks like a decode bug. An unrecognised driver therefore stays on the PBO
// path instead of being guessed at. Users who want it anyway can still set it explicitly.
bool vaapiDriverIsVerified(VaEglApi &api, void *display, QOpenGLExtraFunctions *gl)
{
    if (!api.vaQueryVendorString || !display || !gl)
        return false;
    const char *vaVendor = api.vaQueryVendorString(display);
    if (!vaVendor || !strstr(vaVendor, "iHD"))
        return false;
    const char *renderer = reinterpret_cast<const char *>(gl->glGetString(GL_RENDERER));
    if (!renderer)
        return false;
    // Mesa's iris. i965, removed from Mesa in 22.0, reports "Mesa DRI Intel(R) ..." and has
    // never been through this path.
    return strstr(renderer, "Mesa") && strstr(renderer, "Intel") && !strstr(renderer, "DRI");
}

} // namespace

bool GlRuntime::importVaapiNv12(QOpenGLExtraFunctions *gl, const AVFrame *frame)
{
#if !defined(DRIFT_VAAPI_IMPORT)
    Q_UNUSED(gl);
    Q_UNUSED(frame);
    return false;
#else
    if (m_vaapiImportFailed || !gl || !frame || frame->format != AV_PIX_FMT_VAAPI)
        return false;

    // Content and configuration mismatches fall through to the PBO path for this frame only.
    // Marking them sticky would let one 10-bit clip disable zero-copy for every 8-bit clip
    // beside it on the timeline: scale_vaapi's size-only retry keeps the native sw_format.
    if (!vaapiSwFormatIsNv12(frame))
        return false;
    void *display = vaapiDisplayOf(frame);
    if (!display)
        return false;

    // Every *detectable* failure below falls back to the PBO upload, but a driver that exports
    // a surface we import successfully and sample wrongly shows up as corrupt preview with
    // nothing to catch. So an explicit setting is honoured as given, and Auto engages only on
    // the driver this has been verified against — see vaapiDriverIsVerified.
    //
    // Neither refusal is sticky: a later test, or a restart after flipping the setting, must
    // still be able to engage the path. Real import failures below are.
    const drift::VaapiZeroCopyMode zeroCopy = drift::vaapiZeroCopyMode();
    if (zeroCopy == drift::VaapiZeroCopyMode::Off)
        return false;

    VaEglApi &api = vaEglApi();
    if (!api.ok) {
        logVaapiImportOnce("libva/EGL entry points or GL_OES_EGL_image missing");
        m_vaapiImportFailed = true;
        return false;
    }

    if (zeroCopy == drift::VaapiZeroCopyMode::Auto) {
        if (m_vaapiAutoVerified < 0)
            m_vaapiAutoVerified = vaapiDriverIsVerified(api, display, gl) ? 1 : 0;
        if (m_vaapiAutoVerified == 0) {
            logVaapiImportOnce("driver outside the verified zero-copy set; set "
                               "preview/vaapiZeroCopy to use it anyway");
            return false;
        }
    }

    // Only valid while the context is current, which exec() guarantees. Under Qt's xcb plugin
    // the GL context is GLX and there is no EGL display to import into at all.
    EGLDisplayHandle egl = api.eglGetCurrentDisplay();
    if (!egl) {
        logVaapiImportOnce("no current EGL display; Qt is probably on GLX rather than EGL");
        m_vaapiImportFailed = true;
        return false;
    }
    const char *eglExts = api.eglQueryString(egl, kEglExtensions);
    if (!eglExts || !strstr(eglExts, "EGL_EXT_image_dma_buf_import")) {
        logVaapiImportOnce("EGL_EXT_image_dma_buf_import missing");
        m_vaapiImportFailed = true;
        return false;
    }
    // Tiled surfaces need their modifier passed through or the import is refused or garbled,
    // but sending modifier attribs without this extension is EGL_BAD_ATTRIBUTE.
    const bool useModifiers = strstr(eglExts, "EGL_EXT_image_dma_buf_import_modifiers") != nullptr;

    const int codedW = frame->width & ~1;
    const int codedH = frame->height & ~1;
    if (codedW < 2 || codedH < 2)
        return false;

    VaDrmPrimeSurfaceDescriptor desc{};
    for (auto &object : desc.objects)
        object.fd = -1;
    const auto surface = VASurfaceID(uintptr_t(frame->data[3]));
    if (api.vaExportSurfaceHandle(display, surface, kVaMemTypeDrmPrime2,
                                  kVaExportReadOnly | kVaExportSeparateLayers, &desc)
        != kVaStatusSuccess) {
        logVaapiImportOnce("vaExportSurfaceHandle failed");
        m_vaapiImportFailed = true;
        return false;
    }
    const ExportedSurfaceFds fds{&desc};
    // The decoder or VPP may still be writing. Failing to sync is not a reason to abandon the
    // path, so its result is deliberately not checked — this matches mpv.
    api.vaSyncSurface(display, surface);

    // Guards against a libva ABI change shifting every field past objects[]: without them a
    // mismatched struct imports plausible-looking garbage instead of failing. SEPARATE_LAYERS
    // is still the export request; the composed NV12 shape is accepted because some drivers
    // ignore that flag.
    struct ImportPlane
    {
        int fd = -1;
        uint32_t offset = 0;
        uint32_t pitch = 0;
        uint32_t fourcc = 0;
        uint64_t modifier = kDrmFormatModInvalid;
    };
    ImportPlane planes[2];
    bool shapeOk = false;
    if (desc.num_objects >= 1 && desc.num_objects <= 4) {
        if (desc.num_layers == 2 && desc.layers[0].drm_format == kDrmFormatR8
            && desc.layers[1].drm_format == kDrmFormatGr88) {
            shapeOk = true;
            for (uint32_t i = 0; i < 2; ++i) {
                const auto &layer = desc.layers[i];
                if (layer.num_planes != 1 || layer.object_index[0] >= desc.num_objects
                    || layer.pitch[0] == 0) {
                    shapeOk = false;
                    break;
                }
                const auto &object = desc.objects[layer.object_index[0]];
                planes[i] = {object.fd, layer.offset[0], layer.pitch[0], layer.drm_format,
                             object.drm_format_modifier};
            }
        } else if (desc.num_layers == 1 && desc.layers[0].drm_format == kDrmFormatNv12
                   && desc.layers[0].num_planes == 2) {
            const auto &layer = desc.layers[0];
            if (layer.object_index[0] < desc.num_objects && layer.object_index[1] < desc.num_objects
                && layer.pitch[0] != 0 && layer.pitch[1] != 0) {
                const auto &obj0 = desc.objects[layer.object_index[0]];
                const auto &obj1 = desc.objects[layer.object_index[1]];
                planes[0] = {obj0.fd, layer.offset[0], layer.pitch[0], kDrmFormatR8,
                             obj0.drm_format_modifier};
                planes[1] = {obj1.fd, layer.offset[1], layer.pitch[1], kDrmFormatGr88,
                             obj1.drm_format_modifier};
                shapeOk = true;
            }
        }
    }
    if (!shapeOk) {
        logVaapiImportOnce(
            QStringLiteral("unexpected drm-prime descriptor (%1)").arg(describePrime(desc)));
        m_vaapiImportFailed = true;
        return false;
    }

    // Decide once per frame, before any EGLImage is created. Importing a tiled buffer as
    // implicit-linear (what radeonsi GFX6–8 exports as MOD_INVALID) samples garbage.
    const uint64_t mod0 = planes[0].modifier;
    const uint64_t mod1 = planes[1].modifier;
    const bool modifierUnknown = mod0 == kDrmFormatModInvalid || mod1 == kDrmFormatModInvalid;
    const bool tiled = (mod0 != kDrmFormatModInvalid && mod0 != kDrmFormatModLinear)
                       || (mod1 != kDrmFormatModInvalid && mod1 != kDrmFormatModLinear);
    if (modifierUnknown) {
        logVaapiImportOnce(
            QStringLiteral("driver reports no DRM modifier (tiling unknown); pre-GFX9 radeonsi "
                           "does this"));
        m_vaapiImportFailed = true;
        return false;
    }
    if (tiled && !useModifiers) {
        logVaapiImportOnce(
            QStringLiteral("EGL_EXT_image_dma_buf_import_modifiers missing; cannot describe tiled "
                           "surface"));
        m_vaapiImportFailed = true;
        return false;
    }
    const bool passModifierAttribs = tiled;

    if (!ensureImportTextureNames(gl))
        return false;

    const GLuint textures[2] = {m_importY, m_importUV};
    for (uint32_t i = 0; i < 2; ++i) {
        const ImportPlane &plane = planes[i];

        // The surface allocation is padded (1080 -> 1088); the picture is not. Sizing the image
        // from the descriptor would sample that padding, and because kQuad puts v=0 at the NDC
        // bottom it would land as a garbage strip along the visual bottom edge. The pitch
        // attribute already carries the stride, so the padded height is never needed.
        int32_t attribs[32];
        int n = 0;
        attribs[n++] = kEglWidth;
        attribs[n++] = i == 0 ? codedW : codedW / 2;
        attribs[n++] = kEglHeight;
        attribs[n++] = i == 0 ? codedH : codedH / 2;
        attribs[n++] = kEglLinuxDrmFourccExt;
        attribs[n++] = int32_t(plane.fourcc);
        attribs[n++] = kEglDmaBufPlane0FdExt;
        attribs[n++] = plane.fd;
        attribs[n++] = kEglDmaBufPlane0OffsetExt;
        attribs[n++] = int32_t(plane.offset);
        attribs[n++] = kEglDmaBufPlane0PitchExt;
        attribs[n++] = int32_t(plane.pitch);
        if (passModifierAttribs) {
            attribs[n++] = kEglDmaBufPlane0ModifierLoExt;
            attribs[n++] = int32_t(plane.modifier & 0xffffffffu);
            attribs[n++] = kEglDmaBufPlane0ModifierHiExt;
            attribs[n++] = int32_t(plane.modifier >> 32);
        }
        attribs[n++] = kEglNone;

        // EGL_NO_CONTEXT is required for EGL_LINUX_DMA_BUF_EXT.
        EGLImageHandle image =
            api.eglCreateImageKHR(egl, nullptr, kEglLinuxDmaBufExt, nullptr, attribs);
        if (!image) {
            logVaapiImportOnce("eglCreateImageKHR rejected the exported dma-buf");
            m_vaapiImportFailed = true;
            return false;
        }
        gl->glBindTexture(GL_TEXTURE_2D, textures[i]);
        api.glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, image);
        // Destroying the image only orphans the handle; the texture sibling keeps the buffer.
        // Doing it here rather than after the convert draw means the early returns between the
        // two cannot leak one.
        api.eglDestroyImageKHR(egl, image);
    }

    // The same two texture names are re-targeted every frame, and a crossfade imports twice
    // within one exec(). That is safe because commands in one context are ordered and the
    // driver holds a reference to what an already-recorded draw sampled.
    //
    // The VA surface itself has to outlive the draw: GpuLayer holds the AVFrame across the
    // whole exec() lambda, markPresentReady's glFlush submits the batch inside it, and
    // ClipReader's preview cache holds further refs. Past that — a teardown that clears the
    // cache mid-flight — this relies on the kernel's implicit fencing on the shared buffer.
    return true;
#endif
}

AVFrame *GlRuntime::ensureSoftwareNv12(const AVFrame *src)
{
    if (!src)
        return nullptr;
    if (src->format == AV_PIX_FMT_NV12)
        return const_cast<AVFrame *>(src);

    if (isHwPixelFormat(static_cast<AVPixelFormat>(src->format))) {
        if (!m_hwImportStaging)
            m_hwImportStaging = av_frame_alloc();
        if (!m_hwImportStaging)
            return nullptr;
        av_frame_unref(m_hwImportStaging);
        if (av_hwframe_transfer_data(m_hwImportStaging, src, 0) < 0) {
            av_frame_unref(m_hwImportStaging);
            return nullptr;
        }
        src = m_hwImportStaging;
        if (src->format == AV_PIX_FMT_NV12)
            return m_hwImportStaging;
    }

    const int tw = src->width & ~1;
    const int th = src->height & ~1;
    if (tw < 2 || th < 2)
        return nullptr;

    m_importSws = sws_getCachedContext(m_importSws, src->width, src->height,
                                       static_cast<AVPixelFormat>(src->format), tw, th, AV_PIX_FMT_NV12,
                                       SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (!m_importSws)
        return nullptr;

    if (!m_importNv12)
        m_importNv12 = av_frame_alloc();
    if (!m_importNv12)
        return nullptr;
    if (m_importNv12->format != AV_PIX_FMT_NV12 || m_importNv12->width != tw
        || m_importNv12->height != th || !m_importNv12->data[0]) {
        av_frame_unref(m_importNv12);
        m_importNv12->format = AV_PIX_FMT_NV12;
        m_importNv12->width = tw;
        m_importNv12->height = th;
        if (av_frame_get_buffer(m_importNv12, 0) < 0) {
            av_frame_unref(m_importNv12);
            return nullptr;
        }
    }
    sws_scale(m_importSws, src->data, src->linesize, 0, src->height, m_importNv12->data,
              m_importNv12->linesize);
    m_importNv12->colorspace = src->colorspace;
    m_importNv12->color_range = src->color_range;
    return m_importNv12;
}

GlTarget &GlRuntime::acquirePresentTarget(int width, int height)
{
    const int w = qMax(1, width);
    const int h = qMax(1, height);

    const int slotIndex = m_presentNext;
    GlTarget &slot = m_presentRing[slotIndex];
    m_presentNext = (m_presentNext + 1) % kPresentRingSize;

    // The scene graph may still be sampling this ring slot from a previous publish.
    // Wait for that fence before redrawing into the same FBO.
    waitPresentFence(slotIndex);

    if (!slot.isValid() || slot.width != w || slot.height != h) {
        QOpenGLFramebufferObjectFormat fmt;
        fmt.setAttachment(QOpenGLFramebufferObject::NoAttachment);
        slot.fbo = std::make_unique<QOpenGLFramebufferObject>(w, h, fmt);
        slot.width = w;
        slot.height = h;
    }
    return slot;
}

void GlRuntime::markPresentReady(GlTarget &presentTarget)
{
    auto *gl = functions();
    if (!gl || !presentTarget.isValid())
        return;

    int slotIndex = -1;
    for (int i = 0; i < kPresentRingSize; ++i) {
        if (&m_presentRing[i] == &presentTarget) {
            slotIndex = i;
            break;
        }
    }
    if (slotIndex < 0)
        return;

    // Publish without a client wait: Qt Quick draws on the next vsync. The ring
    // waits this fence in acquirePresentTarget before reuse of the same slot.
    if (m_presentFence[slotIndex]) {
        gl->glDeleteSync(m_presentFence[slotIndex]);
        m_presentFence[slotIndex] = nullptr;
    }
    gl->glFlush();
    m_presentFence[slotIndex] = gl->glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
}

QOpenGLShaderProgram *GlRuntime::builtinProgram(const QString &id, const char *vertexSource,
                                                const char *fragmentSource)
{
    return builtinProgram(id, vertexSource, fragmentSource, nullptr);
}

QOpenGLShaderProgram *GlRuntime::builtinProgram(const QString &id, const char *vertexSource,
                                                const char *fragmentSource, const char *geom)
{
    CompiledEffect &cached = programs[id];
    if (cached.ok)
        return cached.passes[0].program.get();

    cached = CompiledEffect{};
    cached.id = id;

    CompiledPass pass;
    pass.program = std::make_unique<QOpenGLShaderProgram>();
    if (!pass.program->addShaderFromSourceCode(QOpenGLShader::Vertex,
                                               translateShader(vertexSource, false))
        || (geom && !pass.program->addShaderFromSourceCode(QOpenGLShader::Geometry, geom))
        || !pass.program->addShaderFromSourceCode(QOpenGLShader::Fragment,
                                                  translateShader(fragmentSource, true))
        || !pass.program->link()) {
        qWarning("GlRuntime: builtin program '%s' failed: %s", qPrintable(id),
                 qPrintable(pass.program->log()));
        programs.erase(id);
        return nullptr;
    }

    cached.passes.push_back(std::move(pass));
    cached.ok = true;
    return cached.passes[0].program.get();
}

CompiledEffect *GlRuntime::compile(const QString &cacheKey, const drift::GpuEffectDefinition &gpu)
{
    QString sourceSig;
    for (const drift::GpuEffectPass &pass : gpu.passes)
        sourceSig += pass.fragmentShaderSource;
    sourceSig += QLatin1Char('#');
    sourceSig += QString::number(gpu.passes.size());

    CompiledEffect &cached = programs[cacheKey];
    if (cached.ok && cached.id == cacheKey && cached.sourceSig == sourceSig)
        return &cached;

    cached = CompiledEffect{};
    cached.id = cacheKey;
    cached.sourceSig = sourceSig;
    cached.passes.reserve(static_cast<size_t>(gpu.passes.size()));

    for (const drift::GpuEffectPass &pass : gpu.passes) {
        CompiledPass cp;
        cp.program = std::make_unique<QOpenGLShaderProgram>();
        if (!cp.program->addShaderFromSourceCode(QOpenGLShader::Vertex,
                                                translateShader(kQuadVertexShader, false))) {
            qWarning("GlRuntime: vertex shader compile failed for %s: %s", qPrintable(cacheKey),
                     qPrintable(cp.program->log()));
            programs.erase(cacheKey);
            return nullptr;
        }
        if (!cp.program->addShaderFromSourceCode(QOpenGLShader::Fragment,
                                                translateShader(pass.fragmentShaderSource, true))) {
            qWarning("GlRuntime: fragment compile failed for %s pass %d (%s): %s", qPrintable(cacheKey),
                     pass.passIndex, qPrintable(pass.fragmentShaderFile), qPrintable(cp.program->log()));
            programs.erase(cacheKey);
            return nullptr;
        }
        if (!cp.program->link()) {
            qWarning("GlRuntime: link failed for %s pass %d: %s", qPrintable(cacheKey), pass.passIndex,
                     qPrintable(cp.program->log()));
            programs.erase(cacheKey);
            return nullptr;
        }
        cached.passes.push_back(std::move(cp));
    }
    cached.ok = true;
    return &cached;
}

GLuint uploadTexture(QOpenGLExtraFunctions *gl, const QImage &image, bool flipVertically)
{
    QImage rgba = image.convertToFormat(QImage::Format_RGBA8888);
    if (flipVertically)
        rgba = rgba.mirrored(false, true);
    GLuint tex = 0;
    gl->glGenTextures(1, &tex);
    gl->glBindTexture(GL_TEXTURE_2D, tex);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    gl->glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    gl->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, rgba.width(), rgba.height(), 0, GL_RGBA, GL_UNSIGNED_BYTE,
                     rgba.constBits());
    return tex;
}

bool blitTextureToTarget(GlRuntime &rt, QOpenGLExtraFunctions *gl, GLuint srcTex, GlTarget &dest)
{
    if (!rt.copyProgram || !dest.isValid())
        return false;
    dest.fbo->bind();
    gl->glViewport(0, 0, dest.width, dest.height);
    gl->glDisable(GL_BLEND);
    gl->glClearColor(0.f, 0.f, 0.f, 0.f);
    gl->glClear(GL_COLOR_BUFFER_BIT);
    rt.copyProgram->bind();
    rt.copyProgram->setUniformValue("u_currentTexture", 0);
    gl->glActiveTexture(GL_TEXTURE0);
    gl->glBindTexture(GL_TEXTURE_2D, srcTex);
    gl->glBindVertexArray(rt.vao);
    gl->glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    gl->glBindVertexArray(0);
    rt.copyProgram->release();
    dest.fbo->release();
    return true;
}

// Static package assets live for the process lifetime. They are plain uploads (not FBO-backed),
// so they must be flipped to match the Y layout of the FBO-promoted source targets.
GLuint staticTexture(GlRuntime &rt, QOpenGLExtraFunctions *gl, const QString &path)
{
    const auto it = rt.staticTextures.find(path);
    if (it != rt.staticTextures.end())
        return it->second;

    QImage image;
    if (!image.load(path) || image.isNull()) {
        qWarning("GlRuntime: failed to load texture '%s'", qPrintable(path));
        rt.staticTextures[path] = 0;
        return 0;
    }
    const GLuint tex = uploadTexture(gl, image, /*flipVertically=*/true);
    gl->glBindTexture(GL_TEXTURE_2D, tex);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    rt.staticTextures[path] = tex;
    return tex;
}

GLuint cachedUploadTexture(GlRuntime &rt, QOpenGLExtraFunctions *gl, const QImage &image)
{
    if (image.isNull() || !gl)
        return 0;

    const QImage rgba = image.convertToFormat(QImage::Format_RGBA8888);
    const qint64 key = rgba.cacheKey();
    auto indexIt = rt.m_imageUploadIndex.find(key);
    if (indexIt != rt.m_imageUploadIndex.end()) {
        // LRU touch.
        rt.m_imageUploadLru.splice(rt.m_imageUploadLru.begin(), rt.m_imageUploadLru, indexIt->second);
        return indexIt->second->texture;
    }

    const GLuint tex = uploadTexture(gl, rgba);
    if (!tex)
        return 0;

    while (rt.m_imageUploadLru.size() >= GlRuntime::kMaxCachedUploads) {
        GlRuntime::CachedUpload &old = rt.m_imageUploadLru.back();
        rt.m_imageUploadIndex.erase(old.cacheKey);
        if (old.texture)
            gl->glDeleteTextures(1, &old.texture);
        rt.m_imageUploadLru.pop_back();
    }

    rt.m_imageUploadLru.push_front(GlRuntime::CachedUpload{key, tex, rgba.width(), rgba.height()});
    rt.m_imageUploadIndex[key] = rt.m_imageUploadLru.begin();
    return tex;
}

GlTarget promoteImageToTarget(GlRuntime &rt, QOpenGLExtraFunctions *gl, const QImage &image,
                              const QSize &fallbackSize)
{
    QImage rgba = image.isNull() ? QImage(fallbackSize, QImage::Format_RGBA8888)
                                 : image.convertToFormat(QImage::Format_RGBA8888);
    if (image.isNull())
        rgba.fill(Qt::transparent);

    GlTarget target = rt.acquireTarget(rgba.width(), rgba.height());
    if (!target.isValid())
        return {};

    const GLuint uploaded = uploadTexture(gl, rgba);
    const bool ok = blitTextureToTarget(rt, gl, uploaded, target);
    gl->glDeleteTextures(1, &uploaded);
    if (!ok) {
        rt.releaseTarget(std::move(target));
        return {};
    }
    return target;
}

GlTarget promoteImageToTargetCached(GlRuntime &rt, QOpenGLExtraFunctions *gl, const QImage &image,
                                    const QSize &fallbackSize)
{
    if (image.isNull())
        return promoteImageToTarget(rt, gl, image, fallbackSize);

    const QImage rgba = image.convertToFormat(QImage::Format_RGBA8888);
    GlTarget target = rt.acquireTarget(rgba.width(), rgba.height());
    if (!target.isValid())
        return {};

    const GLuint uploaded = cachedUploadTexture(rt, gl, rgba);
    if (!uploaded) {
        rt.releaseTarget(std::move(target));
        return {};
    }
    const bool ok = blitTextureToTarget(rt, gl, uploaded, target);
    // uploaded stays in the LRU cache — do not delete.
    if (!ok) {
        rt.releaseTarget(std::move(target));
        return {};
    }
    return target;
}

GlTarget promoteVideoFrameToTarget(GlRuntime &rt, QOpenGLExtraFunctions *gl,
                                   const PreviewVideoFrame &frame)
{
    if (!gl || !frame.isValid())
        return {};

    const AVFrame *av = frame.frame.get();
    const int codedW = av->width & ~1;
    const int codedH = av->height & ~1;
    if (codedW < 2 || codedH < 2)
        return {};

    // CUDA copies into the pooled textures; VAAPI binds the decoder's own dma-buf into a
    // separate pair, so the draw below has to be told which one holds this frame. Either way
    // the frame never leaves the GPU. Anything neither takes falls through to a hardware
    // transfer and a PBO upload.
    GLuint texY = 0;
    GLuint texUV = 0;
    bool uploaded = rt.importCudaNv12(gl, av);
    if (uploaded) {
        // Read the names back only now. importCudaNv12 allocates the pooled pair through
        // ensureVideoUploadTextures, which deletes and recreates them whenever the frame size
        // changes — and creates them at all on the very first frame. Sampling names captured
        // before the call meant binding texture 0 on that first frame and after every preview
        // resize, which is the black flicker this path was disabled for.
        texY = rt.m_videoY;
        texUV = rt.m_videoUV;
        recordPreviewUploadPath(GlRuntime::PreviewUploadPath::CudaInterop);
    } else if (rt.importVaapiNv12(gl, av)) {
        uploaded = true;
        texY = rt.m_importY;
        texUV = rt.m_importUV;
        recordPreviewUploadPath(GlRuntime::PreviewUploadPath::VaapiDmaBuf);
    }
    if (!uploaded) {
        AVFrame *nv12 = rt.ensureSoftwareNv12(av);
        if (!nv12 || nv12->format != AV_PIX_FMT_NV12)
            return {};
        const int w = nv12->width;
        const int h = nv12->height;
        if (!rt.ensureVideoUploadTextures(gl, w, h))
            return {};
        if (!rt.uploadPlanePbo(gl, rt.m_videoY, w, h, GL_R8, GL_RED, nv12->data[0], nv12->linesize[0], w)
            || !rt.uploadPlanePbo(gl, rt.m_videoUV, w / 2, h / 2, GL_RG8, GL_RG, nv12->data[1],
                                  nv12->linesize[1], w))
            return {};
        texY = rt.m_videoY;
        texUV = rt.m_videoUV;
        recordPreviewUploadPath(GlRuntime::PreviewUploadPath::CpuRoundTrip);
    }

    const int destW = qMax(2, frame.displayWidth() & ~1);
    const int destH = qMax(2, frame.displayHeight() & ~1);
    GlTarget target = rt.acquireTarget(destW, destH);
    if (!target.isValid())
        return {};

    QOpenGLShaderProgram *program =
        rt.builtinProgram(QStringLiteral("__nv12__"), kQuadVertexShader, kNv12FragShader);
    if (!program) {
        rt.releaseTarget(std::move(target));
        return {};
    }

    QVector3D offset;
    QVector3D scale;
    yuvRangeUniforms(frame.colorRange, &offset, &scale);

    target.fbo->bind();
    gl->glViewport(0, 0, destW, destH);
    gl->glDisable(GL_BLEND);
    gl->glClearColor(0.f, 0.f, 0.f, 0.f);
    gl->glClear(GL_COLOR_BUFFER_BIT);
    program->bind();
    program->setUniformValue("u_y", 0);
    program->setUniformValue("u_uv", 1);
    program->setUniformValue("u_yuvToRgb", yuvToRgbMatrix(frame.colorspace));
    program->setUniformValue("u_yuvOffset", offset);
    program->setUniformValue("u_yuvScale", scale);
    program->setUniformValue("u_texMap", texMapForRotation(frame.rotation));
    gl->glActiveTexture(GL_TEXTURE0);
    gl->glBindTexture(GL_TEXTURE_2D, texY);
    gl->glActiveTexture(GL_TEXTURE1);
    gl->glBindTexture(GL_TEXTURE_2D, texUV);
    gl->glBindVertexArray(rt.vao);
    gl->glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    gl->glBindVertexArray(0);
    program->release();
    target.fbo->release();
    return target;
}

GlRuntime::PreviewUploadPath GlRuntime::lastPreviewUploadPath()
{
    QMutexLocker lock(&g_previewImportMutex);
    return g_previewUploadPath;
}

QString GlRuntime::lastVaapiImportReason()
{
    QMutexLocker lock(&g_previewImportMutex);
    return g_vaapiImportReason;
}

void setPackageUniforms(QOpenGLShaderProgram *program, const QMap<QString, QVariant> &parameters,
                        const QSize &resolution, drift::TimeUs timeUs, double progress)
{
    program->setUniformValue("u_currentTexture", 0);
    program->setUniformValue("u_resolution",
                             QVector2D(float(resolution.width()), float(resolution.height())));
    const float timeSec = float(timeUs) / 1'000'000.f;
    program->setUniformValue("u_time", timeSec);
    program->setUniformValue("u_frameIndex", int(std::floor(timeSec * 30.f)));
    // Integer microseconds for hash-stable glitch effects that mirror CPU seeding.
    program->setUniformValue("u_timeUs", float(timeUs));
    program->setUniformValue("u_progress", float(qBound(0.0, progress, 1.0)));

    for (auto it = parameters.constBegin(); it != parameters.constEnd(); ++it) {
        if (drift::isEngineBoundGpuUniform(it.key()))
            continue;
        const int loc = program->uniformLocation(it.key());
        if (loc < 0)
            continue;
        const QVariant &value = it.value();
        if (value.typeId() == QMetaType::Bool) {
            program->setUniformValue(loc, value.toBool() ? 1.f : 0.f);
        } else if (value.userType() == qMetaTypeId<drift::GpuFloatArray>()) {
            const auto array = value.value<drift::GpuFloatArray>();
            if (array.tupleSize > 0 && !array.values.isEmpty()) {
                program->setUniformValueArray(loc, array.values.constData(),
                                              array.values.size() / array.tupleSize,
                                              array.tupleSize);
            }
        } else if (value.typeId() == QMetaType::QString) {
            const QString s = value.toString();
            // File-path params must not fall through to s.toFloat(). A path currently binds as
            // 0.0, which is harmless today but bites the moment a gpu package grows a file param.
            if (s.contains(QLatin1Char('/')) || s.endsWith(QLatin1String(".glb"))
                || s.endsWith(QLatin1String(".gltf"))) {
                continue;
            }
            if (s.startsWith(QLatin1Char('#'))) {
                const QColor c(s);
                program->setUniformValue(loc,
                                         QVector3D(float(c.redF()), float(c.greenF()), float(c.blueF())));
            } else if (it.key() == QLatin1String("position") || it.key() == QLatin1String("blendMode")) {
                float mode = 0.f;
                if (s == QLatin1String("right") || s == QLatin1String("add"))
                    mode = 1.f;
                else if (s == QLatin1String("top") || s == QLatin1String("screen"))
                    mode = 2.f;
                else if (s == QLatin1String("bottom"))
                    mode = 3.f;
                else if (s == QLatin1String("random"))
                    mode = 4.f;
                program->setUniformValue(loc, mode);
            } else {
                program->setUniformValue(loc, s.toFloat());
            }
        } else {
            program->setUniformValue(loc, float(value.toDouble()));
        }
    }
}

GlTarget runPipeline(GlRuntime &rt, QOpenGLExtraFunctions *gl, const QString &cacheKey,
                     const drift::GpuEffectDefinition &gpu, const std::vector<const GlTarget *> &sources,
                     const QMap<QString, QVariant> &parameters, drift::TimeUs timeUs, double progress,
                     const QSize &canvasSize)
{
    CompiledEffect *compiled = rt.compile(cacheKey, gpu);
    if (!compiled || !compiled->ok || compiled->passes.size() != size_t(gpu.passes.size()))
        return {};

    auto sourceTexAt = [&](int index) -> GLuint {
        if (index < 0 || index >= int(sources.size()))
            return sources.empty() ? 0 : sources[0]->texture();
        return sources[size_t(index)]->texture();
    };

    std::map<QString, GlTarget> buffers;
    bool failed = false;
    for (const drift::GpuEffectBufferSpec &spec : gpu.intermediateBuffers) {
        const int w = qMax(1, int(std::lround(canvasSize.width() * spec.scale)));
        const int h = qMax(1, int(std::lround(canvasSize.height() * spec.scale)));
        GlTarget target = rt.acquireTarget(w, h);
        if (!target.isValid()) {
            qWarning("GlRuntime: FBO alloc failed for buffer %s", qPrintable(spec.id));
            failed = true;
            break;
        }
        buffers.emplace(spec.id, std::move(target));
    }

    std::map<QString, GLuint> textures;
    for (const drift::GpuEffectTextureSpec &spec : gpu.textures)
        textures[spec.id] = staticTexture(rt, gl, spec.path);

    GlTarget canvas = rt.acquireTarget(canvasSize.width(), canvasSize.height());
    if (!canvas.isValid()) {
        qWarning("GlRuntime: canvas FBO alloc failed");
        failed = true;
    }

    for (int i = 0; !failed && i < gpu.passes.size(); ++i) {
        const drift::GpuEffectPass &pass = gpu.passes[i];
        QOpenGLShaderProgram *program = compiled->passes[size_t(i)].program.get();

        QSize inputSize = canvasSize;

        GlTarget *outTarget = nullptr;
        if (pass.output.type == drift::GpuEffectPassOutput::Type::Canvas) {
            outTarget = &canvas;
        } else {
            const auto it = buffers.find(pass.output.bufferId);
            if (it == buffers.end()) {
                failed = true;
                break;
            }
            outTarget = &it->second;
        }

        outTarget->fbo->bind();
        gl->glViewport(0, 0, outTarget->width, outTarget->height);
        gl->glDisable(GL_BLEND);
        gl->glClearColor(0.f, 0.f, 0.f, 0.f);
        gl->glClear(GL_COLOR_BUFFER_BIT);

        program->bind();
        setPackageUniforms(program, parameters, inputSize, timeUs, progress);

        // Bind all declared inputs: unit 0 → u_currentTexture, unit i → u_texture{i}.
        const QList<drift::GpuEffectPassInput> inputs =
            pass.inputs.isEmpty() ? QList<drift::GpuEffectPassInput>{drift::GpuEffectPassInput{}}
                                  : pass.inputs;
        int fromUnit = -1;
        int toUnit = -1;
        for (int texUnit = 0; texUnit < inputs.size(); ++texUnit) {
            const drift::GpuEffectPassInput &in = inputs[texUnit];
            GLuint tex = 0;
            switch (in.type) {
            case drift::GpuEffectPassInput::Type::SourceTexture:
                tex = sourceTexAt(in.sourceIndex);
                if (in.sourceIndex == 0)
                    fromUnit = texUnit;
                else if (in.sourceIndex == 1)
                    toUnit = texUnit;
                break;
            case drift::GpuEffectPassInput::Type::Buffer: {
                const auto it = buffers.find(in.bufferId);
                if (it == buffers.end()) {
                    failed = true;
                    break;
                }
                tex = it->second.texture();
                if (texUnit == 0)
                    inputSize = QSize(it->second.width, it->second.height);
                break;
            }
            case drift::GpuEffectPassInput::Type::Texture: {
                const auto it = textures.find(in.textureId);
                tex = it == textures.end() ? 0 : it->second;
                break;
            }
            }
            if (failed)
                break;

            gl->glActiveTexture(GL_TEXTURE0 + texUnit);
            gl->glBindTexture(GL_TEXTURE_2D, tex);
            if (texUnit == 0)
                program->setUniformValue("u_currentTexture", 0);
            else
                program->setUniformValue(qPrintable(QStringLiteral("u_texture%1").arg(texUnit)), texUnit);
        }

        if (!failed) {
            // Transition-friendly aliases pointing at whichever units hold source 0 and source 1.
            // uniformLocation() returns -1 for names a shader does not declare, so this is free.
            if (fromUnit >= 0)
                program->setUniformValue("u_fromTexture", fromUnit);
            if (toUnit >= 0)
                program->setUniformValue("u_toTexture", toUnit);

            // Re-apply resolution after possible buffer-sized primary input.
            program->setUniformValue("u_resolution",
                                     QVector2D(float(inputSize.width()), float(inputSize.height())));

            gl->glBindVertexArray(rt.vao);
            gl->glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
            gl->glBindVertexArray(0);
        }

        program->release();
        outTarget->fbo->release();
    }

    for (auto &entry : buffers)
        rt.releaseTarget(std::move(entry.second));
    buffers.clear();

    if (failed) {
        qWarning("GlRuntime: pass failed for %s — passthrough", qPrintable(cacheKey));
        rt.releaseTarget(std::move(canvas));
        return {};
    }
    return canvas;
}

} // namespace drift::gl
