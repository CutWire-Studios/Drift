#include "D3d11GlInterop.h"

#include <QOpenGLContext>
#include <QOpenGLExtraFunctions>

#include <cstring>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

extern "C" {
#include <libavutil/frame.h>
#include <libavutil/hwcontext.h>
#include <libavutil/hwcontext_d3d11va.h>
#include <libavutil/pixdesc.h>
}

using Microsoft::WRL::ComPtr;

namespace drift::gl {
namespace {

constexpr GLenum kWglAccessReadOnly = 0x0000;

// Readers each open their own D3D11VA device, so a timeline with several hardware clips alternates
// between devices frame to frame. Opening an interop device per switch would be far more expensive
// than keeping a few; four covers a crossfade plus a picture-in-picture without holding decoder
// devices alive indefinitely.
constexpr size_t kMaxDevices = 4;

// A fullscreen triangle from SV_VertexID, so there is no vertex buffer or input layout to manage.
// uv (0,0) is the top-left output pixel and samples the surface's first row, which keeps rows top
// first: the same layout the PBO and CUDA paths write, and the one GlRuntime's quad expects.
//
// The decoder pads its surfaces (1088 rows for 1080p). cropScale maps the output onto the picture,
// and because output and plane sizes are in the same ratio every output pixel centre lands exactly
// on a source texel centre, so point sampling copies the planes rather than filtering them.
constexpr const char *kPlaneShader = R"(
cbuffer Crop : register(b0)
{
    float2 cropScale;
    float2 cropPad;
};
Texture2D plane : register(t0);
SamplerState pointSampler : register(s0);

struct VsOut
{
    float4 pos : SV_Position;
    float2 uv : TEXCOORD0;
};

VsOut vs_main(uint id : SV_VertexID)
{
    VsOut o;
    o.uv = float2((id << 1) & 2, id & 2);
    o.pos = float4(o.uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    return o;
}

float4 ps_luma(VsOut i) : SV_Target
{
    return float4(plane.Sample(pointSampler, i.uv * cropScale).r, 0.0, 0.0, 1.0);
}

float4 ps_chroma(VsOut i) : SV_Target
{
    return float4(plane.Sample(pointSampler, i.uv * cropScale).rg, 0.0, 1.0);
}
)";

struct WglDxApi
{
    const char *(WINAPI *getExtensionsStringARB)(HDC) = nullptr;
    HANDLE (WINAPI *openDevice)(void *) = nullptr;
    BOOL (WINAPI *closeDevice)(HANDLE) = nullptr;
    HANDLE (WINAPI *registerObject)(HANDLE, void *, GLuint, GLenum, GLenum) = nullptr;
    BOOL (WINAPI *unregisterObject)(HANDLE, HANDLE) = nullptr;
    BOOL (WINAPI *lockObjects)(HANDLE, GLint, HANDLE *) = nullptr;
    BOOL (WINAPI *unlockObjects)(HANDLE, GLint, HANDLE *) = nullptr;
};

QString hresultString(HRESULT hr)
{
    return QStringLiteral("0x%1").arg(quint32(hr), 8, 16, QLatin1Char('0'));
}

// FFmpeg's device lock guards the immediate context against its own decode and transfer calls,
// which run on the decoder thread while this runs on the GL thread.
class AvD3d11Lock
{
public:
    explicit AvD3d11Lock(AVD3D11VADeviceContext *hw)
        : m_hw(hw)
    {
        if (m_hw->lock)
            m_hw->lock(m_hw->lock_ctx);
    }
    ~AvD3d11Lock()
    {
        if (m_hw->unlock)
            m_hw->unlock(m_hw->lock_ctx);
    }
    AvD3d11Lock(const AvD3d11Lock &) = delete;
    AvD3d11Lock &operator=(const AvD3d11Lock &) = delete;

private:
    AVD3D11VADeviceContext *m_hw;
};

} // namespace

struct D3d11GlInterop::Impl
{
    struct Device
    {
        ComPtr<ID3D11Device> device;
        HANDLE glDevice = nullptr;
        ComPtr<ID3D11VertexShader> vs;
        ComPtr<ID3D11PixelShader> psLuma;
        ComPtr<ID3D11PixelShader> psChroma;
        ComPtr<ID3D11SamplerState> sampler;
        ComPtr<ID3D11Buffer> crop;

        int surfaceW = 0;
        int surfaceH = 0;
        int width = 0;
        int height = 0;
        ComPtr<ID3D11Texture2D> nv12;
        ComPtr<ID3D11ShaderResourceView> lumaView;
        ComPtr<ID3D11ShaderResourceView> chromaView;
        // RGBA8 rather than R8/RG8: it is the one format every WGL_NV_DX_interop2 implementation
        // shares, and the convert shader only reads .r and .rg anyway.
        ComPtr<ID3D11Texture2D> lumaTarget;
        ComPtr<ID3D11Texture2D> chromaTarget;
        ComPtr<ID3D11RenderTargetView> lumaRtv;
        ComPtr<ID3D11RenderTargetView> chromaRtv;
        GLuint glLuma = 0;
        GLuint glChroma = 0;
        HANDLE objects[2] = {nullptr, nullptr};
        quint64 lastUse = 0;
    };

    bool apiResolved = false;
    bool apiOk = false;
    QString apiWhy;
    WglDxApi api;
    ComPtr<ID3DBlob> vsBlob;
    ComPtr<ID3DBlob> psLumaBlob;
    ComPtr<ID3DBlob> psChromaBlob;
    std::vector<std::unique_ptr<Device>> devices;
    Device *locked = nullptr;
    quint64 clock = 0;

    bool resolveApi();
    bool compileShaders(QString *why);
    Device *deviceFor(QOpenGLExtraFunctions *gl, ID3D11Device *device, QString *why);
    bool ensureSurfaces(QOpenGLExtraFunctions *gl, Device &dev, int surfaceW, int surfaceH, int width,
                        int height, QString *why);
    void releaseSurfaces(QOpenGLExtraFunctions *gl, Device &dev);
    void releaseDevice(QOpenGLExtraFunctions *gl, Device &dev);
};

bool D3d11GlInterop::Impl::resolveApi()
{
    if (apiResolved)
        return apiOk;
    apiResolved = true;

    QOpenGLContext *ctx = QOpenGLContext::currentContext();
    if (!ctx || ctx->isOpenGLES()) {
        apiWhy = QStringLiteral("no desktop OpenGL context is current");
        return false;
    }
    // WGL extension entry points come from wglGetProcAddress, which is what Qt's getProcAddress
    // asks on Windows. A driver without the extension returns null for them, but the extension
    // string is the documented test, so both are checked.
    const auto resolve = [ctx](const char *name) { return ctx->getProcAddress(name); };
    api.getExtensionsStringARB = reinterpret_cast<decltype(api.getExtensionsStringARB)>(
        resolve("wglGetExtensionsStringARB"));
    const HDC dc = wglGetCurrentDC();
    const char *extensions =
        (api.getExtensionsStringARB && dc) ? api.getExtensionsStringARB(dc) : nullptr;
    if (!extensions || !strstr(extensions, "WGL_NV_DX_interop2")) {
        apiWhy = QStringLiteral("the OpenGL driver does not offer WGL_NV_DX_interop2");
        return false;
    }

    api.openDevice = reinterpret_cast<decltype(api.openDevice)>(resolve("wglDXOpenDeviceNV"));
    api.closeDevice = reinterpret_cast<decltype(api.closeDevice)>(resolve("wglDXCloseDeviceNV"));
    api.registerObject =
        reinterpret_cast<decltype(api.registerObject)>(resolve("wglDXRegisterObjectNV"));
    api.unregisterObject =
        reinterpret_cast<decltype(api.unregisterObject)>(resolve("wglDXUnregisterObjectNV"));
    api.lockObjects = reinterpret_cast<decltype(api.lockObjects)>(resolve("wglDXLockObjectsNV"));
    api.unlockObjects =
        reinterpret_cast<decltype(api.unlockObjects)>(resolve("wglDXUnlockObjectsNV"));
    if (!api.openDevice || !api.closeDevice || !api.registerObject || !api.unregisterObject
        || !api.lockObjects || !api.unlockObjects) {
        apiWhy = QStringLiteral("WGL_NV_DX_interop2 is advertised but its entry points are missing");
        return false;
    }
    apiOk = true;
    return true;
}

bool D3d11GlInterop::Impl::compileShaders(QString *why)
{
    const auto compile = [&](const char *entry, const char *target, ComPtr<ID3DBlob> &out) {
        ComPtr<ID3DBlob> errors;
        const HRESULT hr = D3DCompile(kPlaneShader, strlen(kPlaneShader), "drift-d3d11-planes",
                                      nullptr, nullptr, entry, target,
                                      D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &out, &errors);
        if (SUCCEEDED(hr))
            return true;
        const QString detail = errors
            ? QString::fromUtf8(static_cast<const char *>(errors->GetBufferPointer()),
                                int(errors->GetBufferSize()))
                  .trimmed()
            : hresultString(hr);
        *why = QStringLiteral("D3DCompile(%1) failed: %2").arg(QLatin1String(entry), detail);
        return false;
    };
    // Shader model 4: every device the D3D11VA decoder can run on is feature level 10 or better,
    // and SV_VertexID needs no more.
    return compile("vs_main", "vs_4_0", vsBlob) && compile("ps_luma", "ps_4_0", psLumaBlob)
        && compile("ps_chroma", "ps_4_0", psChromaBlob);
}

D3d11GlInterop::Impl::Device *D3d11GlInterop::Impl::deviceFor(QOpenGLExtraFunctions *gl,
                                                              ID3D11Device *device, QString *why)
{
    for (const auto &dev : devices) {
        // Holding a reference keeps a released decoder device alive, so its address cannot be
        // handed to a new device while this entry still names it.
        if (dev->device.Get() == device)
            return dev.get();
    }

    if (device->GetFeatureLevel() < D3D_FEATURE_LEVEL_10_0) {
        *why = QStringLiteral("the decoder's D3D11 device is below feature level 10.0");
        return nullptr;
    }
    if (!vsBlob && !compileShaders(why))
        return nullptr;

    if (devices.size() >= kMaxDevices) {
        auto oldest = devices.end();
        for (auto it = devices.begin(); it != devices.end(); ++it) {
            if (it->get() != locked && (oldest == devices.end() || (*it)->lastUse < (*oldest)->lastUse))
                oldest = it;
        }
        if (oldest != devices.end()) {
            releaseDevice(gl, **oldest);
            devices.erase(oldest);
        }
    }

    auto dev = std::make_unique<Device>();
    dev->device = device;
    dev->glDevice = api.openDevice(device);
    if (!dev->glDevice) {
        // The interop device has to be on the adapter the GL context renders on. HwAccel opens
        // D3D11VA there, so this failing means the two still ended up apart.
        *why = QStringLiteral("wglDXOpenDeviceNV refused the decoder's D3D11 device (error %1); "
                              "it is probably on a different GPU than OpenGL")
                   .arg(GetLastError());
        return nullptr;
    }

    HRESULT hr = device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(),
                                            nullptr, &dev->vs);
    if (SUCCEEDED(hr))
        hr = device->CreatePixelShader(psLumaBlob->GetBufferPointer(), psLumaBlob->GetBufferSize(),
                                       nullptr, &dev->psLuma);
    if (SUCCEEDED(hr))
        hr = device->CreatePixelShader(psChromaBlob->GetBufferPointer(),
                                       psChromaBlob->GetBufferSize(), nullptr, &dev->psChroma);
    if (SUCCEEDED(hr)) {
        CD3D11_SAMPLER_DESC samplerDesc(D3D11_DEFAULT);
        samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
        hr = device->CreateSamplerState(&samplerDesc, &dev->sampler);
    }
    if (SUCCEEDED(hr)) {
        const CD3D11_BUFFER_DESC cropDesc(16, D3D11_BIND_CONSTANT_BUFFER);
        hr = device->CreateBuffer(&cropDesc, nullptr, &dev->crop);
    }
    if (FAILED(hr)) {
        *why = QStringLiteral("creating the D3D11 plane-split pipeline failed (%1)").arg(hresultString(hr));
        releaseDevice(gl, *dev);
        return nullptr;
    }

    devices.push_back(std::move(dev));
    return devices.back().get();
}

bool D3d11GlInterop::Impl::ensureSurfaces(QOpenGLExtraFunctions *gl, Device &dev, int surfaceW,
                                          int surfaceH, int width, int height, QString *why)
{
    if (dev.nv12 && dev.surfaceW == surfaceW && dev.surfaceH == surfaceH && dev.width == width
        && dev.height == height)
        return true;

    releaseSurfaces(gl, dev);
    ID3D11Device *device = dev.device.Get();

    const auto fail = [&](const char *what, HRESULT hr) {
        *why = QStringLiteral("%1 failed (%2)").arg(QLatin1String(what), hresultString(hr));
        return false;
    };

    const CD3D11_TEXTURE2D_DESC nv12Desc(DXGI_FORMAT_NV12, UINT(surfaceW), UINT(surfaceH), 1, 1,
                                         D3D11_BIND_SHADER_RESOURCE);
    HRESULT hr = device->CreateTexture2D(&nv12Desc, nullptr, &dev.nv12);
    if (FAILED(hr))
        return fail("creating the NV12 copy texture", hr);

    const CD3D11_SHADER_RESOURCE_VIEW_DESC lumaDesc(D3D11_SRV_DIMENSION_TEXTURE2D,
                                                    DXGI_FORMAT_R8_UNORM, 0, 1);
    hr = device->CreateShaderResourceView(dev.nv12.Get(), &lumaDesc, &dev.lumaView);
    if (FAILED(hr))
        return fail("creating the luma view", hr);
    const CD3D11_SHADER_RESOURCE_VIEW_DESC chromaDesc(D3D11_SRV_DIMENSION_TEXTURE2D,
                                                      DXGI_FORMAT_R8G8_UNORM, 0, 1);
    hr = device->CreateShaderResourceView(dev.nv12.Get(), &chromaDesc, &dev.chromaView);
    if (FAILED(hr))
        return fail("creating the chroma view", hr);

    const CD3D11_TEXTURE2D_DESC lumaTargetDesc(DXGI_FORMAT_R8G8B8A8_UNORM, UINT(width),
                                               UINT(height), 1, 1, D3D11_BIND_RENDER_TARGET);
    hr = device->CreateTexture2D(&lumaTargetDesc, nullptr, &dev.lumaTarget);
    if (SUCCEEDED(hr))
        hr = device->CreateRenderTargetView(dev.lumaTarget.Get(), nullptr, &dev.lumaRtv);
    if (FAILED(hr))
        return fail("creating the luma target", hr);
    const CD3D11_TEXTURE2D_DESC chromaTargetDesc(DXGI_FORMAT_R8G8B8A8_UNORM, UINT(width / 2),
                                                 UINT(height / 2), 1, 1, D3D11_BIND_RENDER_TARGET);
    hr = device->CreateTexture2D(&chromaTargetDesc, nullptr, &dev.chromaTarget);
    if (SUCCEEDED(hr))
        hr = device->CreateRenderTargetView(dev.chromaTarget.Get(), nullptr, &dev.chromaRtv);
    if (FAILED(hr))
        return fail("creating the chroma target", hr);

    const ID3D11Texture2D *targets[2] = {dev.lumaTarget.Get(), dev.chromaTarget.Get()};
    GLuint *names[2] = {&dev.glLuma, &dev.glChroma};
    for (int i = 0; i < 2; ++i) {
        gl->glGenTextures(1, names[i]);
        dev.objects[i] = api.registerObject(dev.glDevice, const_cast<ID3D11Texture2D *>(targets[i]),
                                            *names[i], GL_TEXTURE_2D, kWglAccessReadOnly);
        if (!dev.objects[i]) {
            *why = QStringLiteral("wglDXRegisterObjectNV failed (error %1)").arg(GetLastError());
            return false;
        }
    }

    dev.surfaceW = surfaceW;
    dev.surfaceH = surfaceH;
    dev.width = width;
    dev.height = height;
    return true;
}

void D3d11GlInterop::Impl::releaseSurfaces(QOpenGLExtraFunctions *gl, Device &dev)
{
    for (HANDLE &object : dev.objects) {
        if (object && dev.glDevice)
            api.unregisterObject(dev.glDevice, object);
        object = nullptr;
    }
    if (gl) {
        for (GLuint *name : {&dev.glLuma, &dev.glChroma}) {
            if (*name)
                gl->glDeleteTextures(1, name);
            *name = 0;
        }
    }
    dev.lumaRtv.Reset();
    dev.chromaRtv.Reset();
    dev.lumaTarget.Reset();
    dev.chromaTarget.Reset();
    dev.lumaView.Reset();
    dev.chromaView.Reset();
    dev.nv12.Reset();
    dev.surfaceW = dev.surfaceH = dev.width = dev.height = 0;
}

void D3d11GlInterop::Impl::releaseDevice(QOpenGLExtraFunctions *gl, Device &dev)
{
    releaseSurfaces(gl, dev);
    if (dev.glDevice)
        api.closeDevice(dev.glDevice);
    dev.glDevice = nullptr;
    dev.vs.Reset();
    dev.psLuma.Reset();
    dev.psChroma.Reset();
    dev.sampler.Reset();
    dev.crop.Reset();
    dev.device.Reset();
}

D3d11GlInterop::D3d11GlInterop()
    : d(std::make_unique<Impl>())
{
}

// Interop handles need the GL context to be torn down properly, which a destructor cannot count
// on; GlRuntime calls release() from its GL thread first. Anything still held here is only the
// COM references, which are safe to drop anywhere.
D3d11GlInterop::~D3d11GlInterop() = default;

D3d11GlInterop::Result D3d11GlInterop::lock(QOpenGLExtraFunctions *gl, const AVFrame *frame,
                                            GLuint *texY, GLuint *texUV, QString *why)
{
    // A caller that returned without unlocking would leave D3D11 unable to render into the targets.
    unlock();

    if (!d->resolveApi()) {
        *why = d->apiWhy;
        return Result::Failed;
    }
    if (!gl || !frame || !texY || !texUV || !frame->hw_frames_ctx || !frame->data[0])
        return Result::Declined;

    const auto *frames = reinterpret_cast<const AVHWFramesContext *>(frame->hw_frames_ctx->data);
    if (!frames || !frames->device_ctx || frames->device_ctx->type != AV_HWDEVICE_TYPE_D3D11VA
        || !frames->device_ctx->hwctx)
        return Result::Declined;
    // Per frame, not sticky: a 10-bit clip must not turn the path off for 8-bit clips beside it.
    if (frames->sw_format != AV_PIX_FMT_NV12) {
        const char *name = av_get_pix_fmt_name(frames->sw_format);
        *why = QStringLiteral("%1 surfaces are not imported, only NV12")
                   .arg(QString::fromLatin1(name ? name : "unknown"));
        return Result::Declined;
    }

    auto *hw = static_cast<AVD3D11VADeviceContext *>(frames->device_ctx->hwctx);
    const int width = frame->width & ~1;
    const int height = frame->height & ~1;
    if (width < 2 || height < 2 || !hw->device || !hw->device_context)
        return Result::Declined;

    auto *surface = reinterpret_cast<ID3D11Texture2D *>(frame->data[0]);
    const UINT slice = UINT(intptr_t(frame->data[1]));
    D3D11_TEXTURE2D_DESC surfaceDesc{};
    surface->GetDesc(&surfaceDesc);
    if (surfaceDesc.Format != DXGI_FORMAT_NV12 || int(surfaceDesc.Width) < width
        || int(surfaceDesc.Height) < height || slice >= surfaceDesc.ArraySize
        || surfaceDesc.MipLevels != 1) {
        *why = QStringLiteral("unexpected D3D11 decoder surface layout");
        return Result::Declined;
    }

    Impl::Device *dev = d->deviceFor(gl, hw->device, why);
    if (!dev)
        return Result::Failed;
    dev->lastUse = ++d->clock;

    {
        const AvD3d11Lock lock(hw);
        if (!d->ensureSurfaces(gl, *dev, int(surfaceDesc.Width), int(surfaceDesc.Height), width,
                               height, why)) {
            d->releaseSurfaces(gl, *dev);
            return Result::Failed;
        }

        ID3D11DeviceContext *ctx = hw->device_context;
        // With mip levels at 1, a slice's subresource index is the slice itself.
        ctx->CopySubresourceRegion(dev->nv12.Get(), 0, 0, 0, 0, surface, slice, nullptr);

        const float crop[4] = {float(width) / float(surfaceDesc.Width),
                               float(height) / float(surfaceDesc.Height), 0.f, 0.f};
        ctx->UpdateSubresource(dev->crop.Get(), 0, nullptr, crop, 0, 0);

        ctx->IASetInputLayout(nullptr);
        ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ctx->RSSetState(nullptr);
        ctx->OMSetBlendState(nullptr, nullptr, 0xffffffff);
        ctx->VSSetShader(dev->vs.Get(), nullptr, 0);
        ctx->PSSetConstantBuffers(0, 1, dev->crop.GetAddressOf());
        ctx->PSSetSamplers(0, 1, dev->sampler.GetAddressOf());

        const auto drawPlane = [ctx](ID3D11RenderTargetView *rtv, ID3D11PixelShader *ps,
                                     ID3D11ShaderResourceView *srv, int w, int h) {
            const D3D11_VIEWPORT viewport{0.f, 0.f, FLOAT(w), FLOAT(h), 0.f, 1.f};
            ctx->OMSetRenderTargets(1, &rtv, nullptr);
            ctx->RSSetViewports(1, &viewport);
            ctx->PSSetShader(ps, nullptr, 0);
            ctx->PSSetShaderResources(0, 1, &srv);
            ctx->Draw(3, 0);
            ID3D11ShaderResourceView *none = nullptr;
            ctx->PSSetShaderResources(0, 1, &none);
        };
        drawPlane(dev->lumaRtv.Get(), dev->psLuma.Get(), dev->lumaView.Get(), width, height);
        drawPlane(dev->chromaRtv.Get(), dev->psChroma.Get(), dev->chromaView.Get(), width / 2,
                  height / 2);
        ctx->OMSetRenderTargets(0, nullptr, nullptr);
    }

    // The lock is the synchronisation point: it waits for the D3D11 draws above to land before GL
    // may sample the textures.
    if (!d->api.lockObjects(dev->glDevice, 2, dev->objects)) {
        *why = QStringLiteral("wglDXLockObjectsNV failed (error %1)").arg(GetLastError());
        return Result::Failed;
    }
    d->locked = dev;

    for (GLuint name : {dev->glLuma, dev->glChroma}) {
        gl->glBindTexture(GL_TEXTURE_2D, name);
        gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    *texY = dev->glLuma;
    *texUV = dev->glChroma;
    return Result::Locked;
}

void D3d11GlInterop::unlock()
{
    if (!d->locked)
        return;
    d->api.unlockObjects(d->locked->glDevice, 2, d->locked->objects);
    d->locked = nullptr;
}

void D3d11GlInterop::release(QOpenGLExtraFunctions *gl)
{
    unlock();
    for (const auto &dev : d->devices)
        d->releaseDevice(gl, *dev);
    d->devices.clear();
}

} // namespace drift::gl
