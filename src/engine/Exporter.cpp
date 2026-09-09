#include "Exporter.h"

#include "AudioMixer.h"
#include "FrameCompositor.h"
#include "GpuCompositor.h"
#include "HwAccel.h"
#include "core/Project.h"
#include "core/Time.h"

#include <QFile>
#include <QHash>
#include <QImage>
#include <QMutex>
#include <QtGlobal>

#ifdef Q_OS_ANDROID
#include "AndroidUri.h"

#include <QDir>
#include <QFileInfo>
#include <QJniEnvironment>
#include <QJniObject>
#include <QMimeDatabase>
#include <QStandardPaths>
#include <QTemporaryFile>
#include <QtCore/qcoreapplication_platform.h>

#include <atomic>
#endif

#include <climits>
#include <cmath>
#include <cstring>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/dict.h>
#include <libavutil/hwcontext.h>
#include <libavutil/imgutils.h>
#include <libavutil/log.h>
#include <libavutil/opt.h>
#include <libavutil/pixdesc.h>
#include <libavutil/rational.h>
#include <libswscale/swscale.h>

#include <libavfilter/avfilter.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
}

namespace {

void resolveExportRange(const drift::Project &project, const ExportSettings &settings,
                        drift::TimeUs *startUsOut, drift::TimeUs *endUsOut, QString *errorOut)
{
    const drift::TimeUs projectDuration = project.durationUs();
    drift::TimeUs startUs = settings.startUs;
    drift::TimeUs endUs = settings.endUs;

    if (startUs == 0 && endUs == 0) {
        startUs = 0;
        endUs = projectDuration;
    } else {
        if (startUs < 0 || endUs <= startUs) {
            if (errorOut)
                *errorOut = QStringLiteral("Invalid export range");
            *startUsOut = 0;
            *endUsOut = 0;
            return;
        }
        endUs = qMin(endUs, projectDuration);
        if (endUs <= startUs) {
            if (errorOut)
                *errorOut = QStringLiteral("Export range is empty");
            *startUsOut = 0;
            *endUsOut = 0;
            return;
        }
    }

    if (endUs <= startUs) {
        if (errorOut)
            *errorOut = QStringLiteral("Timeline is empty");
        *startUsOut = 0;
        *endUsOut = 0;
        return;
    }

    *startUsOut = startUs;
    *endUsOut = endUs;
}

} // namespace

namespace {

#ifdef Q_OS_ANDROID

constexpr const char *kExportServiceClass = "org/cutwire/drift/ExportService";

std::atomic<int> g_backgroundHolds{0};
std::atomic<int> g_notifiedPercent{-1};

// Set by the notification's Cancel action, read by whatever job the outermost hold is protecting.
// Cleared when that hold is taken, so a cancel can never carry into the next job.
std::atomic<bool> g_serviceCancelRequested{false};

// The Cancel action's PendingIntent lands in ExportService.onStartCommand, which has no way back
// into the render except this. Nothing else in the tree registers a native, so the registration
// is done once, lazily, beside the first hold.
void JNICALL exportServiceCancel(JNIEnv *, jclass)
{
    g_serviceCancelRequested.store(true, std::memory_order_relaxed);
}

// False when the Java side predates the Cancel action, which is also the case where calling the
// native would throw. The service still runs; the action simply never appears, because
// startExportService only offers it when this returned true.
bool registerExportServiceNatives()
{
    static const bool registered = []() {
        const JNINativeMethod methods[] = {
            {"nativeCancelRequested", "()V", reinterpret_cast<void *>(exportServiceCancel)},
        };
        return QJniEnvironment().registerNativeMethods(kExportServiceClass, methods, 1);
    }();
    return registered;
}

void startExportService(const QString &title, bool cancellable)
{
    QJniObject context = QNativeInterface::QAndroidApplication::context();
    if (!context.isValid())
        return;

    QJniEnvironment env;
    jclass clazz = env.findClass(kExportServiceClass);
    if (!clazz)
        return;

    // A Cancel button whose native never registered would crash on the first tap.
    cancellable = cancellable && registerExportServiceNatives();

    // Each overload arrives with a Java side; until it does, the job borrows the previous one's
    // signature — a wrong notification title or a missing Cancel button, but the process is still
    // held in the foreground. Probing is what makes the fallback work at all: QJniObject clears the
    // NoSuchMethodError itself and turns the missing method into a silent no-op, so calling and
    // checking afterwards would report success and start nothing.
    if (jmethodID cancellableForm = env.findStaticMethod(
            clazz, "start", "(Landroid/content/Context;Ljava/lang/String;Z)V")) {
        QJniObject::callStaticMethod<void>(clazz, cancellableForm, context.object(),
                                           QJniObject::fromString(title).object<jstring>(),
                                           static_cast<jboolean>(cancellable));
    } else if (jmethodID titled = env.findStaticMethod(
                   clazz, "start", "(Landroid/content/Context;Ljava/lang/String;)V")) {
        QJniObject::callStaticMethod<void>(clazz, titled, context.object(),
                                           QJniObject::fromString(title).object<jstring>());
    } else {
        QJniObject::callStaticMethod<void>(clazz, "start", "(Landroid/content/Context;)V",
                                           context.object());
    }
    env.checkAndClearExceptions();
}

void stopExportService()
{
    QJniObject context = QNativeInterface::QAndroidApplication::context();
    if (!context.isValid())
        return;
    QJniObject::callStaticMethod<void>(kExportServiceClass, "stop", "(Landroid/content/Context;)V",
                                       context.object());
    // A job that cannot raise its notification still has to run: the JNI failure is the service
    // being unavailable, not the render being wrong.
    QJniEnvironment().checkAndClearExceptions();
}

#endif

enum class RateMode { Crf, Bitrate, Lossless };

enum class HwBackend { None, Nvenc, Qsv, Amf, Vaapi, VideoToolbox, MediaCodec };

struct VideoCodecDef {
    const char *id;
    const char *label;
    // Preferred encoder names, tried in order (nullptr-terminated).
    const char *const *encoderNames;
    AVPixelFormat pixFmt;
    RateMode rateMode;
    bool supportsPreset;
    const char *const *presets; // nullptr-terminated; nullptr if none
    const char *defaultPreset;
    int defaultCrf;
    // "mp4" | "webm" | "mkv" preferred when paired with a friendly audio codec.
    const char *preferredContainer;
    HwBackend hw = HwBackend::None;
};

struct AudioCodecDef {
    const char *id;
    const char *label;
    const char *const *encoderNames;
    bool lossless;
    // "mp4" | "webm" | "mkv" | "any"
    const char *containerFamily;
};

const char *const kLibx264[] = {"libx264", "h264", nullptr};
const char *const kLibx265[] = {"libx265", "hevc", nullptr};
const char *const kLibSvtAv1[] = {"libsvtav1", nullptr};
const char *const kFfv1[] = {"ffv1", nullptr};
const char *const kMpeg4[] = {"mpeg4", nullptr};
const char *const kMpeg2[] = {"mpeg2video", nullptr};
const char *const kLibvpx[] = {"libvpx", nullptr};
const char *const kLibvpxVp9[] = {"libvpx-vp9", nullptr};
const char *const kDnxhd[] = {"dnxhd", nullptr};
const char *const kProres[] = {"prores_ks", "prores", nullptr};
const char *const kLibtheora[] = {"libtheora", nullptr};

const char *const kH264Nvenc[] = {"h264_nvenc", nullptr};
const char *const kHevcNvenc[] = {"hevc_nvenc", nullptr};
const char *const kAv1Nvenc[] = {"av1_nvenc", nullptr};
const char *const kH264Qsv[] = {"h264_qsv", nullptr};
const char *const kHevcQsv[] = {"hevc_qsv", nullptr};
const char *const kAv1Qsv[] = {"av1_qsv", nullptr};
const char *const kH264Amf[] = {"h264_amf", nullptr};
const char *const kHevcAmf[] = {"hevc_amf", nullptr};
const char *const kAv1Amf[] = {"av1_amf", nullptr};
const char *const kH264Vaapi[] = {"h264_vaapi", nullptr};
const char *const kHevcVaapi[] = {"hevc_vaapi", nullptr};
const char *const kAv1Vaapi[] = {"av1_vaapi", nullptr};
const char *const kH264Vt[] = {"h264_videotoolbox", nullptr};
const char *const kHevcVt[] = {"hevc_videotoolbox", nullptr};
const char *const kH264MediaCodec[] = {"h264_mediacodec", nullptr};
const char *const kHevcMediaCodec[] = {"hevc_mediacodec", nullptr};

const char *const kX264Presets[] = {"ultrafast", "superfast", "veryfast", "faster", "fast",
                                    "medium",    "slow",      "slower",   "veryslow", nullptr};
const char *const kSvtPresets[] = {"0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", nullptr};
const char *const kVp9CpuUsed[] = {"0", "1", "2", "3", "4", "5", "6", "7", "8", nullptr};
const char *const kNvencPresets[] = {"p1", "p2", "p3", "p4", "p5", "p6", "p7", nullptr};
const char *const kQsvPresets[] = {"veryfast", "faster", "fast", "medium", "slow", "slower", "veryslow", nullptr};
const char *const kAmfQuality[] = {"speed", "balanced", "quality", nullptr};

const VideoCodecDef kVideoCodecs[] = {
    {"av1_svt", "AV1 (SVT)", kLibSvtAv1, AV_PIX_FMT_YUV420P, RateMode::Crf, true, kSvtPresets, "6", 35, "mp4"},
    {"av1_nvenc", "AV1 (NVIDIA)", kAv1Nvenc, AV_PIX_FMT_NV12, RateMode::Crf, true, kNvencPresets, "p4", 30, "mp4",
     HwBackend::Nvenc},
    {"av1_qsv", "AV1 (Intel)", kAv1Qsv, AV_PIX_FMT_NV12, RateMode::Crf, true, kQsvPresets, "medium", 30, "mp4",
     HwBackend::Qsv},
    {"av1_amf", "AV1 (AMD)", kAv1Amf, AV_PIX_FMT_NV12, RateMode::Crf, true, kAmfQuality, "balanced", 30, "mp4",
     HwBackend::Amf},
    {"av1_vaapi", "AV1 (VAAPI)", kAv1Vaapi, AV_PIX_FMT_NV12, RateMode::Crf, false, nullptr, nullptr, 30, "mp4",
     HwBackend::Vaapi},
    {"av1_svt_10", "AV1 10-bit (SVT)", kLibSvtAv1, AV_PIX_FMT_YUV420P10LE, RateMode::Crf, true, kSvtPresets, "6", 35,
     "mkv"},
    {"ffv1", "FFV1", kFfv1, AV_PIX_FMT_YUV444P, RateMode::Lossless, false, nullptr, nullptr, 0, "mkv"},
    {"h264", "H.264 (x264)", kLibx264, AV_PIX_FMT_YUV420P, RateMode::Crf, true, kX264Presets, "medium", 18, "mp4"},
    {"h264_nvenc", "H.264 (NVIDIA)", kH264Nvenc, AV_PIX_FMT_NV12, RateMode::Crf, true, kNvencPresets, "p4", 23, "mp4",
     HwBackend::Nvenc},
    {"h264_qsv", "H.264 (Intel)", kH264Qsv, AV_PIX_FMT_NV12, RateMode::Crf, true, kQsvPresets, "medium", 23, "mp4",
     HwBackend::Qsv},
    {"h264_amf", "H.264 (AMD)", kH264Amf, AV_PIX_FMT_NV12, RateMode::Crf, true, kAmfQuality, "balanced", 23, "mp4",
     HwBackend::Amf},
    {"h264_vaapi", "H.264 (VAAPI)", kH264Vaapi, AV_PIX_FMT_NV12, RateMode::Crf, false, nullptr, nullptr, 23, "mp4",
     HwBackend::Vaapi},
    {"h264_videotoolbox", "H.264 (VideoToolbox)", kH264Vt, AV_PIX_FMT_NV12, RateMode::Crf, false, nullptr, nullptr, 23,
     "mp4", HwBackend::VideoToolbox},
    // Bitrate and not Crf: MediaCodec targets a bitrate, and its constant-quality mode is API 30+
    // and refused outright by a large share of shipping devices. "Hardware" and not a vendor name
    // because the encoder is whatever block the SoC ships and MediaCodec never says which.
    {"h264_mediacodec", "H.264 (Hardware)", kH264MediaCodec, AV_PIX_FMT_NV12, RateMode::Bitrate, false, nullptr,
     nullptr, 0, "mp4", HwBackend::MediaCodec},
    {"h264_10", "H.264 10-bit (x264)", kLibx264, AV_PIX_FMT_YUV420P10LE, RateMode::Crf, true, kX264Presets, "medium",
     18, "mkv"},
    {"h265", "H.265 (x265)", kLibx265, AV_PIX_FMT_YUV420P, RateMode::Crf, true, kX264Presets, "medium", 28, "mp4"},
    {"h265_nvenc", "H.265 (NVIDIA)", kHevcNvenc, AV_PIX_FMT_NV12, RateMode::Crf, true, kNvencPresets, "p4", 28, "mp4",
     HwBackend::Nvenc},
    {"h265_qsv", "H.265 (Intel)", kHevcQsv, AV_PIX_FMT_NV12, RateMode::Crf, true, kQsvPresets, "medium", 28, "mp4",
     HwBackend::Qsv},
    {"h265_amf", "H.265 (AMD)", kHevcAmf, AV_PIX_FMT_NV12, RateMode::Crf, true, kAmfQuality, "balanced", 28, "mp4",
     HwBackend::Amf},
    {"h265_vaapi", "H.265 (VAAPI)", kHevcVaapi, AV_PIX_FMT_NV12, RateMode::Crf, false, nullptr, nullptr, 28, "mp4",
     HwBackend::Vaapi},
    {"h265_videotoolbox", "H.265 (VideoToolbox)", kHevcVt, AV_PIX_FMT_NV12, RateMode::Crf, false, nullptr, nullptr, 28,
     "mp4", HwBackend::VideoToolbox},
    {"h265_mediacodec", "H.265 (Hardware)", kHevcMediaCodec, AV_PIX_FMT_NV12, RateMode::Bitrate, false, nullptr,
     nullptr, 0, "mp4", HwBackend::MediaCodec},
    {"h265_10", "H.265 10-bit (x265)", kLibx265, AV_PIX_FMT_YUV420P10LE, RateMode::Crf, true, kX264Presets, "medium",
     28, "mkv"},
    {"h265_12", "H.265 12-bit (x265)", kLibx265, AV_PIX_FMT_YUV420P12LE, RateMode::Crf, true, kX264Presets, "medium",
     28, "mkv"},
    {"mpeg4", "MPEG-4", kMpeg4, AV_PIX_FMT_YUV420P, RateMode::Bitrate, false, nullptr, nullptr, 0, "mp4"},
    {"mpeg2", "MPEG-2", kMpeg2, AV_PIX_FMT_YUV420P, RateMode::Bitrate, false, nullptr, nullptr, 0, "mkv"},
    {"vp8", "VP8", kLibvpx, AV_PIX_FMT_YUV420P, RateMode::Crf, true, kVp9CpuUsed, "4", 10, "webm"},
    {"vp9", "VP9", kLibvpxVp9, AV_PIX_FMT_YUV420P, RateMode::Crf, true, kVp9CpuUsed, "4", 32, "webm"},
    {"vp9_10", "VP9 10-bit", kLibvpxVp9, AV_PIX_FMT_YUV420P10LE, RateMode::Crf, true, kVp9CpuUsed, "4", 32, "webm"},
    {"dnxhr", "DNxHR", kDnxhd, AV_PIX_FMT_YUV422P, RateMode::Bitrate, false, nullptr, nullptr, 0, "mkv"},
    {"dnxhr_10", "DNxHR 10-bit", kDnxhd, AV_PIX_FMT_YUV422P10LE, RateMode::Bitrate, false, nullptr, nullptr, 0,
     "mkv"},
    {"prores", "ProRes", kProres, AV_PIX_FMT_YUV422P10LE, RateMode::Lossless, false, nullptr, nullptr, 0, "mkv"},
    {"theora", "Theora", kLibtheora, AV_PIX_FMT_YUV420P, RateMode::Bitrate, false, nullptr, nullptr, 0, "mkv"},
};

const char *const kAacEnc[] = {"aac", nullptr};
const char *const kOpusEnc[] = {"libopus", "opus", nullptr};
const char *const kMp3Enc[] = {"libmp3lame", "mp3", nullptr};
const char *const kAc3Enc[] = {"ac3", nullptr};
const char *const kFlacEnc[] = {"flac", nullptr};

const AudioCodecDef kAudioCodecs[] = {
    {"aac", "AAC", kAacEnc, false, "mp4"},
    {"opus", "Opus", kOpusEnc, false, "webm"},
    {"mp3", "MP3", kMp3Enc, false, "mp4"},
    {"ac3", "AC3", kAc3Enc, false, "mp4"},
    {"flac", "FLAC", kFlacEnc, true, "mkv"},
};

const AVCodec *findEncoder(const char *const *names)
{
    for (int i = 0; names && names[i]; ++i) {
        if (const AVCodec *c = avcodec_find_encoder_by_name(names[i]))
            return c;
    }
    return nullptr;
}

AVHWDeviceType hwDeviceType(HwBackend hw)
{
    switch (hw) {
    case HwBackend::Nvenc:
        return AV_HWDEVICE_TYPE_CUDA;
    case HwBackend::Qsv:
        return AV_HWDEVICE_TYPE_QSV;
    case HwBackend::Amf:
        return AV_HWDEVICE_TYPE_D3D11VA;
    case HwBackend::Vaapi:
        return AV_HWDEVICE_TYPE_VAAPI;
    case HwBackend::VideoToolbox:
        return AV_HWDEVICE_TYPE_VIDEOTOOLBOX;
    // FFmpeg's MediaCodec device type wraps a decode Surface; mediacodecenc neither needs nor
    // accepts one, and asking for it would fail the device probe below for no reason.
    case HwBackend::MediaCodec:
    case HwBackend::None:
        break;
    }
    return AV_HWDEVICE_TYPE_NONE;
}

bool hwBackendOnThisOs(HwBackend hw)
{
    if (hw == HwBackend::None)
        return true;
#if defined(Q_OS_MACOS)
    return hw == HwBackend::VideoToolbox;
#elif defined(Q_OS_WIN)
    return hw == HwBackend::Nvenc || hw == HwBackend::Qsv || hw == HwBackend::Amf;
#elif defined(Q_OS_ANDROID)
    // Android is a Linux build, so without this arm it would advertise the desktop GPU backends —
    // none of which the Android FFmpeg is even configured with.
    return hw == HwBackend::MediaCodec;
#else
    return hw == HwBackend::Nvenc || hw == HwBackend::Qsv || hw == HwBackend::Vaapi;
#endif
}

const char *hwVendorName(HwBackend hw)
{
    switch (hw) {
    case HwBackend::Nvenc:
        return "NVIDIA";
    case HwBackend::Qsv:
        return "Intel";
    case HwBackend::Amf:
        return "AMD";
    case HwBackend::Vaapi:
        return "VAAPI";
    case HwBackend::VideoToolbox:
        return "VideoToolbox";
    case HwBackend::MediaCodec:
        return "MediaCodec";
    case HwBackend::None:
        break;
    }
    return "";
}

const char *hwCodecFamilyName(const char *id)
{
    if (std::strncmp(id, "h265", 4) == 0)
        return "H.265";
    if (std::strncmp(id, "av1", 3) == 0)
        return "AV1";
    return "H.264";
}

QString hwEncoderLabel(const VideoCodecDef &def)
{
    return QStringLiteral("%1 %2").arg(QLatin1String(hwVendorName(def.hw)),
                                       QLatin1String(hwCodecFamilyName(def.id)));
}

bool isHardwarePixelFormat(AVPixelFormat fmt)
{
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(fmt);
    return desc && (desc->flags & AV_PIX_FMT_FLAG_HWACCEL);
}

AVPixelFormat pickEncodePixFmt(const AVCodec *codec, HwBackend hw, AVPixelFormat softwarePixFmt)
{
    if (hw == HwBackend::None || !codec)
        return softwarePixFmt;

    const void *configs = nullptr;
    if (avcodec_get_supported_config(nullptr, codec, AV_CODEC_CONFIG_PIX_FORMAT, 0, &configs, nullptr) >= 0
        && configs) {
        const auto *fmts = static_cast<const AVPixelFormat *>(configs);
        for (const AVPixelFormat *p = fmts; *p != AV_PIX_FMT_NONE; ++p) {
            if (*p == softwarePixFmt)
                return softwarePixFmt;
        }
    }

    const AVHWDeviceType type = hwDeviceType(hw);
    for (int i = 0;; ++i) {
        const AVCodecHWConfig *config = avcodec_get_hw_config(codec, i);
        if (!config)
            break;
        if ((config->methods
             & (AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX | AV_CODEC_HW_CONFIG_METHOD_HW_FRAMES_CTX))
            && config->device_type == type && config->pix_fmt != AV_PIX_FMT_NONE)
            return config->pix_fmt;
    }
    return softwarePixFmt;
}

bool setupHwFrames(AVCodecContext *vctx, AVBufferRef *deviceCtx, AVPixelFormat hwPixFmt, int w, int h,
                   QString *error)
{
    AVBufferRef *framesRef = av_hwframe_ctx_alloc(deviceCtx);
    if (!framesRef) {
        *error = QStringLiteral("Could not allocate hardware frames");
        return false;
    }
    auto *frames = reinterpret_cast<AVHWFramesContext *>(framesRef->data);
    frames->format = hwPixFmt;
    frames->sw_format = AV_PIX_FMT_NV12;
    frames->width = w;
    frames->height = h;
    if (hwPixFmt == AV_PIX_FMT_VAAPI || hwPixFmt == AV_PIX_FMT_QSV)
        frames->initial_pool_size = 20;
    if (av_hwframe_ctx_init(framesRef) < 0) {
        av_buffer_unref(&framesRef);
        *error = QStringLiteral("Could not initialize hardware frames");
        return false;
    }
    vctx->hw_frames_ctx = framesRef;
    return true;
}

const VideoCodecDef *findVideoDef(const QString &id)
{
    for (const VideoCodecDef &def : kVideoCodecs) {
        if (id == QLatin1String(def.id))
            return &def;
    }
    return nullptr;
}

const AudioCodecDef *findAudioDef(const QString &id)
{
    for (const AudioCodecDef &def : kAudioCodecs) {
        if (id == QLatin1String(def.id))
            return &def;
    }
    return nullptr;
}

QStringList presetsToList(const char *const *presets)
{
    QStringList out;
    for (int i = 0; presets && presets[i]; ++i)
        out.append(QString::fromUtf8(presets[i]));
    return out;
}

QVariantMap videoDefToMap(const VideoCodecDef &def)
{
    const AVCodec *enc = findEncoder(def.encoderNames);
    bool available = enc != nullptr;
    if (def.hw != HwBackend::None) {
        available = available && hwBackendOnThisOs(def.hw);
        // MediaCodec has no AVHWDevice to probe — the encoder existing in the build is the whole
        // answer, and deviceAvailable(NONE) is false, which would hide the row outright.
        const AVHWDeviceType type = hwDeviceType(def.hw);
        if (type != AV_HWDEVICE_TYPE_NONE)
            available = available && drift::hwaccel::deviceAvailable(type);
    }

    QVariantMap m;
    m.insert(QStringLiteral("id"), QString::fromUtf8(def.id));
    m.insert(QStringLiteral("label"), QString::fromUtf8(def.label));
    m.insert(QStringLiteral("available"), available);
    m.insert(QStringLiteral("hardware"), def.hw != HwBackend::None);
    m.insert(QStringLiteral("encoderName"), enc ? QString::fromUtf8(enc->name) : QString());
    m.insert(QStringLiteral("supportsCrf"), def.rateMode == RateMode::Crf);
    m.insert(QStringLiteral("supportsBitrate"), def.rateMode != RateMode::Lossless);
    m.insert(QStringLiteral("lossless"), def.rateMode == RateMode::Lossless);
    m.insert(QStringLiteral("supportsPreset"), def.supportsPreset);
    m.insert(QStringLiteral("presets"), presetsToList(def.presets));
    m.insert(QStringLiteral("defaultPreset"),
             def.defaultPreset ? QString::fromUtf8(def.defaultPreset) : QString());
    m.insert(QStringLiteral("defaultCrf"), def.defaultCrf);
    m.insert(QStringLiteral("container"), QString::fromUtf8(def.preferredContainer));
    return m;
}

QVariantMap audioDefToMap(const AudioCodecDef &def)
{
    const AVCodec *enc = findEncoder(def.encoderNames);
    QVariantMap m;
    m.insert(QStringLiteral("id"), QString::fromUtf8(def.id));
    m.insert(QStringLiteral("label"), QString::fromUtf8(def.label));
    m.insert(QStringLiteral("available"), enc != nullptr);
    m.insert(QStringLiteral("lossless"), def.lossless);
    m.insert(QStringLiteral("container"), QString::fromUtf8(def.containerFamily));
    return m;
}

void evenDims(int &w, int &h)
{
    w &= ~1;
    h &= ~1;
    w = qMax(2, w);
    h = qMax(2, h);
}

void scaleSize(int projW, int projH, int targetH, int &outW, int &outH)
{
    outH = targetH > 0 ? targetH : projH;
    outW = static_cast<int>(std::llround(static_cast<double>(projW) * outH / projH));
    evenDims(outW, outH);
}

void fillLimitedBlackNv12(uint8_t *y, int yStride, uint8_t *uv, int uvStride, int w, int h)
{
    for (int row = 0; row < h; ++row)
        memset(y + row * yStride, 16, size_t(w));
    for (int row = 0; row < h / 2; ++row)
        memset(uv + row * uvStride, 128, size_t(w));
}

void fillLimitedBlackFrame(AVFrame *frame)
{
    if (!frame || !frame->data[0])
        return;
    if (frame->format == AV_PIX_FMT_NV12 && frame->data[1]) {
        fillLimitedBlackNv12(frame->data[0], frame->linesize[0], frame->data[1], frame->linesize[1],
                             frame->width, frame->height);
        return;
    }
    if (frame->format == AV_PIX_FMT_YUV420P && frame->data[1] && frame->data[2]) {
        for (int row = 0; row < frame->height; ++row)
            memset(frame->data[0] + row * frame->linesize[0], 16, size_t(frame->width));
        for (int row = 0; row < frame->height / 2; ++row) {
            memset(frame->data[1] + row * frame->linesize[1], 128, size_t(frame->width / 2));
            memset(frame->data[2] + row * frame->linesize[2], 128, size_t(frame->width / 2));
        }
    }
}

void nv12ToYuv420p(const uint8_t *y, int yStride, const uint8_t *uv, int uvStride, AVFrame *dst)
{
    const int w = dst->width;
    const int h = dst->height;
    for (int row = 0; row < h; ++row)
        memcpy(dst->data[0] + row * dst->linesize[0], y + row * yStride, size_t(w));
    for (int row = 0; row < h / 2; ++row) {
        const uint8_t *src = uv + row * uvStride;
        uint8_t *u = dst->data[1] + row * dst->linesize[1];
        uint8_t *v = dst->data[2] + row * dst->linesize[2];
        for (int x = 0; x < w / 2; ++x) {
            u[x] = src[2 * x];
            v[x] = src[2 * x + 1];
        }
    }
}

// Sends a frame (or nullptr to flush) to the encoder and interleaves the packets.
bool encodeWriteFrame(AVFormatContext *fmt, AVCodecContext *codec, AVStream *stream, AVFrame *frame,
                      AVPacket *pkt, QString *errorOut)
{
    int rc = avcodec_send_frame(codec, frame);
    if (rc < 0) {
        if (errorOut)
            *errorOut = QStringLiteral("Encoder rejected a frame");
        return false;
    }

    while (rc >= 0) {
        rc = avcodec_receive_packet(codec, pkt);
        if (rc == AVERROR(EAGAIN) || rc == AVERROR_EOF)
            break;
        if (rc < 0) {
            if (errorOut)
                *errorOut = QStringLiteral("Failed to read an encoded packet");
            return false;
        }
        av_packet_rescale_ts(pkt, codec->time_base, stream->time_base);
        pkt->stream_index = stream->index;
        rc = av_interleaved_write_frame(fmt, pkt);
        av_packet_unref(pkt);
        if (rc < 0) {
            if (errorOut)
                *errorOut = QStringLiteral("Failed to write a packet");
            return false;
        }
    }
    return true;
}

AVSampleFormat pickSampleFmt(const AVCodec *codec)
{
    const void *configs = nullptr;
    if (!codec
        || avcodec_get_supported_config(nullptr, codec, AV_CODEC_CONFIG_SAMPLE_FORMAT, 0, &configs,
                                        nullptr)
            < 0
        || !configs)
        return AV_SAMPLE_FMT_FLTP;

    const auto *fmts = static_cast<const AVSampleFormat *>(configs);
    // Prefer planar float, then planar s16, then first listed.
    for (const AVSampleFormat *p = fmts; *p != AV_SAMPLE_FMT_NONE; ++p) {
        if (*p == AV_SAMPLE_FMT_FLTP)
            return *p;
    }
    for (const AVSampleFormat *p = fmts; *p != AV_SAMPLE_FMT_NONE; ++p) {
        if (*p == AV_SAMPLE_FMT_S16P)
            return *p;
    }
    return fmts[0];
}

void fillAudioFrame(AVFrame *frame, AVSampleFormat fmt, const float *interleaved, int samples)
{
    if (fmt == AV_SAMPLE_FMT_FLTP) {
        auto *left = reinterpret_cast<float *>(frame->data[0]);
        auto *right = reinterpret_cast<float *>(frame->data[1]);
        for (int i = 0; i < samples; ++i) {
            left[i] = interleaved[i * 2];
            right[i] = interleaved[i * 2 + 1];
        }
        return;
    }
    if (fmt == AV_SAMPLE_FMT_FLT) {
        auto *dst = reinterpret_cast<float *>(frame->data[0]);
        std::memcpy(dst, interleaved, static_cast<size_t>(samples) * 2 * sizeof(float));
        return;
    }
    if (fmt == AV_SAMPLE_FMT_S16P) {
        auto *left = reinterpret_cast<int16_t *>(frame->data[0]);
        auto *right = reinterpret_cast<int16_t *>(frame->data[1]);
        for (int i = 0; i < samples; ++i) {
            left[i] = static_cast<int16_t>(qBound(-1.0f, interleaved[i * 2], 1.0f) * 32767.0f);
            right[i] = static_cast<int16_t>(qBound(-1.0f, interleaved[i * 2 + 1], 1.0f) * 32767.0f);
        }
        return;
    }
    if (fmt == AV_SAMPLE_FMT_S16) {
        auto *dst = reinterpret_cast<int16_t *>(frame->data[0]);
        for (int i = 0; i < samples * 2; ++i)
            dst[i] = static_cast<int16_t>(qBound(-1.0f, interleaved[i], 1.0f) * 32767.0f);
        return;
    }
    // Fallback: treat as planar float.
    auto *left = reinterpret_cast<float *>(frame->data[0]);
    auto *right = reinterpret_cast<float *>(frame->data[1]);
    for (int i = 0; i < samples; ++i) {
        left[i] = interleaved[i * 2];
        right[i] = interleaved[i * 2 + 1];
    }
}

void applyHwRateControl(AVCodecContext *vctx, const VideoCodecDef &def, const ExportSettings &settings)
{
    const bool useCrf = settings.rateControl == QLatin1String("crf") && def.rateMode == RateMode::Crf;
    const int crf = settings.crf;
    AVCodecContext *const ctx = vctx;
    void *const priv = ctx->priv_data;

    if (useCrf) {
        ctx->bit_rate = 0;
        switch (def.hw) {
        case HwBackend::Nvenc:
            av_opt_set(priv, "rc", "vbr", 0);
            av_opt_set_int(priv, "cq", crf, 0);
            break;
        case HwBackend::Qsv:
            ctx->global_quality = crf;
            break;
        case HwBackend::Amf:
            av_opt_set(priv, "rc", "cqp", 0);
            av_opt_set_int(priv, "qp_i", crf, 0);
            av_opt_set_int(priv, "qp_p", crf, 0);
            av_opt_set_int(priv, "qp_b", crf, 0);
            break;
        case HwBackend::Vaapi:
            av_opt_set(priv, "rc_mode", "CQP", 0);
            av_opt_set_int(priv, "qp", crf, 0);
            ctx->global_quality = crf;
            break;
        case HwBackend::VideoToolbox:
            ctx->flags |= AV_CODEC_FLAG_QSCALE;
#ifndef FF_QP2LAMBDA
            ctx->global_quality = crf * 118;
#else
            ctx->global_quality = crf * FF_QP2LAMBDA;
#endif
            break;
        // Unreachable: both MediaCodec rows are RateMode::Bitrate, so useCrf is never true.
        case HwBackend::MediaCodec:
        case HwBackend::None:
            break;
        }
        return;
    }

    ctx->bit_rate = static_cast<int64_t>(qMax(100, settings.videoBitrateKbps)) * 1000;
    switch (def.hw) {
    case HwBackend::Nvenc:
        av_opt_set(priv, "rc", "vbr", 0);
        break;
    case HwBackend::Amf:
        av_opt_set(priv, "rc", "vbr_peak", 0);
        break;
    case HwBackend::Vaapi:
        av_opt_set(priv, "rc_mode", "VBR", 0);
        break;
    case HwBackend::MediaCodec:
        // Left unset, mediacodecenc passes no bitrate-mode at all and the answer is whatever the
        // SoC's encoder defaults to. Naming it makes the same timeline encode the same way on
        // every device. (pts_as_dts needs no setting: it turns itself on for max_b_frames <= 0,
        // which is how the encoder is configured.)
        av_opt_set(priv, "bitrate_mode", "vbr", 0);
        break;
    case HwBackend::Qsv:
    case HwBackend::VideoToolbox:
    case HwBackend::None:
        break;
    }
}

void applyVideoRateControl(AVCodecContext *vctx, const VideoCodecDef &def, const ExportSettings &settings)
{
    if (def.rateMode == RateMode::Lossless)
        return;
    if (def.hw != HwBackend::None) {
        applyHwRateControl(vctx, def, settings);
        return;
    }

    const QString id = QString::fromUtf8(def.id);
    const bool useCrf = settings.rateControl == QLatin1String("crf") && def.rateMode == RateMode::Crf;
    const bool useBitrate =
        settings.rateControl == QLatin1String("bitrate") && def.rateMode != RateMode::Lossless;

    if (useCrf) {
        const int crf = settings.crf;
        if (id.startsWith(QLatin1String("av1"))) {
            vctx->bit_rate = 0;
            av_opt_set_int(vctx->priv_data, "crf", crf, 0);
        } else if (id.startsWith(QLatin1String("vp8")) || id.startsWith(QLatin1String("vp9"))) {
            vctx->bit_rate = 0;
            av_opt_set_int(vctx->priv_data, "crf", crf, 0);
            av_opt_set_int(vctx->priv_data, "b", 0, 0);
        } else {
            vctx->bit_rate = 0;
            av_opt_set(vctx->priv_data, "crf", QByteArray::number(crf).constData(), 0);
        }
        return;
    }

    if (useBitrate || def.rateMode == RateMode::Bitrate) {
        vctx->bit_rate = static_cast<int64_t>(qMax(100, settings.videoBitrateKbps)) * 1000;
    }
}

void applyVideoPreset(AVCodecContext *vctx, const VideoCodecDef &def, const ExportSettings &settings)
{
    if (def.hw == HwBackend::VideoToolbox && vctx->priv_data)
        av_opt_set_int(vctx->priv_data, "allow_sw", 0, 0);

    if (!def.supportsPreset || !vctx->priv_data)
        return;
    QByteArray preset = settings.videoPreset.toUtf8();
    if (preset.isEmpty() && def.defaultPreset)
        preset = def.defaultPreset;

    if (def.hw == HwBackend::Amf) {
        av_opt_set(vctx->priv_data, "quality", preset.constData(), 0);
        return;
    }
    if (def.hw == HwBackend::Nvenc || def.hw == HwBackend::Qsv) {
        av_opt_set(vctx->priv_data, "preset", preset.constData(), 0);
        return;
    }
    if (def.hw != HwBackend::None)
        return;

    const QString id = QString::fromUtf8(def.id);
    if (id.startsWith(QLatin1String("vp8")) || id.startsWith(QLatin1String("vp9"))) {
        av_opt_set(vctx->priv_data, "cpu-used", preset.constData(), 0);
        av_opt_set(vctx->priv_data, "deadline", "good", 0);
        return;
    }
    if (id.startsWith(QLatin1String("av1"))) {
        av_opt_set(vctx->priv_data, "preset", preset.constData(), 0);
        return;
    }
    av_opt_set(vctx->priv_data, "preset", preset.constData(), 0);
}

// libav muxer name for an audio-only container id (may differ from the file extension).
QByteArray audioOnlyMuxerName(const QString &container)
{
    if (container == QLatin1String("m4a"))
        return QByteArrayLiteral("ipod");
    return container.toUtf8();
}

// Drift's SDR pipeline is BT.709 limited-range end-to-end (GPU NV12 decode + export).
void applySdrBt709Tags(AVCodecContext *vctx)
{
    if (!vctx)
        return;
    vctx->color_range = AVCOL_RANGE_MPEG;
    vctx->colorspace = AVCOL_SPC_BT709;
    vctx->color_primaries = AVCOL_PRI_BT709;
    vctx->color_trc = AVCOL_TRC_BT709;
}

void applySdrBt709Tags(AVFrame *frame)
{
    if (!frame)
        return;
    frame->color_range = AVCOL_RANGE_MPEG;
    frame->colorspace = AVCOL_SPC_BT709;
    frame->color_primaries = AVCOL_PRI_BT709;
    frame->color_trc = AVCOL_TRC_BT709;
}

// RGB (full) → YUV (limited) with BT.709 coefficients. Without this, swscale
// defaults to BT.601 and players that assume BT.709 for HD shift the hues.
bool configureExportSws(SwsContext *sws)
{
    if (!sws)
        return false;
    const int *coeff = sws_getCoefficients(SWS_CS_ITU709);
    // brightness=0, contrast=1<<16, saturation=1<<16 are identity.
    return sws_setColorspaceDetails(sws, coeff, 1 /* src full-range RGB */, coeff,
                                    0 /* dst limited-range YUV */, 0, 1 << 16, 1 << 16)
           >= 0;
}

void applyExportMetadata(AVFormatContext *fmt, const ExportSettings &settings)
{
    if (!fmt || !settings.audioOnly)
        return;
    auto setIf = [fmt](const char *key, const QString &value) {
        if (value.isEmpty())
            return;
        const QByteArray utf8 = value.toUtf8();
        av_dict_set(&fmt->metadata, key, utf8.constData(), 0);
    };
    setIf("title", settings.metadataTitle);
    setIf("artist", settings.metadataArtist);
    setIf("album", settings.metadataAlbum);
    setIf("comment", settings.metadataComment);
}

bool runAudioOnlyExport(const drift::Project &project, const ExportSettings &settings, const QString &outputPath,
                        QString *errorOut, const Exporter::ProgressFn &onProgress)
{
    const AudioCodecDef *adef = findAudioDef(settings.audioCodecId);
    if (!adef) {
        if (errorOut)
            *errorOut = QStringLiteral("Unknown audio codec selection");
        return false;
    }

    const AVCodec *acodec = findEncoder(adef->encoderNames);
    if (!acodec) {
        if (errorOut)
            *errorOut = QStringLiteral("Selected audio encoder is not available");
        return false;
    }

    const int sampleRate = project.sampleRate() > 0 ? project.sampleRate() : 48000;
    drift::TimeUs rangeStartUs = 0;
    drift::TimeUs rangeEndUs = 0;
    resolveExportRange(project, settings, &rangeStartUs, &rangeEndUs, errorOut);
    const drift::TimeUs durationUs = rangeEndUs - rangeStartUs;
    if (durationUs <= 0)
        return false;

    const int64_t totalAudioSamples =
        qMax<int64_t>(0, std::llround(static_cast<double>(durationUs) * sampleRate / 1e6));

    AVFormatContext *fmt = nullptr;
    AVCodecContext *actx = nullptr;
    AVStream *astream = nullptr;
    AVFrame *aframe = nullptr;
    AVPacket *pkt = nullptr;
    bool ok = false;
    bool cancelled = false;
    bool headerWritten = false;
    QString error;

    const QByteArray outUtf8 = outputPath.toUtf8();
    avformat_alloc_output_context2(&fmt, nullptr, nullptr, outUtf8.constData());
    if (!fmt) {
        const QString container = Exporter::preferredAudioOnlyContainer(settings.audioCodecId);
        const QByteArray muxer = audioOnlyMuxerName(container);
        avformat_alloc_output_context2(&fmt, nullptr, muxer.constData(), outUtf8.constData());
    }
    if (!fmt) {
        error = QStringLiteral("Could not determine output format");
        goto cleanup;
    }

    {
        astream = avformat_new_stream(fmt, nullptr);
        if (!astream) {
            error = QStringLiteral("Could not create output stream");
            goto cleanup;
        }

        actx = avcodec_alloc_context3(acodec);
        if (!actx) {
            error = QStringLiteral("Could not allocate audio encoder");
            goto cleanup;
        }

        const AVSampleFormat audioFmt = pickSampleFmt(acodec);
        actx->sample_fmt = audioFmt;
        actx->sample_rate = sampleRate;
        av_channel_layout_default(&actx->ch_layout, 2);
        if (!adef->lossless)
            actx->bit_rate = static_cast<int64_t>(qMax(32, settings.audioBitrateKbps)) * 1000;
        actx->time_base = AVRational{1, sampleRate};
        if (fmt->oformat->flags & AVFMT_GLOBALHEADER)
            actx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

        if (avcodec_open2(actx, acodec, nullptr) < 0) {
            error = QStringLiteral("Could not open the audio encoder");
            goto cleanup;
        }

        avcodec_parameters_from_context(astream->codecpar, actx);
        astream->time_base = actx->time_base;

        const int frameSize = actx->frame_size > 0 ? actx->frame_size : 1024;

        if (!(fmt->oformat->flags & AVFMT_NOFILE)) {
            if (avio_open(&fmt->pb, outUtf8.constData(), AVIO_FLAG_WRITE) < 0) {
                error = QStringLiteral("Could not open the output file");
                goto cleanup;
            }
        }

        applyExportMetadata(fmt, settings);

        if (avformat_write_header(fmt, nullptr) < 0) {
            error = QStringLiteral("Could not write the file header");
            goto cleanup;
        }
        headerWritten = true;

        pkt = av_packet_alloc();
        aframe = av_frame_alloc();
        if (!pkt || !aframe) {
            error = QStringLiteral("Out of memory");
            goto cleanup;
        }

        aframe->format = audioFmt;
        aframe->sample_rate = sampleRate;
        av_channel_layout_copy(&aframe->ch_layout, &actx->ch_layout);
        aframe->nb_samples = frameSize;
        if (av_frame_get_buffer(aframe, 0) < 0) {
            error = QStringLiteral("Could not allocate the audio frame");
            goto cleanup;
        }

        AudioMixer mixer;
        mixer.setProject(&project);

        std::vector<float> audioBuffer;
        int64_t audioSamplesGenerated = 0;
        int64_t audioPts = 0;

        auto flushAudioFrames = [&](bool drainAll) -> bool {
            while (static_cast<int64_t>(audioBuffer.size()) >= static_cast<int64_t>(frameSize) * 2) {
                if (av_frame_make_writable(aframe) < 0) {
                    error = QStringLiteral("Audio frame not writable");
                    return false;
                }
                aframe->nb_samples = frameSize;
                fillAudioFrame(aframe, audioFmt, audioBuffer.data(), frameSize);
                aframe->pts = audioPts;
                audioPts += frameSize;
                if (!encodeWriteFrame(fmt, actx, astream, aframe, pkt, &error))
                    return false;
                audioBuffer.erase(audioBuffer.begin(), audioBuffer.begin() + frameSize * 2);
            }
            if (drainAll && !audioBuffer.empty()) {
                const int remaining = static_cast<int>(audioBuffer.size() / 2);
                if (av_frame_make_writable(aframe) < 0) {
                    error = QStringLiteral("Audio frame not writable");
                    return false;
                }
                aframe->nb_samples = remaining;
                fillAudioFrame(aframe, audioFmt, audioBuffer.data(), remaining);
                aframe->pts = audioPts;
                audioPts += remaining;
                if (!encodeWriteFrame(fmt, actx, astream, aframe, pkt, &error))
                    return false;
                audioBuffer.clear();
            }
            return true;
        };

        while (audioSamplesGenerated < totalAudioSamples) {
            if (onProgress && totalAudioSamples > 0
                && !onProgress(static_cast<double>(audioSamplesGenerated) / totalAudioSamples)) {
                cancelled = true;
                break;
            }

            const int64_t chunkSamples = qMin<int64_t>(frameSize * 8, totalAudioSamples - audioSamplesGenerated);
            const size_t base = audioBuffer.size();
            audioBuffer.resize(base + static_cast<size_t>(chunkSamples) * 2);
            const drift::TimeUs audioStartUs = rangeStartUs
                + static_cast<drift::TimeUs>((audioSamplesGenerated * drift::kUsPerSecond) / sampleRate);
            mixer.mix(audioStartUs, static_cast<int>(chunkSamples), sampleRate, audioBuffer.data() + base);
            audioSamplesGenerated += chunkSamples;
            if (!flushAudioFrames(false))
                goto cleanup;
        }

        if (!cancelled) {
            if (!flushAudioFrames(true))
                goto cleanup;
            if (!encodeWriteFrame(fmt, actx, astream, nullptr, pkt, &error))
                goto cleanup;
            if (av_write_trailer(fmt) < 0) {
                error = QStringLiteral("Could not finalize the file");
                goto cleanup;
            }
            ok = true;
        }
    }

cleanup:
    if (aframe)
        av_frame_free(&aframe);
    if (pkt)
        av_packet_free(&pkt);
    if (actx)
        avcodec_free_context(&actx);
    if (fmt) {
        if (headerWritten && !ok && fmt->pb)
            av_write_trailer(fmt);
        if (fmt->pb && !(fmt->oformat->flags & AVFMT_NOFILE))
            avio_closep(&fmt->pb);
        avformat_free_context(fmt);
    }

    if (!ok && QFile::exists(outputPath))
        QFile::remove(outputPath);

    if (!ok && errorOut) {
        *errorOut = cancelled ? QStringLiteral("Export cancelled")
                              : (error.isEmpty() ? QStringLiteral("Export failed") : error);
    }
    return ok;
}

constexpr drift::TimeUs kMaxGifDurationUs = 60 * drift::kUsPerSecond;

AVFrame *rgbaImageToFrame(const QImage &image, int64_t pts)
{
    if (image.isNull())
        return nullptr;

    const QImage rgba = image.convertToFormat(QImage::Format_RGBA8888);
    AVFrame *frame = av_frame_alloc();
    if (!frame)
        return nullptr;

    frame->format = AV_PIX_FMT_RGBA;
    frame->width = rgba.width();
    frame->height = rgba.height();
    if (av_frame_get_buffer(frame, 32) < 0) {
        av_frame_free(&frame);
        return nullptr;
    }

    for (int y = 0; y < rgba.height(); ++y) {
        std::memcpy(frame->data[0] + y * frame->linesize[0], rgba.constScanLine(y),
                    static_cast<size_t>(rgba.bytesPerLine()));
    }

    frame->pts = pts;
    return frame;
}

bool setupGifPaletteGraph(AVFilterGraph **graphOut, AVFilterContext **srcOut, AVFilterContext **sinkOut,
                          int inW, int inH, int outW, int outH, AVRational timeBase, QString *errorOut)
{
    AVFilterGraph *graph = avfilter_graph_alloc();
    if (!graph) {
        if (errorOut)
            *errorOut = QStringLiteral("Out of memory");
        return false;
    }

    const AVFilter *buffersrc = avfilter_get_by_name("buffer");
    const AVFilter *buffersink = avfilter_get_by_name("buffersink");
    if (!buffersrc || !buffersink) {
        avfilter_graph_free(&graph);
        if (errorOut)
            *errorOut = QStringLiteral("GIF palette filters are unavailable");
        return false;
    }

    AVFilterContext *srcCtx = nullptr;
    AVFilterContext *sinkCtx = nullptr;
    const QByteArray args =
        QString::asprintf("video_size=%dx%d:pix_fmt=rgba:time_base=%d/%d:pixel_aspect=1/1", inW, inH,
                          timeBase.num, timeBase.den)
            .toUtf8();
    if (avfilter_graph_create_filter(&srcCtx, buffersrc, "in", args.constData(), nullptr, graph) < 0
        || avfilter_graph_create_filter(&sinkCtx, buffersink, "out", nullptr, nullptr, graph) < 0) {
        avfilter_graph_free(&graph);
        if (errorOut)
            *errorOut = QStringLiteral("Could not create GIF palette filters");
        return false;
    }

    AVFilterInOut *outputs = avfilter_inout_alloc();
    AVFilterInOut *inputs = avfilter_inout_alloc();
    outputs->name = av_strdup("in");
    outputs->filter_ctx = srcCtx;
    outputs->pad_idx = 0;
    outputs->next = nullptr;

    inputs->name = av_strdup("out");
    inputs->filter_ctx = sinkCtx;
    inputs->pad_idx = 0;
    inputs->next = nullptr;

    const QString graphDesc =
        QStringLiteral("[in] scale=%1:%2:flags=lanczos,split=2[s0][s1];"
                       "[s0]palettegen=stats_mode=full:reserve_transparent=1[p];"
                       "[s1][p]paletteuse=dither=bayer:bayer_scale=3[out]")
            .arg(outW)
            .arg(outH);
    const QByteArray graphUtf8 = graphDesc.toUtf8();
    if (avfilter_graph_parse_ptr(graph, graphUtf8.constData(), &inputs, &outputs, nullptr) < 0
        || avfilter_graph_config(graph, nullptr) < 0) {
        avfilter_inout_free(&inputs);
        avfilter_inout_free(&outputs);
        avfilter_graph_free(&graph);
        if (errorOut)
            *errorOut = QStringLiteral("Could not configure GIF palette filters");
        return false;
    }

    avfilter_inout_free(&inputs);
    avfilter_inout_free(&outputs);
    *graphOut = graph;
    *srcOut = srcCtx;
    *sinkOut = sinkCtx;
    return true;
}

bool runGifExport(const drift::Project &project, const ExportSettings &settings, const QString &outputPath,
                  QString *errorOut, const Exporter::ProgressFn &onProgress)
{
    const AVCodec *codec = avcodec_find_encoder(AV_CODEC_ID_GIF);
    if (!codec) {
        if (errorOut)
            *errorOut = QStringLiteral("GIF encoder is not available");
        return false;
    }

    const int projW = project.width();
    const int projH = project.height();
    if (projW <= 0 || projH <= 0) {
        if (errorOut)
            *errorOut = QStringLiteral("Invalid project resolution");
        return false;
    }

    int outW = 0;
    int outH = 0;
    scaleSize(projW, projH, settings.targetHeight, outW, outH);

    AVRational frameRate{qMax(1, project.fps()), 1};
    if (settings.fpsNum > 0 && settings.fpsDen > 0)
        frameRate = AVRational{settings.fpsNum, settings.fpsDen};
    av_reduce(&frameRate.num, &frameRate.den, frameRate.num, frameRate.den, INT_MAX);
    const AVRational frameTb{frameRate.den, frameRate.num};
    const double fpsValue = av_q2d(frameRate);

    drift::TimeUs rangeStartUs = 0;
    drift::TimeUs rangeEndUs = 0;
    resolveExportRange(project, settings, &rangeStartUs, &rangeEndUs, errorOut);
    const drift::TimeUs durationUs = rangeEndUs - rangeStartUs;
    if (durationUs <= 0)
        return false;
    if (durationUs > kMaxGifDurationUs) {
        if (errorOut)
            *errorOut = QStringLiteral("GIF export is limited to 60 seconds");
        return false;
    }

    const int64_t totalFrames =
        qMax<int64_t>(1, std::llround(static_cast<double>(durationUs) * fpsValue / 1e6));

    AVFilterGraph *filterGraph = nullptr;
    AVFilterContext *filterSrc = nullptr;
    AVFilterContext *filterSink = nullptr;
    if (!setupGifPaletteGraph(&filterGraph, &filterSrc, &filterSink, projW, projH, outW, outH, frameTb,
                              errorOut)) {
        return false;
    }

    AVFormatContext *fmt = nullptr;
    AVCodecContext *vctx = nullptr;
    AVStream *vstream = nullptr;
    AVPacket *pkt = nullptr;
    bool ok = false;
    bool cancelled = false;
    bool headerWritten = false;
    QString error;

    const QByteArray outUtf8 = outputPath.toUtf8();
    avformat_alloc_output_context2(&fmt, nullptr, "gif", outUtf8.constData());
    if (!fmt) {
        error = QStringLiteral("Could not create GIF output");
        goto cleanup;
    }

    {
        vstream = avformat_new_stream(fmt, nullptr);
        if (!vstream) {
            error = QStringLiteral("Could not create output stream");
            goto cleanup;
        }

        vctx = avcodec_alloc_context3(codec);
        if (!vctx) {
            error = QStringLiteral("Could not allocate GIF encoder");
            goto cleanup;
        }

        vctx->width = outW;
        vctx->height = outH;
        vctx->pix_fmt = AV_PIX_FMT_PAL8;
        vctx->time_base = frameTb;
        vctx->framerate = frameRate;

        if (avcodec_open2(vctx, codec, nullptr) < 0) {
            error = QStringLiteral("Could not open GIF encoder");
            goto cleanup;
        }

        avcodec_parameters_from_context(vstream->codecpar, vctx);
        vstream->time_base = vctx->time_base;

        if (!(fmt->oformat->flags & AVFMT_NOFILE)) {
            if (avio_open(&fmt->pb, outUtf8.constData(), AVIO_FLAG_WRITE) < 0) {
                error = QStringLiteral("Could not open the output file");
                goto cleanup;
            }
        }

        if (avformat_write_header(fmt, nullptr) < 0) {
            error = QStringLiteral("Could not write the file header");
            goto cleanup;
        }
        headerWritten = true;

        pkt = av_packet_alloc();
        if (!pkt) {
            error = QStringLiteral("Out of memory");
            goto cleanup;
        }

        FrameCompositor compositor;
        compositor.setProject(&project);

        for (int64_t i = 0; i < totalFrames; ++i) {
            if (onProgress && !onProgress(static_cast<double>(i) / (totalFrames + 1))) {
                cancelled = true;
                break;
            }

            const drift::TimeUs t = rangeStartUs + static_cast<drift::TimeUs>(
                std::llround(static_cast<double>(i) * 1e6 * frameRate.den / frameRate.num));
            QImage img = compositor.compositeAt(t);
            if (img.isNull()) {
                img = QImage(projW, projH, QImage::Format_RGBA8888);
                img.fill(Qt::black);
            } else if (img.format() != QImage::Format_RGBA8888) {
                img = img.convertToFormat(QImage::Format_RGBA8888);
            }
            if (img.width() != projW || img.height() != projH)
                img = img.scaled(projW, projH, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);

            AVFrame *srcFrame = rgbaImageToFrame(img, i);
            if (!srcFrame) {
                error = QStringLiteral("Could not prepare a GIF frame");
                goto cleanup;
            }

            if (av_buffersrc_add_frame_flags(filterSrc, srcFrame, AV_BUFFERSRC_FLAG_KEEP_REF) < 0) {
                av_frame_free(&srcFrame);
                error = QStringLiteral("GIF palette filter rejected a frame");
                goto cleanup;
            }
            av_frame_free(&srcFrame);
        }

        if (!cancelled) {
            if (av_buffersrc_add_frame_flags(filterSrc, nullptr, 0) < 0) {
                error = QStringLiteral("Could not finalize GIF palette");
                goto cleanup;
            }

            int64_t outPts = 0;
            while (true) {
                AVFrame *palFrame = av_frame_alloc();
                if (!palFrame) {
                    error = QStringLiteral("Out of memory");
                    goto cleanup;
                }

                const int rc = av_buffersink_get_frame(filterSink, palFrame);
                if (rc == AVERROR(EAGAIN) || rc == AVERROR_EOF) {
                    av_frame_free(&palFrame);
                    break;
                }
                if (rc < 0) {
                    av_frame_free(&palFrame);
                    error = QStringLiteral("Failed to read a GIF frame");
                    goto cleanup;
                }

                palFrame->pts = outPts++;
                if (!encodeWriteFrame(fmt, vctx, vstream, palFrame, pkt, &error)) {
                    av_frame_free(&palFrame);
                    goto cleanup;
                }
                av_frame_free(&palFrame);

                if (onProgress)
                    onProgress(static_cast<double>(outPts) / (totalFrames + 1));
            }

            if (!encodeWriteFrame(fmt, vctx, vstream, nullptr, pkt, &error))
                goto cleanup;
            if (av_write_trailer(fmt) < 0) {
                error = QStringLiteral("Could not finalize the GIF");
                goto cleanup;
            }
            ok = true;
        }
    }

cleanup:
    if (pkt)
        av_packet_free(&pkt);
    if (vctx)
        avcodec_free_context(&vctx);
    if (filterGraph)
        avfilter_graph_free(&filterGraph);
    if (fmt) {
        if (headerWritten && !ok && fmt->pb)
            av_write_trailer(fmt);
        if (fmt->pb && !(fmt->oformat->flags & AVFMT_NOFILE))
            avio_closep(&fmt->pb);
        avformat_free_context(fmt);
    }

    if (!ok && QFile::exists(outputPath))
        QFile::remove(outputPath);

    if (!ok && errorOut) {
        *errorOut = cancelled ? QStringLiteral("Export cancelled")
                              : (error.isEmpty() ? QStringLiteral("GIF export failed") : error);
    }
    return ok;
}

#ifdef Q_OS_ANDROID
// avio has no content:// protocol, and a document created by ACTION_CREATE_DOCUMENT has no path to
// open instead — so the encode runs into app storage and the result is streamed into the document
// afterwards. The staging name carries the suffix from the document's display name because that is
// the only place the container the user asked for is still legible: the URI itself has none, and
// avformat picks the muxer by extension.
bool runToDocument(const drift::Project &project, const ExportSettings &settings, const QUrl &target,
                   QString *errorOut, const Exporter::ProgressFn &onProgress)
{
    QString suffix = QFileInfo(AndroidUri::displayName(target)).suffix();
    if (suffix.isEmpty()) {
        suffix = settings.gifExport
                     ? QStringLiteral("gif")
                     : settings.audioOnly
                           ? Exporter::defaultSuffix(
                                 Exporter::preferredAudioOnlyContainer(settings.audioCodecId), true)
                           : Exporter::defaultSuffix(
                                 Exporter::preferredContainer(settings.videoCodecId,
                                                              settings.audioCodecId),
                                 false);
    }

    QTemporaryFile staging(QDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation))
                               .filePath(QStringLiteral("export-XXXXXX.") + suffix));
    if (!staging.open()) {
        if (errorOut)
            *errorOut = QStringLiteral("Could not stage the export");
        return false;
    }
    const QString stagingPath = staging.fileName();
    staging.close();

    if (!Exporter::run(project, settings, stagingPath, errorOut, onProgress))
        return false;

    QFile encoded(stagingPath);
    std::unique_ptr<QFile> sink = AndroidUri::openForWrite(target);
    if (!sink || !encoded.open(QIODevice::ReadOnly)) {
        if (errorOut)
            *errorOut = QStringLiteral("Could not write to the chosen location");
        return false;
    }

    std::vector<char> buffer(1 << 16);
    while (!encoded.atEnd()) {
        const qint64 read = encoded.read(buffer.data(), static_cast<qint64>(buffer.size()));
        if (read <= 0 || sink->write(buffer.data(), read) != read) {
            if (errorOut)
                *errorOut = QStringLiteral("Could not write to the chosen location");
            return false;
        }
    }
    if (!sink->flush()) {
        if (errorOut)
            *errorOut = QStringLiteral("Could not write to the chosen location");
        return false;
    }
    return true;
}
#endif

} // namespace

Exporter::BackgroundHold::BackgroundHold(const QString &title, bool cancellable)
{
#ifdef Q_OS_ANDROID
    if (g_backgroundHolds.fetch_add(1, std::memory_order_acq_rel) == 0) {
        g_notifiedPercent.store(-1, std::memory_order_relaxed);
        g_serviceCancelRequested.store(false, std::memory_order_relaxed);
        startExportService(title, cancellable);
    }
    drift::android::acquireKeepScreenOn();
#else
    Q_UNUSED(title);
    Q_UNUSED(cancellable);
#endif
}

bool Exporter::BackgroundHold::cancelRequested()
{
#ifdef Q_OS_ANDROID
    return g_serviceCancelRequested.load(std::memory_order_relaxed);
#else
    return false;
#endif
}

Exporter::BackgroundHold::~BackgroundHold()
{
#ifdef Q_OS_ANDROID
    drift::android::releaseKeepScreenOn();
    if (g_backgroundHolds.fetch_sub(1, std::memory_order_acq_rel) == 1)
        stopExportService();
#endif
}

void Exporter::BackgroundHold::setPercent(int percent)
{
#ifdef Q_OS_ANDROID
    percent = qBound(0, percent, 100);
    if (g_notifiedPercent.exchange(percent, std::memory_order_relaxed) == percent)
        return;

    QJniObject context = QNativeInterface::QAndroidApplication::context();
    if (!context.isValid())
        return;
    QJniObject::callStaticMethod<void>(kExportServiceClass, "setPercent",
                                       "(Landroid/content/Context;I)V", context.object(), percent);
    QJniEnvironment().checkAndClearExceptions();
#else
    Q_UNUSED(percent);
#endif
}

const QList<ExportScalePreset> &Exporter::scalePresets()
{
    static const QList<ExportScalePreset> kPresets = {
        {QStringLiteral("source"), QStringLiteral("Same as project"), 0, 16000},
        {QStringLiteral("1080p"), QStringLiteral("1080p"), 1080, 12000},
        {QStringLiteral("720p"), QStringLiteral("720p"), 720, 8000},
        {QStringLiteral("480p"), QStringLiteral("480p"), 480, 4000},
    };
    return kPresets;
}

const ExportScalePreset *Exporter::scalePresetById(const QString &id)
{
    for (const ExportScalePreset &preset : scalePresets()) {
        if (preset.id == id)
            return &preset;
    }
    return nullptr;
}

QVariantList Exporter::videoCodecs()
{
    QVariantList out;
    for (const VideoCodecDef &def : kVideoCodecs) {
        if (!hwBackendOnThisOs(def.hw))
            continue;
        out.append(videoDefToMap(def));
    }
    return out;
}

QVariantList Exporter::audioCodecs()
{
    QVariantList out;
    for (const AudioCodecDef &def : kAudioCodecs)
        out.append(audioDefToMap(def));
    return out;
}

QVariantMap Exporter::videoCodecById(const QString &id)
{
    if (const VideoCodecDef *def = findVideoDef(id))
        return videoDefToMap(*def);
    return {};
}

QVariantMap Exporter::audioCodecById(const QString &id)
{
    if (const AudioCodecDef *def = findAudioDef(id))
        return audioDefToMap(*def);
    return {};
}

QString Exporter::preferredContainer(const QString &videoCodecId, const QString &audioCodecId)
{
    const VideoCodecDef *vdef = findVideoDef(videoCodecId);
    const AudioCodecDef *adef = findAudioDef(audioCodecId);
    const QString vCont = vdef ? QString::fromUtf8(vdef->preferredContainer) : QStringLiteral("mkv");
    const QString aFam = adef ? QString::fromUtf8(adef->containerFamily) : QStringLiteral("mkv");

    // Lossless / awkward video always prefers mkv.
    if (vCont == QLatin1String("mkv"))
        return QStringLiteral("mkv");

    // WebM video with Opus (or another webm-family audio) → webm; else mkv.
    if (vCont == QLatin1String("webm")) {
        if (aFam == QLatin1String("webm") || audioCodecId == QLatin1String("opus")
            || audioCodecId == QLatin1String("vorbis"))
            return QStringLiteral("webm");
        return QStringLiteral("mkv");
    }

    // MP4-friendly video: need MP4-friendly audio.
    if (vCont == QLatin1String("mp4")) {
        if (aFam == QLatin1String("mp4"))
            return QStringLiteral("mp4");
        if (aFam == QLatin1String("webm"))
            return QStringLiteral("mkv"); // e.g. H.264 + Opus
        return QStringLiteral("mkv");
    }

    return QStringLiteral("mkv");
}

QString Exporter::preferredAudioOnlyContainer(const QString &audioCodecId)
{
    if (audioCodecId == QLatin1String("aac"))
        return QStringLiteral("m4a");
    if (audioCodecId == QLatin1String("mp3"))
        return QStringLiteral("mp3");
    if (audioCodecId == QLatin1String("opus"))
        return QStringLiteral("opus");
    if (audioCodecId == QLatin1String("ac3"))
        return QStringLiteral("ac3");
    if (audioCodecId == QLatin1String("flac"))
        return QStringLiteral("flac");
    return QStringLiteral("m4a");
}

QStringList Exporter::saveFilters(const QString &container, bool audioOnly)
{
    if (audioOnly) {
        if (container == QLatin1String("m4a"))
            return {QStringLiteral("AAC audio (*.m4a)")};
        if (container == QLatin1String("mp3"))
            return {QStringLiteral("MP3 audio (*.mp3)")};
        if (container == QLatin1String("opus"))
            return {QStringLiteral("Opus audio (*.opus)")};
        if (container == QLatin1String("ac3"))
            return {QStringLiteral("AC3 audio (*.ac3)")};
        if (container == QLatin1String("flac"))
            return {QStringLiteral("FLAC audio (*.flac)")};
        return {QStringLiteral("Audio files (*.*)")};
    }
    if (container == QLatin1String("webm"))
        return {QStringLiteral("WebM video (*.webm)")};
    if (container == QLatin1String("gif"))
        return {QStringLiteral("GIF image (*.gif)")};
    if (container == QLatin1String("mkv"))
        return {QStringLiteral("Matroska video (*.mkv)"), QStringLiteral("MP4 video (*.mp4)")};
    return {QStringLiteral("MP4 video (*.mp4)"), QStringLiteral("Matroska video (*.mkv)")};
}

QString Exporter::defaultSuffix(const QString &container, bool audioOnly)
{
    if (audioOnly) {
        if (container == QLatin1String("m4a"))
            return QStringLiteral("m4a");
        if (container == QLatin1String("mp3"))
            return QStringLiteral("mp3");
        if (container == QLatin1String("opus"))
            return QStringLiteral("opus");
        if (container == QLatin1String("ac3"))
            return QStringLiteral("ac3");
        if (container == QLatin1String("flac"))
            return QStringLiteral("flac");
        return QStringLiteral("m4a");
    }
    if (container == QLatin1String("webm"))
        return QStringLiteral("webm");
    if (container == QLatin1String("gif"))
        return QStringLiteral("gif");
    if (container == QLatin1String("mkv"))
        return QStringLiteral("mkv");
    return QStringLiteral("mp4");
}

QVariantList Exporter::scaleOptions(int projectWidth, int projectHeight)
{
    QVariantList out;
    if (projectWidth <= 0 || projectHeight <= 0)
        return out;

    for (const ExportScalePreset &preset : scalePresets()) {
        // Skip downscale targets that would upscale.
        if (preset.targetHeight > 0 && projectHeight <= preset.targetHeight)
            continue;

        int outW = 0;
        int outH = 0;
        scaleSize(projectWidth, projectHeight, preset.targetHeight, outW, outH);

        const QString label = QStringLiteral("%1 · %2×%3").arg(preset.label).arg(outW).arg(outH);

        out.append(QVariantMap{
            {QStringLiteral("id"), preset.id},
            {QStringLiteral("label"), label},
            {QStringLiteral("targetHeight"), preset.targetHeight},
            {QStringLiteral("width"), outW},
            {QStringLiteral("height"), outH},
            {QStringLiteral("videoBitrateKbps"), preset.videoBitrateKbps},
        });
    }

    // Always include source if somehow filtered out (shouldn't happen).
    if (out.isEmpty()) {
        int outW = 0;
        int outH = 0;
        scaleSize(projectWidth, projectHeight, 0, outW, outH);
        out.append(QVariantMap{
            {QStringLiteral("id"), QStringLiteral("source")},
            {QStringLiteral("label"), QStringLiteral("Same as project · %1×%2").arg(outW).arg(outH)},
            {QStringLiteral("targetHeight"), 0},
            {QStringLiteral("width"), outW},
            {QStringLiteral("height"), outH},
            {QStringLiteral("videoBitrateKbps"), 16000},
        });
    }
    return out;
}

QVariantList Exporter::frameRateOptions(int projectFps)
{
    struct FrameRatePreset
    {
        const char *id;
        int num;
        int den;
        const char *label;
    };

    // NTSC rates carry a 1001 denominator; an integer fps could not express them,
    // which is why ExportSettings keeps the rate rational.
    static const FrameRatePreset presets[] = {
        {"23.976", 24000, 1001, "23.976 fps (NTSC film)"},
        {"24", 24, 1, "24 fps"},
        {"25", 25, 1, "25 fps (PAL)"},
        {"29.97", 30000, 1001, "29.97 fps (NTSC)"},
        {"30", 30, 1, "30 fps"},
        {"48", 48, 1, "48 fps"},
        {"50", 50, 1, "50 fps"},
        {"59.94", 60000, 1001, "59.94 fps (NTSC)"},
        {"60", 60, 1, "60 fps"},
        {"120", 120, 1, "120 fps"},
        {"240", 240, 1, "240 fps"},
    };

    QVariantList out;
    // 0/1 means "whatever the project is set to", so the export follows later
    // project-setup changes instead of pinning the rate at dialog time.
    out.append(QVariantMap{
        {QStringLiteral("id"), QStringLiteral("project")},
        {QStringLiteral("label"), QStringLiteral("Same as project (%1 fps)").arg(qMax(1, projectFps))},
        {QStringLiteral("fpsNum"), 0},
        {QStringLiteral("fpsDen"), 1},
    });
    for (const FrameRatePreset &preset : presets) {
        out.append(QVariantMap{
            {QStringLiteral("id"), QString::fromLatin1(preset.id)},
            {QStringLiteral("label"), QString::fromLatin1(preset.label)},
            {QStringLiteral("fpsNum"), preset.num},
            {QStringLiteral("fpsDen"), preset.den},
        });
    }
    return out;
}

bool Exporter::gifAvailable()
{
    return avcodec_find_encoder(AV_CODEC_ID_GIF) != nullptr;
}

ExportSettings Exporter::defaultSettings()
{
    ExportSettings s;
    // Prefer first available CRF codec starting at h264.
    const QStringList prefer = {QStringLiteral("h264"), QStringLiteral("h265"), QStringLiteral("vp9"),
                                QStringLiteral("av1_svt")};
    for (const QString &id : prefer) {
        const QVariantMap m = videoCodecById(id);
        if (m.value(QStringLiteral("available")).toBool()) {
            s.videoCodecId = id;
            s.crf = m.value(QStringLiteral("defaultCrf"), 18).toInt();
            s.videoPreset = m.value(QStringLiteral("defaultPreset")).toString();
            if (s.videoPreset.isEmpty())
                s.videoPreset = QStringLiteral("medium");
            break;
        }
    }
    if (audioCodecById(QStringLiteral("aac")).value(QStringLiteral("available")).toBool())
        s.audioCodecId = QStringLiteral("aac");
    else {
        for (const QVariant &v : audioCodecs()) {
            const QVariantMap m = v.toMap();
            if (m.value(QStringLiteral("available")).toBool()) {
                s.audioCodecId = m.value(QStringLiteral("id")).toString();
                break;
            }
        }
    }
    s.rateControl = QStringLiteral("crf");
    return s;
}

ExportSettings Exporter::settingsFromMap(const QVariantMap &map)
{
    ExportSettings s = defaultSettings();
    if (map.contains(QStringLiteral("targetHeight")))
        s.targetHeight = map.value(QStringLiteral("targetHeight")).toInt();
    if (map.contains(QStringLiteral("fpsNum")))
        s.fpsNum = map.value(QStringLiteral("fpsNum")).toInt();
    if (map.contains(QStringLiteral("fpsDen")))
        s.fpsDen = map.value(QStringLiteral("fpsDen")).toInt();
    // Anything nonsensical falls back to the project rate rather than producing a
    // broken time_base the muxer would reject.
    if (s.fpsNum <= 0 || s.fpsDen <= 0) {
        s.fpsNum = 0;
        s.fpsDen = 1;
    } else if (static_cast<int64_t>(s.fpsNum) > static_cast<int64_t>(kMaxExportFps) * s.fpsDen) {
        s.fpsNum = kMaxExportFps;
        s.fpsDen = 1;
    }
    if (map.contains(QStringLiteral("videoCodecId")))
        s.videoCodecId = map.value(QStringLiteral("videoCodecId")).toString();
    if (map.contains(QStringLiteral("rateControl")))
        s.rateControl = map.value(QStringLiteral("rateControl")).toString();
    if (map.contains(QStringLiteral("crf")))
        s.crf = map.value(QStringLiteral("crf")).toInt();
    if (map.contains(QStringLiteral("videoBitrateKbps")))
        s.videoBitrateKbps = map.value(QStringLiteral("videoBitrateKbps")).toInt();
    if (map.contains(QStringLiteral("videoPreset")))
        s.videoPreset = map.value(QStringLiteral("videoPreset")).toString();
    if (map.contains(QStringLiteral("audioCodecId")))
        s.audioCodecId = map.value(QStringLiteral("audioCodecId")).toString();
    if (map.contains(QStringLiteral("audioBitrateKbps")))
        s.audioBitrateKbps = map.value(QStringLiteral("audioBitrateKbps")).toInt();
    if (map.contains(QStringLiteral("audioOnly")))
        s.audioOnly = map.value(QStringLiteral("audioOnly")).toBool();
    if (map.contains(QStringLiteral("gifExport")))
        s.gifExport = map.value(QStringLiteral("gifExport")).toBool();
    if (map.contains(QStringLiteral("metadataTitle")))
        s.metadataTitle = map.value(QStringLiteral("metadataTitle")).toString();
    if (map.contains(QStringLiteral("metadataArtist")))
        s.metadataArtist = map.value(QStringLiteral("metadataArtist")).toString();
    if (map.contains(QStringLiteral("metadataAlbum")))
        s.metadataAlbum = map.value(QStringLiteral("metadataAlbum")).toString();
    if (map.contains(QStringLiteral("metadataComment")))
        s.metadataComment = map.value(QStringLiteral("metadataComment")).toString();
    if (map.contains(QStringLiteral("startUs")))
        s.startUs = static_cast<drift::TimeUs>(map.value(QStringLiteral("startUs")).toDouble());
    if (map.contains(QStringLiteral("endUs")))
        s.endUs = static_cast<drift::TimeUs>(map.value(QStringLiteral("endUs")).toDouble());
    return s;
}

bool Exporter::run(const drift::Project &project, const ExportSettings &settings, const QString &outputPath,
                   QString *errorOut, const ProgressFn &onProgress)
{
#ifdef Q_OS_ANDROID
    if (const QUrl target(outputPath); AndroidUri::isContentUri(target))
        return runToDocument(project, settings, target, errorOut, onProgress);
#endif

    if (settings.gifExport)
        return runGifExport(project, settings, outputPath, errorOut, onProgress);
    if (settings.audioOnly)
        return runAudioOnlyExport(project, settings, outputPath, errorOut, onProgress);

    const int projW = project.width();
    const int projH = project.height();
    if (projW <= 0 || projH <= 0) {
        if (errorOut)
            *errorOut = QStringLiteral("Invalid project resolution");
        return false;
    }

    const VideoCodecDef *vdef = findVideoDef(settings.videoCodecId);
    const AudioCodecDef *adef = findAudioDef(settings.audioCodecId);
    if (!vdef || !adef) {
        if (errorOut)
            *errorOut = QStringLiteral("Unknown codec selection");
        return false;
    }

    const AVCodec *vcodec = findEncoder(vdef->encoderNames);
    const AVCodec *acodec = findEncoder(adef->encoderNames);
    if (vdef->hw != HwBackend::None && !hwBackendOnThisOs(vdef->hw)) {
        if (errorOut)
            *errorOut = QStringLiteral("The %1 encoder is not available on this platform.")
                            .arg(hwEncoderLabel(*vdef));
        return false;
    }
    if (!vcodec || !acodec) {
        if (errorOut) {
            if (vdef->hw != HwBackend::None && !vcodec)
                *errorOut = QStringLiteral("The %1 encoder is not available in this FFmpeg build.")
                                .arg(hwEncoderLabel(*vdef));
            else
                *errorOut = QStringLiteral("Selected encoder is not available");
        }
        return false;
    }

    int outW = 0;
    int outH = 0;
    scaleSize(projW, projH, settings.targetHeight, outW, outH);

    // The export rate is independent of the project rate: sampling the timeline
    // faster pulls extra frames out of the source wherever it has them, which is
    // what makes slowed clips look smooth.
    AVRational frameRate{qMax(1, project.fps()), 1};
    if (settings.fpsNum > 0 && settings.fpsDen > 0)
        frameRate = AVRational{settings.fpsNum, settings.fpsDen};
    av_reduce(&frameRate.num, &frameRate.den, frameRate.num, frameRate.den, INT_MAX);
    const AVRational frameTb{frameRate.den, frameRate.num};
    const double fpsValue = av_q2d(frameRate);

    const int sampleRate = project.sampleRate() > 0 ? project.sampleRate() : 48000;
    drift::TimeUs rangeStartUs = 0;
    drift::TimeUs rangeEndUs = 0;
    resolveExportRange(project, settings, &rangeStartUs, &rangeEndUs, errorOut);
    const drift::TimeUs durationUs = rangeEndUs - rangeStartUs;
    if (durationUs <= 0)
        return false;

    const int64_t totalFrames =
        qMax<int64_t>(1, std::llround(static_cast<double>(durationUs) * fpsValue / 1e6));
    const int64_t totalAudioSamples =
        qMax<int64_t>(0, std::llround(static_cast<double>(durationUs) * sampleRate / 1e6));

    AVFormatContext *fmt = nullptr;
    AVCodecContext *vctx = nullptr;
    AVCodecContext *actx = nullptr;
    AVStream *vstream = nullptr;
    AVStream *astream = nullptr;
    AVFrame *vframe = nullptr;
    AVFrame *hwframe = nullptr;
    AVFrame *aframe = nullptr;
    AVPacket *pkt = nullptr;
    AVBufferRef *hwDeviceCtx = nullptr;
    SwsContext *sws = nullptr;
    bool ok = false;
    bool cancelled = false;
    bool headerWritten = false;
    QString error;

    // Encoded straight into the chosen path: a file handed over by the documents portal cannot be
    // renamed or replaced, so writing a sibling ".part" and moving it into place would strand the
    // result next to the file the user picked.
    const QByteArray outUtf8 = outputPath.toUtf8();

    avformat_alloc_output_context2(&fmt, nullptr, nullptr, outUtf8.constData());
    if (!fmt) {
        // Portal pickers hand back whatever name the user typed, extension or not; when there is
        // nothing to guess from, fall back to the container this codec pair asks for.
        const QString container = preferredContainer(settings.videoCodecId, settings.audioCodecId);
        const QByteArray muxer = container == QLatin1String("mkv")
                                     ? QByteArrayLiteral("matroska")
                                     : container.toUtf8();
        avformat_alloc_output_context2(&fmt, nullptr, muxer.constData(), outUtf8.constData());
    }
    if (!fmt) {
        error = QStringLiteral("Could not determine output format");
        goto cleanup;
    }

    {
        vstream = avformat_new_stream(fmt, nullptr);
        astream = avformat_new_stream(fmt, nullptr);
        if (!vstream || !astream) {
            error = QStringLiteral("Could not create output streams");
            goto cleanup;
        }

        vctx = avcodec_alloc_context3(vcodec);
        actx = avcodec_alloc_context3(acodec);
        if (!vctx || !actx) {
            error = QStringLiteral("Could not allocate encoders");
            goto cleanup;
        }

        vctx->width = outW;
        vctx->height = outH;
        vctx->pix_fmt = pickEncodePixFmt(vcodec, vdef->hw, vdef->pixFmt);
        vctx->time_base = frameTb;
        vctx->framerate = frameRate;
        vctx->gop_size = qMax(1, static_cast<int>(std::llround(fpsValue * 2.0)));
        const bool vtH264 =
            vdef->hw == HwBackend::VideoToolbox && std::strncmp(vdef->id, "h264", 4) == 0;
        // MediaCodec: most Android encoders emit no B-frames whatever this says, and mediacodecenc
        // reads a zero here as licence to treat pts as dts — which is the only way the muxer gets a
        // usable timestamp, since MediaCodec reports none of its own.
        vctx->max_b_frames =
            (vdef->hw == HwBackend::Vaapi || vdef->hw == HwBackend::MediaCodec || vtH264) ? 0 : 2;
        if (vdef->hw != HwBackend::None) {
            if (std::strncmp(vdef->id, "h264", 4) == 0)
                vctx->profile = AV_PROFILE_H264_HIGH;
            else if (std::strncmp(vdef->id, "h265", 4) == 0)
                vctx->profile = AV_PROFILE_HEVC_MAIN;
        }
        applySdrBt709Tags(vctx);
        if (fmt->oformat->flags & AVFMT_GLOBALHEADER)
            vctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

        // MediaCodec is a hardware backend with no hardware *device*: mediacodecenc takes plain
        // NV12 in system memory and manages its own codec instance, so there is nothing to create
        // and nothing to upload to.
        if (vdef->hw != HwBackend::None && hwDeviceType(vdef->hw) != AV_HWDEVICE_TYPE_NONE) {
            const AVHWDeviceType type = hwDeviceType(vdef->hw);
            // deviceAvailable() first, not just for the answer: it is the only VAAPI probe
            // that survives a host with no libva, where FFmpeg's stub asserts instead.
            // A codec id restored from settings can name an encoder this machine cannot run.
            if (!drift::hwaccel::deviceAvailable(type)
                || av_hwdevice_ctx_create(&hwDeviceCtx, type, nullptr, nullptr, 0) < 0) {
                error = QStringLiteral("Could not create the %1 encoder device.")
                            .arg(QLatin1String(hwVendorName(vdef->hw)));
                goto cleanup;
            }
            vctx->hw_device_ctx = av_buffer_ref(hwDeviceCtx);
            if (isHardwarePixelFormat(vctx->pix_fmt)
                && !setupHwFrames(vctx, hwDeviceCtx, vctx->pix_fmt, outW, outH, &error))
                goto cleanup;
        }

        applyVideoRateControl(vctx, *vdef, settings);
        applyVideoPreset(vctx, *vdef, settings);

        const AVSampleFormat audioFmt = pickSampleFmt(acodec);
        actx->sample_fmt = audioFmt;
        actx->sample_rate = sampleRate;
        av_channel_layout_default(&actx->ch_layout, 2);
        if (!adef->lossless)
            actx->bit_rate = static_cast<int64_t>(qMax(32, settings.audioBitrateKbps)) * 1000;
        actx->time_base = AVRational{1, sampleRate};
        if (fmt->oformat->flags & AVFMT_GLOBALHEADER)
            actx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

        if (avcodec_open2(vctx, vcodec, nullptr) < 0) {
            if (vdef->hw == HwBackend::MediaCodec) {
                // No driver to blame on a phone: the device advertises a maximum size and a fixed
                // pool of codec instances, and a refusal is nearly always one of those two.
                error = QStringLiteral("Could not open the %1 encoder. This device may not support "
                                       "this resolution, or another app is using the encoder.")
                            .arg(hwEncoderLabel(*vdef));
            } else if (vdef->hw != HwBackend::None) {
                error = QStringLiteral(
                            "Could not open the %1 encoder. Check that the GPU driver supports this "
                            "resolution.")
                            .arg(hwEncoderLabel(*vdef));
            } else {
                error = QStringLiteral("Could not open the video encoder");
            }
            goto cleanup;
        }
        if (avcodec_open2(actx, acodec, nullptr) < 0) {
            error = QStringLiteral("Could not open the audio encoder");
            goto cleanup;
        }

        avcodec_parameters_from_context(vstream->codecpar, vctx);
        avcodec_parameters_from_context(astream->codecpar, actx);
        vstream->time_base = vctx->time_base;
        astream->time_base = actx->time_base;

        const int frameSize = actx->frame_size > 0 ? actx->frame_size : 1024;
        const AVPixelFormat outPixFmt = vctx->pix_fmt;
        const AVPixelFormat swPixFmt =
            isHardwarePixelFormat(outPixFmt) ? AV_PIX_FMT_NV12 : outPixFmt;
        const bool hwUpload = isHardwarePixelFormat(outPixFmt);

        if (!(fmt->oformat->flags & AVFMT_NOFILE)) {
            if (avio_open(&fmt->pb, outUtf8.constData(), AVIO_FLAG_WRITE) < 0) {
                error = QStringLiteral("Could not open the output file");
                goto cleanup;
            }
        }

        if (avformat_write_header(fmt, nullptr) < 0) {
            error = QStringLiteral("Could not write the file header");
            goto cleanup;
        }
        headerWritten = true;

        const bool forceCpuSws = qEnvironmentVariableIsSet("DRIFT_EXPORT_SWSCALE")
                                 && qgetenv("DRIFT_EXPORT_SWSCALE") != "0";
        const bool useGpuNv12 = !forceCpuSws && GpuCompositor::isAvailable()
            && (swPixFmt == AV_PIX_FMT_NV12 || swPixFmt == AV_PIX_FMT_YUV420P) && (outW % 2 == 0)
            && (outH % 2 == 0);

        if (!useGpuNv12) {
            // Lanczos when down/upscaling; bicubic is enough for a pure format convert.
            const int swsFlags = (projW != outW || projH != outH) ? SWS_LANCZOS : SWS_BICUBIC;
            sws = sws_getContext(projW, projH, AV_PIX_FMT_RGBA, outW, outH, swPixFmt, swsFlags,
                                 nullptr, nullptr, nullptr);
            if (!sws) {
                error = QStringLiteral("Could not create the scaler");
                goto cleanup;
            }
            if (!configureExportSws(sws)) {
                error = QStringLiteral("Could not configure export colour conversion");
                goto cleanup;
            }
        }

        pkt = av_packet_alloc();
        vframe = av_frame_alloc();
        aframe = av_frame_alloc();
        if (hwUpload)
            hwframe = av_frame_alloc();
        if (!pkt || !vframe || !aframe || (hwUpload && !hwframe)) {
            error = QStringLiteral("Out of memory");
            goto cleanup;
        }

        vframe->format = swPixFmt;
        vframe->width = outW;
        vframe->height = outH;
        if (av_frame_get_buffer(vframe, 32) < 0) {
            error = QStringLiteral("Could not allocate the video frame");
            goto cleanup;
        }

        aframe->format = audioFmt;
        aframe->sample_rate = sampleRate;
        av_channel_layout_copy(&aframe->ch_layout, &actx->ch_layout);
        aframe->nb_samples = frameSize;
        if (av_frame_get_buffer(aframe, 0) < 0) {
            error = QStringLiteral("Could not allocate the audio frame");
            goto cleanup;
        }

        FrameCompositor compositor;
        compositor.setProject(&project);
        AudioMixer mixer;
        mixer.setProject(&project);

        std::vector<float> audioBuffer;
        int64_t audioSamplesGenerated = 0;
        int64_t audioPts = 0;

        auto flushAudioFrames = [&](bool drainAll) -> bool {
            while (static_cast<int64_t>(audioBuffer.size()) >= static_cast<int64_t>(frameSize) * 2) {
                if (av_frame_make_writable(aframe) < 0) {
                    error = QStringLiteral("Audio frame not writable");
                    return false;
                }
                aframe->nb_samples = frameSize;
                fillAudioFrame(aframe, audioFmt, audioBuffer.data(), frameSize);
                aframe->pts = audioPts;
                audioPts += frameSize;
                if (!encodeWriteFrame(fmt, actx, astream, aframe, pkt, &error))
                    return false;
                audioBuffer.erase(audioBuffer.begin(), audioBuffer.begin() + frameSize * 2);
            }
            if (drainAll && !audioBuffer.empty()) {
                const int remaining = static_cast<int>(audioBuffer.size() / 2);
                if (av_frame_make_writable(aframe) < 0) {
                    error = QStringLiteral("Audio frame not writable");
                    return false;
                }
                aframe->nb_samples = remaining;
                fillAudioFrame(aframe, audioFmt, audioBuffer.data(), remaining);
                aframe->pts = audioPts;
                audioPts += remaining;
                if (!encodeWriteFrame(fmt, actx, astream, aframe, pkt, &error))
                    return false;
                audioBuffer.clear();
            }
            return true;
        };

        auto sendVideoAndAudio = [&](int64_t pts) -> bool {
            applySdrBt709Tags(vframe);
            vframe->pts = pts;
            AVFrame *encodeFrame = vframe;
            if (hwUpload) {
                av_frame_unref(hwframe);
                if (av_hwframe_get_buffer(vctx->hw_frames_ctx, hwframe, 0) < 0) {
                    error = QStringLiteral("Could not allocate a hardware frame");
                    return false;
                }
                if (av_hwframe_transfer_data(hwframe, vframe, 0) < 0) {
                    error = QStringLiteral("Could not upload a frame to the encoder");
                    return false;
                }
                applySdrBt709Tags(hwframe);
                hwframe->pts = pts;
                encodeFrame = hwframe;
            }
            if (!encodeWriteFrame(fmt, vctx, vstream, encodeFrame, pkt, &error))
                return false;

            const int64_t targetSamples =
                qMin(totalAudioSamples,
                     ((pts + 1) * static_cast<int64_t>(sampleRate) * frameRate.den) / frameRate.num);
            const int need = static_cast<int>(targetSamples - audioSamplesGenerated);
            if (need > 0) {
                const size_t base = audioBuffer.size();
                audioBuffer.resize(base + static_cast<size_t>(need) * 2);
                const drift::TimeUs audioStartUs = rangeStartUs
                    + static_cast<drift::TimeUs>(
                        (static_cast<int64_t>(audioSamplesGenerated) * drift::kUsPerSecond)
                        / sampleRate);
                mixer.mix(audioStartUs, need, sampleRate, audioBuffer.data() + base);
                audioSamplesGenerated = targetSamples;
            }
            return flushAudioFrames(false);
        };

        FrameCompositor::RenderOptions composeOptions;
        if (outH < projH)
            composeOptions.previewScale = double(outH) / double(projH);
        composeOptions.readAheadUs = static_cast<drift::TimeUs>(
            std::llround(2.0 * 1e6 * frameRate.den / frameRate.num));

        std::vector<uint8_t> nv12Y;
        std::vector<uint8_t> nv12Uv;
        if (useGpuNv12 && swPixFmt == AV_PIX_FMT_YUV420P) {
            nv12Y.resize(size_t(outW) * size_t(outH));
            nv12Uv.resize(size_t(outW) * size_t(outH / 2));
        }

        struct InflightNv12
        {
            int slot = 0;
            int64_t pts = 0;
            bool packed = false;
        };
        InflightNv12 inflight[GpuCompositor::kExportNv12Slots];
        int inflightCount = 0;

        auto consumeNv12Head = [&]() -> bool {
            const InflightNv12 job = inflight[0];
            for (int i = 0; i < inflightCount - 1; ++i)
                inflight[i] = inflight[i + 1];
            --inflightCount;

            if (av_frame_make_writable(vframe) < 0) {
                error = QStringLiteral("Video frame not writable");
                return false;
            }

            bool mapped = false;
            if (job.packed) {
                if (swPixFmt == AV_PIX_FMT_NV12) {
                    mapped = GpuCompositor::finishExportNv12(job.slot, vframe->data[0],
                                                             vframe->linesize[0], vframe->data[1],
                                                             vframe->linesize[1], outW, outH);
                } else {
                    mapped = GpuCompositor::finishExportNv12(job.slot, nv12Y.data(), outW,
                                                             nv12Uv.data(), outW, outW, outH);
                    if (mapped)
                        nv12ToYuv420p(nv12Y.data(), outW, nv12Uv.data(), outW, vframe);
                }
            }
            if (!mapped)
                fillLimitedBlackFrame(vframe);

            return sendVideoAndAudio(job.pts);
        };

        for (int64_t i = 0; i < totalFrames; ++i) {
            if (onProgress && !onProgress(static_cast<double>(i) / totalFrames)) {
                cancelled = true;
                break;
            }

            const drift::TimeUs t = rangeStartUs + static_cast<drift::TimeUs>(
                std::llround(static_cast<double>(i) * 1e6 * frameRate.den / frameRate.num));

            if (useGpuNv12) {
                if (inflightCount == GpuCompositor::kExportNv12Slots) {
                    if (!consumeNv12Head())
                        goto cleanup;
                }

                GpuScene scene;
                if (!compositor.buildSceneAt(t, composeOptions, &scene)) {
                    scene = GpuScene{};
                    scene.canvasSize = QSize(outW, outH);
                    scene.backgroundColor = Qt::black;
                }

                const int slot = int(i % GpuCompositor::kExportNv12Slots);
                const bool packed = GpuCompositor::beginExportNv12(scene, outW, outH, slot);
                inflight[inflightCount++] = InflightNv12{slot, i, packed};
                continue;
            }

            QImage img = compositor.compositeAt(t);
            if (img.isNull()) {
                img = QImage(projW, projH, QImage::Format_RGBA8888);
                img.fill(Qt::black);
            } else if (img.format() != QImage::Format_RGBA8888) {
                img = img.convertToFormat(QImage::Format_RGBA8888);
            }
            if (img.width() != projW || img.height() != projH)
                img = img.scaled(projW, projH, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);

            if (av_frame_make_writable(vframe) < 0) {
                error = QStringLiteral("Video frame not writable");
                goto cleanup;
            }
            {
                const uint8_t *srcData[4] = {img.constBits(), nullptr, nullptr, nullptr};
                const int srcStride[4] = {static_cast<int>(img.bytesPerLine()), 0, 0, 0};
                sws_scale(sws, srcData, srcStride, 0, projH, vframe->data, vframe->linesize);
            }
            if (!sendVideoAndAudio(i))
                goto cleanup;
        }

        if (useGpuNv12 && !cancelled) {
            while (inflightCount > 0) {
                if (!consumeNv12Head())
                    goto cleanup;
            }
        } else if (useGpuNv12) {
            while (inflightCount > 0) {
                const InflightNv12 job = inflight[0];
                for (int i = 0; i < inflightCount - 1; ++i)
                    inflight[i] = inflight[i + 1];
                --inflightCount;
                if (!job.packed)
                    continue;
                std::vector<uint8_t> dumpY(size_t(outW) * size_t(outH));
                std::vector<uint8_t> dumpUv(size_t(outW) * size_t(outH / 2));
                GpuCompositor::finishExportNv12(job.slot, dumpY.data(), outW, dumpUv.data(), outW,
                                                outW, outH);
            }
        }

        if (!cancelled) {
            if (!flushAudioFrames(true))
                goto cleanup;
            if (!encodeWriteFrame(fmt, vctx, vstream, nullptr, pkt, &error))
                goto cleanup;
            if (!encodeWriteFrame(fmt, actx, astream, nullptr, pkt, &error))
                goto cleanup;
            if (av_write_trailer(fmt) < 0) {
                error = QStringLiteral("Could not finalize the file");
                goto cleanup;
            }
            ok = true;
        }
    }

cleanup:
    if (sws)
        sws_freeContext(sws);
    if (hwframe)
        av_frame_free(&hwframe);
    if (vframe)
        av_frame_free(&vframe);
    if (aframe)
        av_frame_free(&aframe);
    if (pkt)
        av_packet_free(&pkt);
    if (vctx)
        avcodec_free_context(&vctx);
    if (actx)
        avcodec_free_context(&actx);
    if (hwDeviceCtx)
        av_buffer_unref(&hwDeviceCtx);
    if (fmt) {
        if (headerWritten && !ok && fmt->pb)
            av_write_trailer(fmt);
        if (fmt->pb && !(fmt->oformat->flags & AVFMT_NOFILE))
            avio_closep(&fmt->pb);
        avformat_free_context(fmt);
    }

    // A failed or cancelled run leaves a partial file at the destination. Best effort: the portal
    // may refuse the unlink, in which case the half-written file stays and the caller reports the
    // failure anyway.
    if (!ok && QFile::exists(outputPath))
        QFile::remove(outputPath);

    if (!ok && errorOut) {
        *errorOut = cancelled ? QStringLiteral("Export cancelled")
                              : (error.isEmpty() ? QStringLiteral("Export failed") : error);
    }
    return ok;
}

QUrl Exporter::publishToGallery(const QUrl &source, const QString &displayName, QString *errorOut)
{
#ifdef Q_OS_ANDROID
    // The scoped insert below (RELATIVE_PATH + IS_PENDING) is API 29. Publishing on 28 would mean
    // WRITE_EXTERNAL_STORAGE and a raw path into the shared volume — a permission this app
    // deliberately never asks for, for one release of Android.
    if (QNativeInterface::QAndroidApplication::sdkVersion() < 29) {
        if (errorOut)
            *errorOut = QStringLiteral("Saving to the gallery needs Android 10 or newer");
        return {};
    }

    std::unique_ptr<QFile> encoded = AndroidUri::openForRead(source);
    if (!encoded) {
        if (errorOut)
            *errorOut = QStringLiteral("Could not read the exported file");
        return {};
    }

    const QString mimeType =
        QMimeDatabase().mimeTypeForFile(displayName, QMimeDatabase::MatchExtension).name();
    const bool audio = mimeType.startsWith(QLatin1String("audio/"));
    // Marketplace downloads reach this too, and those can be stills. An image inserted into the
    // Video collection is a row the gallery will not show.
    const bool image = mimeType.startsWith(QLatin1String("image/"));

    QJniObject context = QNativeInterface::QAndroidApplication::context();
    QJniObject resolver =
        context.callObjectMethod("getContentResolver", "()Landroid/content/ContentResolver;");
    QJniObject collection = QJniObject::getStaticObjectField(
        audio ? "android/provider/MediaStore$Audio$Media"
              : image ? "android/provider/MediaStore$Images$Media"
                      : "android/provider/MediaStore$Video$Media",
        "EXTERNAL_CONTENT_URI", "Landroid/net/Uri;");
    if (!resolver.isValid() || !collection.isValid()) {
        if (errorOut)
            *errorOut = QStringLiteral("Could not reach the media library");
        return {};
    }

    const auto putString = [](QJniObject &values, const char *key, const QString &value) {
        values.callMethod<void>("put", "(Ljava/lang/String;Ljava/lang/String;)V",
                                QJniObject::fromString(QString::fromLatin1(key)).object<jstring>(),
                                QJniObject::fromString(value).object<jstring>());
    };
    const auto putInt = [](QJniObject &values, const char *key, jint value) {
        values.callMethod<void>(
            "put", "(Ljava/lang/String;Ljava/lang/Integer;)V",
            QJniObject::fromString(QString::fromLatin1(key)).object<jstring>(),
            QJniObject::callStaticObjectMethod("java/lang/Integer", "valueOf",
                                               "(I)Ljava/lang/Integer;", value)
                .object());
    };

    QJniObject values("android/content/ContentValues");
    putString(values, "_display_name", displayName);
    putString(values, "mime_type", mimeType);
    putString(values, "relative_path", audio ? QStringLiteral("Music/Drift")
                                       : image ? QStringLiteral("Pictures/Drift")
                                               : QStringLiteral("Movies/Drift"));
    // Pending until the bytes are there, so the gallery never shows a half-written video.
    putInt(values, "is_pending", 1);

    QJniObject item = resolver.callObjectMethod(
        "insert", "(Landroid/net/Uri;Landroid/content/ContentValues;)Landroid/net/Uri;",
        collection.object(), values.object());
    QJniEnvironment env;
    env.checkAndClearExceptions();
    if (!item.isValid()) {
        if (errorOut)
            *errorOut = QStringLiteral("Could not add the export to the media library");
        return {};
    }

    // Written through the resolver's own stream rather than QFile: Qt's content file engine gates
    // on Context.checkUriPermission, which reports nothing for a MediaStore row this app owns
    // outright and has been granted no explicit URI permission for.
    bool ok = false;
    QJniObject stream = resolver.callObjectMethod(
        "openOutputStream", "(Landroid/net/Uri;)Ljava/io/OutputStream;", item.object());
    if (!env.checkAndClearExceptions() && stream.isValid()) {
        constexpr jsize kChunk = 1 << 16;
        jbyteArray chunk = env->NewByteArray(kChunk);
        std::vector<char> buffer(kChunk);
        ok = true;
        while (!encoded->atEnd()) {
            const qint64 read = encoded->read(buffer.data(), kChunk);
            if (read <= 0) {
                ok = false;
                break;
            }
            env->SetByteArrayRegion(chunk, 0, static_cast<jsize>(read),
                                    reinterpret_cast<const jbyte *>(buffer.data()));
            stream.callMethod<void>("write", "([BII)V", chunk, jint(0), static_cast<jint>(read));
            if (env.checkAndClearExceptions()) {
                ok = false;
                break;
            }
        }
        env->DeleteLocalRef(chunk);
        stream.callMethod<void>("close", "()V");
        if (env.checkAndClearExceptions())
            ok = false;
    }

    if (ok) {
        QJniObject done("android/content/ContentValues");
        putInt(done, "is_pending", 0);
        resolver.callMethod<jint>("update",
                                  "(Landroid/net/Uri;Landroid/content/ContentValues;"
                                  "Ljava/lang/String;[Ljava/lang/String;)I",
                                  item.object(), done.object(), static_cast<jstring>(nullptr),
                                  static_cast<jobjectArray>(nullptr));
        env.checkAndClearExceptions();
        return QUrl(item.toString());
    }

    resolver.callMethod<jint>("delete", "(Landroid/net/Uri;Ljava/lang/String;[Ljava/lang/String;)I",
                              item.object(), static_cast<jstring>(nullptr),
                              static_cast<jobjectArray>(nullptr));
    env.checkAndClearExceptions();
    if (errorOut)
        *errorOut = QStringLiteral("Could not write the export to the media library");
    return {};
#else
    Q_UNUSED(source);
    Q_UNUSED(displayName);
    Q_UNUSED(errorOut);
    return {};
#endif
}
