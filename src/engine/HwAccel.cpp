#include "HwAccel.h"

#include "GpuCompositor.h"
#include "GpuDevice.h"
#include "GpuPreference.h"

#include <QByteArray>
#include <QHash>
#include <QMutex>
#include <QMutexLocker>
#include <QtGlobal>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavcodec/codec.h>
#include <libavutil/log.h>
}

#if !defined(Q_OS_WIN) && !defined(Q_OS_MACOS)
#include <dlfcn.h>
#endif

namespace drift::hwaccel {
namespace {

#if !defined(Q_OS_WIN) && !defined(Q_OS_MACOS)
// FFmpeg reaches libva through implib-gen stubs whose generated dlopen failure path is
// assert(0), so asking it about VAAPI on a host with no libva aborts the process instead
// of reporting no device. Load the two the default-device path actually calls into first;
// libva-x11 is not on that path, so its absence is fine. The handles are deliberately
// never closed: FFmpeg dlopens the same sonames moments later, and unloading underneath
// it would undo the check.
bool vaapiLoadable()
{
    static const bool ok = dlopen("libva.so.2", RTLD_LAZY | RTLD_LOCAL) != nullptr
        && dlopen("libva-drm.so.2", RTLD_LAZY | RTLD_LOCAL) != nullptr;
    return ok;
}
#endif

} // namespace

namespace {

QMutex g_renderVendorMutex;
QString g_renderVendor;

} // namespace

QString renderVendor()
{
    {
        QMutexLocker lock(&g_renderVendorMutex);
        if (!g_renderVendor.isEmpty())
            return g_renderVendor;
    }
    // Nothing seeded yet: fall back to whatever the compositor last reported. A snapshot,
    // so this stays empty until GL has actually been brought up somewhere.
    return GpuCompositor::status().vendor;
}

void setRenderVendor(const QString &vendor)
{
    QMutexLocker lock(&g_renderVendorMutex);
    g_renderVendor = vendor;
}

QList<Backend> decodeBackendOrder()
{
    return decodeBackendOrderFor(renderVendor());
}

namespace {

// Name of the first adapter from a PCI vendor, for the "decodes on ..." half of the warning.
QString adapterNameForVendor(quint16 vendorId)
{
    for (const drift::gpu::Adapter &adapter : drift::gpu::enumerateAdapters()) {
        if (adapter.vendorId != vendorId)
            continue;
        return adapter.name.isEmpty() ? adapter.vendor : adapter.name;
    }
    return {};
}

} // namespace

namespace {

// The DRM render node on the GPU `vendor` draws with, empty when that GPU has none — or when
// nothing identifies it. Linux only.
QByteArray renderGpuVaapiNode(const QString &vendor)
{
#if defined(Q_OS_LINUX)
    const drift::gpu::PciId render = drift::gpu::renderPciId(vendor);
    if (!render.isValid())
        return {};
    for (const auto &[node, id] : drift::gpu::drmRenderNodes()) {
        if (id == render)
            return node.toUtf8();
    }
#else
    Q_UNUSED(vendor);
#endif
    return {};
}

} // namespace

RenderMatchInfo describeRenderMatch(Backend backend, const QString &vendorIn)
{
    const QString vendor = vendorIn.isEmpty() ? renderVendor() : vendorIn;

    RenderMatchInfo info;
    info.renderGpu = drift::gpu::adapterName(drift::gpu::renderPciId(vendor));
    if (info.renderGpu.isEmpty())
        info.renderGpu = drift::gpu::pciVendorName(drift::gpu::vendorIdForGlVendor(vendor));

    switch (backend) {
    case Backend::Cuda: {
        // CUDA only ever runs on NVIDIA, so the vendor string settles this without needing to
        // identify either device — which is why this is the one verdict that works on GLX too.
        const quint16 glVendor = drift::gpu::vendorIdForGlVendor(vendor);
        if (glVendor == 0) {
            info.match = RenderMatch::Unknown;
            break;
        }
        info.decodeGpu = adapterNameForVendor(0x10de);
        info.match = glVendor == 0x10de ? RenderMatch::Matches : RenderMatch::Mismatch;
        break;
    }
    case Backend::D3d11va:
#if defined(Q_OS_WIN)
        // deviceString() opens D3D11VA on the adapter GL draws on, so these agree by
        // construction — unless the GL vendor named no adapter, and then nothing is known.
        info.match = drift::gpu::adapterIndexForGlVendor(vendor) >= 0 ? RenderMatch::Matches
                                                                      : RenderMatch::Unknown;
#else
        info.match = RenderMatch::Matches;
#endif
        break;
    case Backend::Vaapi: {
#if defined(Q_OS_LINUX)
        const QList<QPair<QString, drift::gpu::PciId>> nodes = drift::gpu::drmRenderNodes();
        if (!drift::gpu::renderPciId(vendor).isValid() || nodes.isEmpty()) {
            info.match = RenderMatch::Unknown;
            break;
        }
        if (!renderGpuVaapiNode(vendor).isEmpty()) {
            // deviceString() pins libva to that node, so the decoder lands on the render GPU.
            info.match = RenderMatch::Matches;
            info.decodeGpu = info.renderGpu;
            break;
        }
        // The rendering GPU has no render node, so libva must be driving a different card.
        // Note this is sysfs alone: a node that exists but that libva cannot open — an NVIDIA
        // one without nvidia-vaapi-driver — still reads as a match here. That case shows up as
        // a real cpu-roundtrip during playback, which is what the observed warning is for.
        info.match = RenderMatch::Mismatch;
        info.decodeGpu = drift::gpu::adapterName(nodes.first().second);
#else
        info.match = RenderMatch::Matches;
#endif
        break;
    }
    case Backend::VideoToolbox:
    case Backend::MediaCodec:
    case Backend::None:
        info.match = RenderMatch::Matches;
        break;
    }
    return info;
}

bool backendMatchesRenderer(Backend backend, const QString &vendor)
{
    if (vendor.isEmpty())
        return true;
    return describeRenderMatch(backend, vendor).match != RenderMatch::Mismatch;
}

QList<Backend> decodeAttemptOrder(Backend pinned, bool pinnedOnly, const QString &vendor)
{
    if (pinnedOnly && pinned != Backend::None)
        return {pinned};
    QList<Backend> order = decodeBackendOrderFor(vendor);
    if (pinned != Backend::None && backendMatchesRenderer(pinned, vendor)) {
        order.removeOne(pinned);
        order.prepend(pinned);
    }
    return order;
}

QByteArray deviceString(AVHWDeviceType type)
{
#if defined(Q_OS_WIN)
    if (type == AV_HWDEVICE_TYPE_D3D11VA) {
        const int index = drift::gpu::adapterIndexForGlVendor(renderVendor());
        if (index >= 0)
            return QByteArray::number(index);
    }
#elif defined(Q_OS_LINUX)
    if (type == AV_HWDEVICE_TYPE_VAAPI) {
        // Same reasoning as the D3D11VA branch: libva's default node is whichever card the
        // system lists first, not the one drawing, and dma-buf import only works when the two
        // are the same device. Empty when the render GPU has no node of its own — libva's
        // default still decodes there, just on the other card, and saying nothing is better
        // than pinning a node that would drop VAAPI out of the picker altogether.
        return renderGpuVaapiNode(renderVendor());
    }
#else
    Q_UNUSED(type);
#endif
    return {};
}

QList<Backend> decodeBackendOrderFor(const QString &vendor)
{
#if defined(Q_OS_ANDROID)
    Q_UNUSED(vendor);
    return {Backend::MediaCodec};
#elif defined(Q_OS_MACOS)
    Q_UNUSED(vendor);
    return {Backend::VideoToolbox};
#else
#if defined(Q_OS_WIN)
    QList<Backend> order = {Backend::Cuda, Backend::D3d11va};
#else
    QList<Backend> order = {Backend::Cuda, Backend::Vaapi};
#endif

    // CUDA leads because it is the only backend with both a fast readback and a surface
    // scaler — but only when the frames it decodes are already on the GPU that will draw
    // them. On a hybrid laptop whose display is wired to the integrated GPU, putting NVDEC
    // first means every previewed frame is decoded on the discrete card, pulled across PCIe
    // into system memory, and uploaded again to the integrated one: two bus crossings per
    // frame to reach a device that could have decoded it in place. Follow the renderer
    // instead, and leave the order alone when there is no GL context to ask yet.
    if (!vendor.isEmpty() && !vendor.contains(QStringLiteral("NVIDIA"), Qt::CaseInsensitive)) {
        order.removeOne(Backend::Cuda);
        order.append(Backend::Cuda);
    }
    return order;
#endif
}

QList<Backend> availableDecodeBackends()
{
    // Honour the kill switch here, not just at decode time: this is what probes the
    // devices, and a wedged driver would otherwise hang the picker on startup — the
    // exact case someone sets DRIFT_NO_HWACCEL to get out of.
    if (disabledByEnv())
        return {};

    QList<Backend> out;
    for (const Backend backend : decodeBackendOrder()) {
        // deviceAvailable() is the wrong question for MediaCodec: FFmpeg's device init returns
        // success with a null surface on any Android build, so it would list the backend on a
        // device with no usable decoder at all.
        const bool ok = backend == Backend::MediaCodec ? mediaCodecDecodeAvailable()
                                                       : deviceAvailable(deviceType(backend));
        if (ok)
            out.append(backend);
    }
    return out;
}

AVHWDeviceType deviceType(Backend backend)
{
    switch (backend) {
    case Backend::Cuda:
        return AV_HWDEVICE_TYPE_CUDA;
    case Backend::D3d11va:
        return AV_HWDEVICE_TYPE_D3D11VA;
    case Backend::Vaapi:
        return AV_HWDEVICE_TYPE_VAAPI;
    case Backend::VideoToolbox:
        return AV_HWDEVICE_TYPE_VIDEOTOOLBOX;
    case Backend::MediaCodec:
        return AV_HWDEVICE_TYPE_MEDIACODEC;
    case Backend::None:
        break;
    }
    return AV_HWDEVICE_TYPE_NONE;
}

const char *name(Backend backend)
{
    switch (backend) {
    case Backend::Cuda:
        return "NVDEC";
    case Backend::D3d11va:
        return "Direct3D 11";
    case Backend::Vaapi:
        return "VAAPI";
    case Backend::VideoToolbox:
        return "VideoToolbox";
    case Backend::MediaCodec:
        return "MediaCodec";
    case Backend::None:
        break;
    }
    return "";
}

QString id(Backend backend)
{
    switch (backend) {
    case Backend::Cuda:
        return QStringLiteral("nvdec");
    case Backend::D3d11va:
        return QStringLiteral("d3d11va");
    case Backend::Vaapi:
        return QStringLiteral("vaapi");
    case Backend::VideoToolbox:
        return QStringLiteral("videotoolbox");
    case Backend::MediaCodec:
        return QStringLiteral("mediacodec");
    case Backend::None:
        break;
    }
    return {};
}

Backend backendFromId(const QString &id)
{
    for (const Backend backend : {Backend::Cuda, Backend::D3d11va, Backend::Vaapi,
                                  Backend::VideoToolbox, Backend::MediaCodec}) {
        if (drift::hwaccel::id(backend) == id)
            return backend;
    }
    return Backend::None;
}

const char *scaleFilter(Backend backend)
{
    switch (backend) {
    case Backend::Cuda:
        return "scale_cuda";
    case Backend::Vaapi:
        return "scale_vaapi";
    case Backend::VideoToolbox:
        return "scale_vt";
    // MediaCodec cannot downscale on the way out either, so the preview's sws pass stands.
    case Backend::MediaCodec:
    case Backend::D3d11va:
    case Backend::None:
        break;
    }
    return nullptr;
}

bool deviceAvailable(AVHWDeviceType type)
{
    if (type == AV_HWDEVICE_TYPE_NONE)
        return false;
#if !defined(Q_OS_WIN) && !defined(Q_OS_MACOS)
    if (type == AV_HWDEVICE_TYPE_VAAPI && !vaapiLoadable())
        return false;
#endif
    // Keyed on the device string too: it follows the render vendor, which is only seeded once a
    // GL context has been probed.
    const QByteArray device = deviceString(type);
    const QString key = QStringLiteral("%1:%2").arg(static_cast<int>(type)).arg(QString::fromLatin1(device));

    static QMutex mutex;
    static QHash<QString, bool> cache;
    QMutexLocker lock(&mutex);
    const auto it = cache.constFind(key);
    if (it != cache.cend())
        return it.value();

    AVBufferRef *ctx = nullptr;
    const int previousLog = av_log_get_level();
    av_log_set_level(AV_LOG_QUIET);
    const int err = av_hwdevice_ctx_create(&ctx, type, device.isEmpty() ? nullptr : device.constData(),
                                           nullptr, 0);
    av_log_set_level(previousLog);
    if (ctx)
        av_buffer_unref(&ctx);
    const bool ok = err >= 0;
    cache.insert(key, ok);
    return ok;
}

bool disabledByEnv()
{
    return qEnvironmentVariableIsSet("DRIFT_NO_HWACCEL");
}

const AVCodec *findMediaCodecDecoder(AVCodecID codecId)
{
#ifndef Q_OS_ANDROID
    Q_UNUSED(codecId);
    return nullptr;
#else
    const char *name = nullptr;
    switch (codecId) {
    case AV_CODEC_ID_H264: name = "h264_mediacodec"; break;
    case AV_CODEC_ID_HEVC: name = "hevc_mediacodec"; break;
    case AV_CODEC_ID_VP9:  name = "vp9_mediacodec";  break;
    case AV_CODEC_ID_VP8:  name = "vp8_mediacodec";  break;
    case AV_CODEC_ID_AV1:  name = "av1_mediacodec";  break;
    default: return nullptr;
    }
    return avcodec_find_decoder_by_name(name);
#endif
}

bool mediaCodecDecodeAvailable()
{
#ifndef Q_OS_ANDROID
    return false;
#else
    if (disabledByEnv() || qEnvironmentVariableIsSet("DRIFT_NO_MEDIACODEC"))
        return false;
    for (const AVCodecID id : {AV_CODEC_ID_H264, AV_CODEC_ID_HEVC, AV_CODEC_ID_VP9,
                               AV_CODEC_ID_VP8, AV_CODEC_ID_AV1}) {
        if (findMediaCodecDecoder(id))
            return true;
    }
    return false;
#endif
}

const AVCodec *findDecoder(AVCodecID codecId, AVHWDeviceType type, AVPixelFormat *pixFmt)
{
    if (type == AV_HWDEVICE_TYPE_NONE)
        return nullptr;
    void *iter = nullptr;
    while (const AVCodec *codec = av_codec_iterate(&iter)) {
        if (!av_codec_is_decoder(codec) || codec->id != codecId)
            continue;
        for (int i = 0;; ++i) {
            const AVCodecHWConfig *config = avcodec_get_hw_config(codec, i);
            if (!config)
                break;
            if ((config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX)
                && config->device_type == type) {
                if (pixFmt)
                    *pixFmt = config->pix_fmt;
                return codec;
            }
        }
    }
    return nullptr;
}

} // namespace drift::hwaccel
