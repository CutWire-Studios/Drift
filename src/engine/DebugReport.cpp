#include "DebugReport.h"

#include "ClipReader.h"
#include "Exporter.h"
#include "GlRuntime.h"
#include "GpuCompositor.h"
#include "GpuDevice.h"
#include "GpuPreference.h"
#include "HwAccel.h"
#include "OrtRuntime.h"
#include "VaapiZeroCopy.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QLocale>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QRegularExpression>
#include <QSet>
#include <QSurfaceFormat>
#include <QSysInfo>
#include <QThread>
#include <QVariantList>
#include <utility>

#ifndef DRIFT_VERSION
#define DRIFT_VERSION "0.0.0"
#endif

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavcodec/codec.h>
#include <libavutil/avutil.h>
#include <libavutil/hwcontext.h>
}

namespace {

QString trReport(const char *text)
{
    return QCoreApplication::translate("DebugReport", text);
}

QString readKeyValueFile(const QString &path, const QString &key)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};

    while (!file.atEnd()) {
        const QByteArray line = file.readLine().trimmed();
        if (line.startsWith('#') || !line.contains('='))
            continue;
        const int eq = line.indexOf('=');
        if (QString::fromUtf8(line.left(eq)) != key)
            continue;
        QByteArray value = line.mid(eq + 1);
        if (value.size() >= 2 && value.startsWith('"') && value.endsWith('"'))
            value = value.mid(1, value.size() - 2);
        return QString::fromUtf8(value);
    }
    return {};
}

QString osReleasePretty(const QString &path)
{
    const QString pretty = readKeyValueFile(path, QStringLiteral("PRETTY_NAME"));
    if (!pretty.isEmpty())
        return pretty;
    const QString name = readKeyValueFile(path, QStringLiteral("NAME"));
    const QString version = readKeyValueFile(path, QStringLiteral("VERSION_ID"));
    if (name.isEmpty())
        return {};
    return version.isEmpty() ? name : QStringLiteral("%1 %2").arg(name, version);
}

QString cpuModel()
{
#if defined(Q_OS_LINUX) || defined(Q_OS_ANDROID)
    // /proc/cpuinfo on Android rarely carries "model name" (that field is x86-only in
    // practice); "Hardware" is the SoC/board name most ARM kernels report instead, so it
    // is kept as a fallback rather than dropping straight to the raw architecture string.
    QFile cpu(QStringLiteral("/proc/cpuinfo"));
    if (cpu.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QString hardware;
        while (!cpu.atEnd()) {
            const QByteArray line = cpu.readLine();
            const int colon = line.indexOf(':');
            if (colon < 0)
                continue;
            const QByteArray key = line.left(colon).trimmed();
            const QString value = QString::fromUtf8(line.mid(colon + 1).trimmed());
            if (key == "model name" && !value.isEmpty())
                return value;
            if (key == "Hardware" && hardware.isEmpty())
                hardware = value;
        }
        if (!hardware.isEmpty())
            return hardware;
    }
#endif
    return QSysInfo::currentCpuArchitecture();
}

QString packageKind()
{
#if defined(Q_OS_WIN)
    return QStringLiteral("Windows");
#elif defined(Q_OS_MACOS)
    return QStringLiteral("macOS");
#elif defined(Q_OS_ANDROID)
    return QStringLiteral("Android");
#else
    if (qEnvironmentVariableIsSet("FLATPAK_ID") || QFile::exists(QStringLiteral("/.flatpak-info")))
        return QStringLiteral("Flatpak");
    if (qEnvironmentVariableIsSet("APPIMAGE"))
        return QStringLiteral("AppImage");
    if (qEnvironmentVariableIsSet("SNAP"))
        return QStringLiteral("Snap");
    return QStringLiteral("Linux");
#endif
}

// Flatpak bind-mounts the host file at /run/host/os-release (no extra
// filesystem permission). /run/host/etc/os-release is systemd-nspawn's
// layout. /etc/os-release inside the sandbox is the KDE/Freedesktop runtime.
QString hostOsReleasePath()
{
    static const QString kCandidates[] = {
        QStringLiteral("/run/host/os-release"),
        QStringLiteral("/run/host/etc/os-release"),
        QStringLiteral("/etc/os-release"),
    };
    for (const QString &path : kCandidates) {
        if (QFile::exists(path))
            return path;
    }
    return {};
}

QString osPretty()
{
#if defined(Q_OS_LINUX)
    if (const QString pretty = osReleasePretty(hostOsReleasePath()); !pretty.isEmpty())
        return pretty;
#endif
    return QSysInfo::prettyProductName();
}

QString pciIdString(const drift::gpu::Adapter &gpu)
{
    if (gpu.vendorId == 0 && gpu.deviceId == 0)
        return {};
    return QStringLiteral("%1:%2")
        .arg(gpu.vendorId, 4, 16, QLatin1Char('0'))
        .arg(gpu.deviceId, 4, 16, QLatin1Char('0'))
        .toUpper();
}

QString formatGpu(const drift::gpu::Adapter &gpu)
{
    QString head = gpu.name;
    if (head.isEmpty() && !gpu.vendor.isEmpty())
        head = QStringLiteral("%1 Graphics").arg(gpu.vendor);
    if (head.isEmpty())
        head = trReport("Unknown GPU");

    QStringList bits;
    if (!gpu.driver.isEmpty())
        bits.append(gpu.driver);
    if (const QString pci = pciIdString(gpu); !pci.isEmpty())
        bits.append(pci);
    if (bits.isEmpty())
        return head;
    return QStringLiteral("%1 (%2)").arg(head, bits.join(QStringLiteral(", ")));
}

struct OpenGlInfo
{
    QString renderer; // "AMD Radeon RX 6600 (ATI Technologies Inc.)"
    int major = 0;
    int minor = 0;
    bool isEs = false;
    bool core = false;
    bool software = false;
    bool valid = false;
};

// What OpenGL this process actually got. The version matters as much as the
// renderer — a report that said only "AMD Radeon RX 6600" could not distinguish a
// healthy machine from one Qt had quietly put on a 3.0 software rasterizer — so
// prefer the scene graph's own context, which is the format GlRuntime inherits,
// and fall back to a probe context that does not insist on 3.3 so a machine below
// the floor still reports what it has instead of nothing at all.
OpenGlInfo openglInfo()
{
    OpenGlInfo info;
    if (!qobject_cast<QGuiApplication *>(QCoreApplication::instance()))
        return info;

    QSurfaceFormat format = QSurfaceFormat::defaultFormat();
    QOpenGLContext *shared = QOpenGLContext::globalShareContext();
    if (shared && shared->isValid())
        format = shared->format();

    QOffscreenSurface surface;
    surface.setFormat(format);
    surface.create();
    if (!surface.isValid())
        return info;

    QOpenGLContext ctx;
    ctx.setFormat(surface.format());
    if (shared && shared->isValid())
        ctx.setShareContext(shared);
    if (!ctx.create()) {
        // The requested version is the thing that failed; ask for whatever exists.
        ctx.setFormat(QSurfaceFormat());
        if (!ctx.create())
            return info;
    }
    if (!ctx.makeCurrent(&surface))
        return info;

    if (QOpenGLFunctions *fn = ctx.functions()) {
        const char *glRenderer = reinterpret_cast<const char *>(fn->glGetString(GL_RENDERER));
        const char *glVendor = reinterpret_cast<const char *>(fn->glGetString(GL_VENDOR));
        if (glRenderer)
            info.renderer = QString::fromUtf8(glRenderer);
        info.software = drift::gl::isSoftwareRenderer(info.renderer);
        if (glVendor) {
            const QString vendor = QString::fromUtf8(glVendor);
            if (!vendor.isEmpty() && !info.renderer.contains(vendor, Qt::CaseInsensitive)) {
                info.renderer = info.renderer.isEmpty()
                    ? vendor
                    : QStringLiteral("%1 (%2)").arg(info.renderer, vendor);
            }
        }
    }
    info.major = ctx.format().majorVersion();
    info.minor = ctx.format().minorVersion();
    info.isEs = ctx.isOpenGLES();
    info.core = ctx.format().profile() == QSurfaceFormat::CoreProfile;
    info.valid = true;
    ctx.doneCurrent();
    return info;
}

QString openglLabel(const OpenGlInfo &info)
{
    if (!info.valid)
        return {};
    QString version;
    if (info.major > 0) {
        version = (info.isEs ? QStringLiteral("OpenGL ES %1.%2") : QStringLiteral("OpenGL %1.%2"))
                      .arg(info.major)
                      .arg(info.minor);
        if (info.core)
            version += QStringLiteral(" core");
    }
    if (info.renderer.isEmpty())
        return version;
    if (version.isEmpty())
        return info.renderer;
    return QStringLiteral("%1 — %2").arg(info.renderer, version);
}

// Why the preview can or cannot draw. Forces a bring-up attempt so the row is
// meaningful even when the user opens this before touching the timeline.
QString gpuCompositorLabel()
{
    if (!qobject_cast<QGuiApplication *>(QCoreApplication::instance()))
        return trReport("Not available");

    GpuCompositor::isAvailable();
    const drift::gl::GlStatusInfo info = GpuCompositor::status();
    switch (info.status) {
    case drift::gl::GlStatus::Ready:
        return trReport("Ready");
    case drift::gl::GlStatus::VersionTooLow:
        return trReport("OpenGL %1.%2 is below the 3.3 minimum")
            .arg(info.major)
            .arg(info.minor);
    case drift::gl::GlStatus::NoShareContext:
        return trReport("No shared OpenGL context");
    case drift::gl::GlStatus::SurfaceFailed:
        return trReport("Offscreen surface creation failed");
    case drift::gl::GlStatus::ContextFailed:
        return trReport("OpenGL context creation failed");
    case drift::gl::GlStatus::MakeCurrentFailed:
        return trReport("Could not make the OpenGL context current");
    case drift::gl::GlStatus::NoFunctions:
        return trReport("OpenGL functions unavailable");
    case drift::gl::GlStatus::ShaderLinkFailed:
        return trReport("Shader compilation failed");
    case drift::gl::GlStatus::NoApplication:
    case drift::gl::GlStatus::NotAttempted:
        break;
    }
    return trReport("Unknown");
}

// The backend the preview decoder would land on here: the first one this platform
// offers whose device actually opens. ClipReader walks the same list per clip.
drift::hwaccel::Backend activeDecodeBackend()
{
    // availableDecodeBackends() rather than a deviceAvailable() walk: MediaCodec needs a
    // different probe (its device init succeeds with no decoder present), and that function is
    // where that lives. It also keeps this in step with what the preview picker offers.
    const QList<drift::hwaccel::Backend> available = drift::hwaccel::availableDecodeBackends();
    return available.isEmpty() ? drift::hwaccel::Backend::None : available.first();
}

const AVCodec *findNamedEncoder(const char *const *names)
{
    for (int i = 0; names && names[i]; ++i) {
        if (const AVCodec *codec = avcodec_find_encoder_by_name(names[i]))
            return codec;
    }
    return nullptr;
}

QString decodeModeLabel()
{
    switch (ClipReader::hardwareDecodeMode()) {
    case ClipReader::HardwareDecodeMode::Software:
        return trReport("Software");
    case ClipReader::HardwareDecodeMode::Hardware: {
        const drift::hwaccel::Backend pinned = ClipReader::pinnedDecodeBackend();
        if (pinned == drift::hwaccel::Backend::None)
            return trReport("Hardware");
        return QStringLiteral("%1 (%2)")
            .arg(trReport("Hardware"), QString::fromLatin1(drift::hwaccel::name(pinned)));
    }
    case ClipReader::HardwareDecodeMode::Auto:
        break;
    }
#if defined(Q_OS_ANDROID)
    // Auto on Android means the MediaCodec heuristic, not the HwAccel backend probe — naming it
    // is what distinguishes "no hardware path exists here" from "the heuristic declined".
    if (drift::hwaccel::mediaCodecDecodeAvailable())
        return trReport("Auto (MediaCodec)");
#endif
    return trReport("Auto");
}

// What the preview actually ended up doing, which is the half of the story the mode
// alone does not tell: a pinned backend that silently fell back looks identical above.
QString activeDecodeLabel()
{
    const std::optional<drift::hwaccel::Backend> active = ClipReader::activeDecodeBackend();
    if (!active)
        return trReport("Nothing decoded yet");

    const QString base = *active == drift::hwaccel::Backend::None
        ? trReport("Software")
        : QString::fromLatin1(drift::hwaccel::name(*active));
    if (ClipReader::hardwareFallbackCount() == 0)
        return base;
    const QString failed = trReport("hardware decoding failed");
    const QString why = ClipReader::lastHardwareFailure();
    return why.isEmpty() ? QStringLiteral("%1 — %2").arg(base, failed)
                         : QStringLiteral("%1 — %2: %3").arg(base, failed, why);
}

QVariantMap systemRow(const QString &label, const QString &value)
{
    return {{QStringLiteral("label"), label}, {QStringLiteral("value"), value}};
}

QString supportLabel(bool ok)
{
    return ok ? trReport("Supported") : trReport("Not supported");
}

bool flatpakExtensionMounted(const QString &subdir)
{
    static const char *const kTriplets[] = {"x86_64-linux-gnu", "aarch64-linux-gnu", "i386-linux-gnu"};
    for (const char *triplet : kTriplets) {
        if (QFile::exists(QStringLiteral("/usr/lib/%1/%2").arg(QLatin1String(triplet), subdir)))
            return true;
    }
    return false;
}

bool gpuIsNvidia(const drift::gpu::Adapter &gpu)
{
    return gpu.vendorId == 0x10de || gpu.driver == QLatin1String("nvidia")
           || gpu.vendor.compare(QLatin1String("NVIDIA"), Qt::CaseInsensitive) == 0;
}

bool gpuIsAmd(const drift::gpu::Adapter &gpu)
{
    return gpu.vendorId == 0x1002 || gpu.vendorId == 0x1022
           || gpu.driver == QLatin1String("amdgpu") || gpu.driver == QLatin1String("radeon")
           || gpu.vendor.compare(QLatin1String("AMD"), Qt::CaseInsensitive) == 0;
}

QString previewUploadLabel()
{
    using Path = drift::gl::GlRuntime::PreviewUploadPath;
    switch (drift::gl::GlRuntime::lastPreviewUploadPath()) {
    case Path::CudaInterop:
        return QStringLiteral("CUDA interop");
    case Path::VaapiDmaBuf:
        return QStringLiteral("VAAPI dma-buf");
    case Path::MediaCodecImage:
        return QStringLiteral("MediaCodec image");
    case Path::D3d11Interop:
        return QStringLiteral("D3D11 interop");
    case Path::CpuRoundTrip:
        return QStringLiteral("CPU round-trip");
    case Path::None:
        break;
    }
    return trReport("Nothing uploaded yet");
}

QString zeroCopyLabel()
{
    // What actually happened to the last frame, not what the setting allows: this row used to
    // read "Active" whenever VAAPI was not switched off, including on Windows, where nothing had
    // been imported at all.
    using Path = drift::gl::GlRuntime::PreviewUploadPath;
    switch (drift::gl::GlRuntime::lastPreviewUploadPath()) {
    case Path::CudaInterop:
    case Path::VaapiDmaBuf:
    case Path::MediaCodecImage:
    case Path::D3d11Interop:
        return QStringLiteral("%1 (%2)").arg(trReport("Active"), previewUploadLabel());
    case Path::CpuRoundTrip:
    case Path::None:
        break;
    }
    const QString reason = drift::gl::GlRuntime::lastZeroCopyDeclineReason();
    if (!reason.isEmpty())
        return reason;
#if defined(Q_OS_WIN)
    if (!drift::d3d11ZeroCopyEnabled())
        return trReport("Off");
#else
    if (drift::vaapiZeroCopyMode() == drift::VaapiZeroCopyMode::Off)
        return trReport("Off");
#endif
    return trReport("Not engaged");
}

QVariantMap hintRow(const QString &id, const QString &title, const QString &detail,
                    const QString &command = {}, const QString &action = {})
{
    QVariantMap m{{QStringLiteral("id"), id},
                  {QStringLiteral("title"), title},
                  {QStringLiteral("detail"), detail}};
    if (!command.isEmpty())
        m.insert(QStringLiteral("command"), command);
    if (!action.isEmpty())
        m.insert(QStringLiteral("action"), action);
    return m;
}

} // namespace

QVariantMap DebugReport::collect()
{
    QVariantMap info;
    const QString package = packageKind();
    const drift::hwaccel::Backend backend = activeDecodeBackend();
    const AVHWDeviceType backendType = drift::hwaccel::deviceType(backend);
    // MediaCodec is in the backend list now, so `backend` names it directly on Android. Kept as
    // its own flag because the per-codec probe below still has to ask for the *_mediacodec
    // decoder by name — those are not reachable through a hardware device context.
    const bool mediaCodecOk = backend == drift::hwaccel::Backend::MediaCodec;
    const bool hwDecodeOk = backend != drift::hwaccel::Backend::None;

    struct CodecSpec {
        const char *name;
        AVCodecID id;
    };
    static const CodecSpec kCodecs[] = {
        {"H264", AV_CODEC_ID_H264},
        {"VP9", AV_CODEC_ID_VP9},
        {"VP8", AV_CODEC_ID_VP8},
        {"AV1", AV_CODEC_ID_AV1},
        {"HEVC", AV_CODEC_ID_HEVC},
    };

    QVariantList codecs;
    for (const CodecSpec &spec : kCodecs) {
        const AVCodec *software = avcodec_find_decoder(spec.id);
        const AVCodec *hardware = drift::hwaccel::findDecoder(spec.id, backendType, nullptr);
        if (!hardware && mediaCodecOk)
            hardware = drift::hwaccel::findMediaCodecDecoder(spec.id);
        QVariantMap row;
        row.insert(QStringLiteral("name"), QString::fromLatin1(spec.name));
        row.insert(QStringLiteral("software"), software != nullptr);
        row.insert(QStringLiteral("hardware"), hwDecodeOk && hardware != nullptr);
        row.insert(QStringLiteral("softwareDecoder"),
                   software ? QString::fromUtf8(software->name) : QString());
        row.insert(QStringLiteral("hardwareDecoder"),
                   hardware ? QString::fromUtf8(hardware->name) : QString());
        codecs.append(row);
    }

    // Software names match Exporter's catalog. Hardware is the first available
    // NVENC/QSV/AMF/VAAPI/VideoToolbox encoder for that family, if any.
    static const char *const kH264Enc[] = {"libx264", "h264", nullptr};
    static const char *const kVp9Enc[] = {"libvpx-vp9", nullptr};
    static const char *const kVp8Enc[] = {"libvpx", nullptr};
    static const char *const kAv1Enc[] = {"libsvtav1", nullptr};
    static const char *const kHevcEnc[] = {"libx265", "hevc", nullptr};
    struct EncoderSpec {
        const char *name;
        const char *const *softwareNames;
        const char *hwIdPrefix;
    };
    static const EncoderSpec kEncoders[] = {
        {"H264", kH264Enc, "h264"},
        {"VP9", kVp9Enc, "vp9"},
        {"VP8", kVp8Enc, "vp8"},
        {"AV1", kAv1Enc, "av1"},
        {"HEVC", kHevcEnc, "h265"},
    };

    const QVariantList exportCodecs = Exporter::videoCodecs();
    auto firstHwEncoder = [&exportCodecs](const char *prefix) -> QVariantMap {
        const QString pre = QString::fromLatin1(prefix);
        for (const QVariant &v : exportCodecs) {
            const QVariantMap m = v.toMap();
            if (!m.value(QStringLiteral("hardware")).toBool())
                continue;
            if (!m.value(QStringLiteral("id")).toString().startsWith(pre))
                continue;
            if (m.value(QStringLiteral("available")).toBool())
                return m;
        }
        return {};
    };

    QVariantList encoders;
    for (const EncoderSpec &spec : kEncoders) {
        const AVCodec *software = findNamedEncoder(spec.softwareNames);
        const QVariantMap hw = firstHwEncoder(spec.hwIdPrefix);
        QVariantMap row;
        row.insert(QStringLiteral("name"), QString::fromLatin1(spec.name));
        row.insert(QStringLiteral("software"), software != nullptr);
        row.insert(QStringLiteral("hardware"), !hw.isEmpty());
        row.insert(QStringLiteral("softwareEncoder"),
                   software ? QString::fromUtf8(software->name) : QString());
        row.insert(QStringLiteral("hardwareEncoder"),
                   hw.value(QStringLiteral("encoderName")).toString());
        encoders.append(row);
    }

    QVariantList system;
    system.append(systemRow(trReport("Drift"), QStringLiteral(DRIFT_VERSION)));
    system.append(systemRow(trReport("Package"), package));
    if (const QString flatpakId = qEnvironmentVariable("FLATPAK_ID"); !flatpakId.isEmpty())
        system.append(systemRow(trReport("Flatpak ID"), flatpakId));
    if (const QString runtime = readKeyValueFile(QStringLiteral("/.flatpak-info"), QStringLiteral("runtime"));
        !runtime.isEmpty())
        system.append(systemRow(trReport("Flatpak runtime"), runtime));
    if (const QString appImage = qEnvironmentVariable("APPIMAGE"); !appImage.isEmpty())
        system.append(systemRow(trReport("AppImage"), appImage));
    system.append(systemRow(trReport("OS"), osPretty()));
#if defined(Q_OS_LINUX)
    {
        const QString versionId = readKeyValueFile(hostOsReleasePath(), QStringLiteral("VERSION_ID"));
        if (!versionId.isEmpty())
            system.append(systemRow(trReport("Distro version"), versionId));
    }
#endif
    system.append(systemRow(trReport("Kernel"),
                            QStringLiteral("%1 %2").arg(QSysInfo::kernelType(), QSysInfo::kernelVersion())));
    system.append(systemRow(trReport("Architecture"), QSysInfo::currentCpuArchitecture()));
    system.append(systemRow(trReport("CPU"), cpuModel()));
    system.append(systemRow(trReport("CPU threads"), QString::number(QThread::idealThreadCount())));

    const QList<drift::gpu::Adapter> gpus = drift::gpu::enumerateAdapters();
    if (gpus.isEmpty()) {
        system.append(systemRow(trReport("GPU"), trReport("Unknown")));
    } else if (gpus.size() == 1) {
        system.append(systemRow(trReport("GPU"), formatGpu(gpus.first())));
    } else {
        for (int i = 0; i < gpus.size(); ++i) {
            system.append(systemRow(trReport("GPU %1").arg(i + 1), formatGpu(gpus.at(i))));
        }
    }
    const OpenGlInfo glInfo = openglInfo();
    if (const QString gl = openglLabel(glInfo); !gl.isEmpty())
        system.append(systemRow(QStringLiteral("OpenGL"), gl));
    // Its own row rather than a suffix on the one above: this is the line worth
    // grepping for in a pasted report.
    if (glInfo.valid) {
        system.append(systemRow(trReport("OpenGL driver"),
                                glInfo.software ? trReport("Software rasterizer")
                                                : trReport("Hardware")));
    }
    system.append(systemRow(trReport("GPU compositor"), gpuCompositorLabel()));

    system.append(systemRow(trReport("Qt"), QString::fromLatin1(qVersion())));
    system.append(systemRow(trReport("FFmpeg"), QString::fromUtf8(av_version_info())));
    system.append(systemRow(trReport("Hardware decode"),
                            backend != drift::hwaccel::Backend::None
                                ? QString::fromLatin1(drift::hwaccel::name(backend))
                            : mediaCodecOk ? QStringLiteral("MediaCodec")
                                           : trReport("Not available")));
    const QList<drift::ort::RuntimeInfo> ortRuntimes = drift::ort::installedRuntimes();
    if (ortRuntimes.isEmpty()) {
        system.append(systemRow(trReport("ONNX Runtime"), trReport("Not installed")));
    } else {
        const drift::ort::RuntimeInfo &rt = ortRuntimes.first();
        QString value = rt.variant;
        if (!rt.version.isEmpty())
            value = QStringLiteral("%1 %2").arg(rt.variant, rt.version);
        system.append(systemRow(trReport("ONNX Runtime"), value));
    }
    system.append(systemRow(trReport("Preview decode"), decodeModeLabel()));
    system.append(systemRow(trReport("Active decode"), activeDecodeLabel()));
    {
        const QString platform = QGuiApplication::platformName();
        system.append(systemRow(trReport("Window platform"),
                                platform.isEmpty() ? trReport("Unknown") : platform));
    }
    system.append(systemRow(trReport("Preview upload"), previewUploadLabel()));
    system.append(systemRow(trReport("Zero-copy"), zeroCopyLabel()));
#if defined(Q_OS_WIN)
    if (drift::gpu::preferenceSupported()) {
        QString preference = trReport("Windows default");
        switch (drift::gpu::storedPreference()) {
        case drift::gpu::Preference::PowerSaving:
            preference = trReport("Power saving");
            break;
        case drift::gpu::Preference::HighPerformance:
            preference = trReport("High performance");
            break;
        case drift::gpu::Preference::Auto:
            break;
        }
        system.append(systemRow(trReport("Preferred GPU"), preference));
    }
#endif
    system.append(systemRow(trReport("Locale"), QLocale::system().name()));
    if (drift::hwaccel::disabledByEnv())
        system.append(systemRow(QStringLiteral("DRIFT_NO_HWACCEL"), trReport("Set")));
#if defined(Q_OS_ANDROID)
    if (qEnvironmentVariableIsSet("DRIFT_NO_MEDIACODEC"))
        system.append(systemRow(QStringLiteral("DRIFT_NO_MEDIACODEC"), trReport("Set")));
#endif

    QVariantList hints;
    if (package == QLatin1String("Flatpak")) {
        const bool hasX264 = findNamedEncoder(kH264Enc) != nullptr;
        if (!flatpakExtensionMounted(QStringLiteral("codecs-extra")) && !hasX264) {
            hints.append(hintRow(
                QStringLiteral("codecs-extra"), trReport("Missing extra codecs"),
                trReport("H.264 and H.265 encoding is missing from this Flatpak. Install the extra "
                         "codecs extension, then restart Drift."),
                QStringLiteral("flatpak install org.freedesktop.Platform.codecs-extra")));
        }
        bool nvidia = false;
        for (const drift::gpu::Adapter &gpu : gpus) {
            if (gpuIsNvidia(gpu)) {
                nvidia = true;
                break;
            }
        }
        if (nvidia && !flatpakExtensionMounted(QStringLiteral("dri/nvidia-vaapi-driver"))) {
            hints.append(hintRow(
                QStringLiteral("vaapi-nvidia"), trReport("NVIDIA VAAPI driver not installed"),
                trReport("VAAPI encode on NVIDIA needs the NVIDIA VAAPI extension, and so does "
                         "hardware decode when NVDEC is unavailable. Install it, then restart "
                         "Drift."),
                    QStringLiteral("flatpak install org.freedesktop.Platform.VAAPI.nvidia")));
        }
    }
    bool amd = false;
    for (const drift::gpu::Adapter &gpu : gpus) {
        if (gpuIsAmd(gpu)) {
            amd = true;
            break;
        }
    }
    if (amd) {
        hints.append(hintRow(
            QStringLiteral("amd-gfx6-8"), trReport("Pre-Vega AMD skips zero-copy preview"),
            trReport("GCN 1–4 GPUs (HD 7000 through Polaris / RX 500) export tiled surfaces "
                     "without a DRM modifier, so Drift refuses zero-copy preview and copies "
                     "each frame through system memory. Vega, Navi and newer can enable "
                     "Settings → Preview → Faster preview.")));
    }
    // The whole of issue #139: Qt's GPU blacklist matches a card it failed to
    // identify, loads its bundled llvmpipe, and the preview goes black on hardware
    // that would have run it fine.
    if (glInfo.software || GpuCompositor::status().status == drift::gl::GlStatus::VersionTooLow) {
#if defined(Q_OS_WIN)
        hints.append(hintRow(
            QStringLiteral("software-opengl"),
            trReport("Preview is running on a software OpenGL driver"),
            trReport("Windows loaded Qt's bundled software renderer instead of your graphics "
                     "card. It only provides OpenGL 3.0, below the 3.3 the preview needs, so "
                     "the picture stays black. Update your graphics driver; if that does not "
                     "help, start Drift with QT_OPENGL=desktop."),
            QStringLiteral("set QT_OPENGL=desktop")));
#else
        hints.append(hintRow(
            QStringLiteral("software-opengl"),
            trReport("Preview is running on a software OpenGL driver"),
            trReport("OpenGL is being rendered on the CPU rather than the GPU, which the "
                     "preview may be too old to use. Check that a Mesa driver for your card is "
                     "installed and that LIBGL_ALWAYS_SOFTWARE is not set.")));
#endif
    }
    if (ortRuntimes.isEmpty()) {
        hints.append(hintRow(
            QStringLiteral("onnxruntime"), trReport("AI engine not installed"),
            trReport("Auto-subtitles, segmentation and face tracking need ONNX Runtime from "
                     "Add-ons → Acceleration."),
            {}, QStringLiteral("addons")));
    }

    info.insert(QStringLiteral("codecs"), codecs);
    info.insert(QStringLiteral("encoders"), encoders);
    info.insert(QStringLiteral("system"), system);
    info.insert(QStringLiteral("hints"), hints);
    info.insert(QStringLiteral("version"), QStringLiteral(DRIFT_VERSION));
    info.insert(QStringLiteral("package"), package);
    info.insert(QStringLiteral("hardwareDecodeAvailable"), hwDecodeOk);
    info.insert(QStringLiteral("hardwareDecodeBackend"),
                backend != drift::hwaccel::Backend::None
                    ? QString::fromLatin1(drift::hwaccel::name(backend))
                : mediaCodecOk ? QStringLiteral("mediacodec")
                               : QString::fromLatin1(drift::hwaccel::name(backend)));
    return info;
}

QString DebugReport::formatPlainText(const QVariantMap &info)
{
    QString text;
    text += QStringLiteral("CutWire Drift debug report\n\n");

    text += QStringLiteral("## System\n");
    const QVariantList system = info.value(QStringLiteral("system")).toList();
    for (const QVariant &entry : system) {
        const QVariantMap row = entry.toMap();
        text += QStringLiteral("- %1: %2\n")
                    .arg(row.value(QStringLiteral("label")).toString(),
                         row.value(QStringLiteral("value")).toString());
    }

    auto appendCodecTable = [&](const QString &heading, const QString &swHeader, const QString &hwHeader,
                                const QString &listKey, const QString &swNameKey, const QString &hwNameKey) {
        text += QStringLiteral("\n## %1\n").arg(heading);
        text += QStringLiteral("| Codec | %1 | %2 | SW | HW |\n").arg(swHeader, hwHeader);
        text += QStringLiteral("| --- | --- | --- | --- | --- |\n");
        const QVariantList rows = info.value(listKey).toList();
        for (const QVariant &entry : rows) {
            const QVariantMap row = entry.toMap();
            const QString hardware = row.value(QStringLiteral("hardwareUnavailable")).toBool()
                                         ? trReport("Unavailable")
                                         : supportLabel(row.value(QStringLiteral("hardware")).toBool());
            text += QStringLiteral("| %1 | %2 | %3 | %4 | %5 |\n")
                        .arg(row.value(QStringLiteral("name")).toString(),
                             supportLabel(row.value(QStringLiteral("software")).toBool()), hardware,
                             row.value(swNameKey).toString(), row.value(hwNameKey).toString());
        }
    };

    appendCodecTable(QStringLiteral("Video decoders"), QStringLiteral("Software"), QStringLiteral("Hardware"),
                     QStringLiteral("codecs"), QStringLiteral("softwareDecoder"),
                     QStringLiteral("hardwareDecoder"));
    appendCodecTable(QStringLiteral("Video encoders"), QStringLiteral("Software"), QStringLiteral("Hardware"),
                     QStringLiteral("encoders"), QStringLiteral("softwareEncoder"),
                     QStringLiteral("hardwareEncoder"));

    const QVariantList hints = info.value(QStringLiteral("hints")).toList();
    if (!hints.isEmpty()) {
        text += QStringLiteral("\n## Hints\n");
        for (const QVariant &entry : hints) {
            const QVariantMap row = entry.toMap();
            text += QStringLiteral("- %1: %2\n")
                        .arg(row.value(QStringLiteral("title")).toString(),
                             row.value(QStringLiteral("detail")).toString());
            const QString command = row.value(QStringLiteral("command")).toString();
            if (!command.isEmpty())
                text += QStringLiteral("  `%1`\n").arg(command);
        }
    }
    return text;
}
