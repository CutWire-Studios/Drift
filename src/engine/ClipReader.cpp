#include "ClipReader.h"

#include "HwAccel.h"
#include "MediaProbe.h"

#include <QTransform>
#include <QtMath>
#include <QByteArray>
#include <QFile>
#include <QDir>
#include <QUuid>
#include <QTextStream>
#include <QStandardPaths>

#include <atomic>
#include <cmath>
#include <cstring>
#include <limits>
#include <utility>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavcodec/codec.h>
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavformat/avformat.h>
#include <libavutil/hwcontext.h>
#include <libavutil/imgutils.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

namespace {

bool sliceTrfFile(const QString &sourcePath, const QString &destPath, int startFrame, double scaleX, double scaleY)
{
    QFile src(sourcePath);
    if (!src.open(QIODevice::ReadOnly | QIODevice::Text))
        return false;

    QFile dest(destPath);
    if (!dest.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;

    QTextStream srcStream(&src);
    QTextStream destStream(&dest);

    // Read and copy all header lines starting with '#'
    while (!srcStream.atEnd()) {
        qint64 pos = src.pos();
        QString line = srcStream.readLine();
        if (line.startsWith(QLatin1Char('#'))) {
            destStream << line << "\n";
        } else {
            src.seek(pos);
            break;
        }
    }

    int currentLine = 0;
    while (currentLine < startFrame && !srcStream.atEnd()) {
        srcStream.readLine();
        currentLine++;
    }

    int outFrameIndex = 1;
    while (!srcStream.atEnd()) {
        QString line = srcStream.readLine();
        QStringList parts = line.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        if (parts.size() >= 5) {
            bool ok1 = false, ok2 = false;
            double ox = parts[3].toDouble(&ok1);
            double oy = parts[4].toDouble(&ok2);
            if (ok1 && ok2) {
                parts[3] = QString::number(ox * scaleX, 'f', 6);
                parts[4] = QString::number(oy * scaleY, 'f', 6);
            }
            parts[0] = QString::number(outFrameIndex);
            destStream << parts.join(QLatin1Char(' ')) << "\n";
            outFrameIndex++;
        } else {
            destStream << line << "\n";
        }
    }

    return true;
}

bool isHardwarePixelFormat(AVPixelFormat fmt)
{
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(fmt);
    return desc && (desc->flags & AV_PIX_FMT_FLAG_HWACCEL);
}

int swsColorspaceFromFrame(const AVFrame *frame)
{
    if (!frame)
        return SWS_CS_ITU709;
    switch (frame->colorspace) {
    case AVCOL_SPC_BT470BG:
    case AVCOL_SPC_SMPTE170M:
        return SWS_CS_ITU601;
    case AVCOL_SPC_SMPTE240M:
        return SWS_CS_SMPTE240M;
    case AVCOL_SPC_FCC:
        return SWS_CS_FCC;
    case AVCOL_SPC_BT709:
    case AVCOL_SPC_BT2020_NCL:
    case AVCOL_SPC_BT2020_CL:
        return SWS_CS_ITU709;
    case AVCOL_SPC_UNSPECIFIED:
    default:
        // Drift's SDR pipeline defaults to BT.709 when the bitstream is untagged.
        return SWS_CS_ITU709;
    }
}

// YUV (typically limited) → RGB/NV12 with source colourspace when tagged.
void configureDecodeSws(SwsContext *sws, const AVFrame *src, int dstRange)
{
    if (!sws || !src)
        return;
    const int *coeff = sws_getCoefficients(swsColorspaceFromFrame(src));
    // Unspecified range is treated as limited (MPEG/TV) — the common case for camera footage.
    const int srcRange = src->color_range == AVCOL_RANGE_JPEG ? 1 : 0;
    sws_setColorspaceDetails(sws, coeff, srcRange, coeff, dstRange, 0, 1 << 16, 1 << 16);
}

int swsFlagsForResize(int srcW, int srcH, int dstW, int dstH)
{
    return (srcW != dstW || srcH != dstH) ? SWS_LANCZOS : SWS_BICUBIC;
}

// Prefer the hardware surface format when the decoder offers it; otherwise pick the
// first software format so get_format never hard-fails with AV_PIX_FMT_NONE
// (that path leaves the hwaccel decoder in a half-initialized state).
AVPixelFormat hwGetFormat(AVCodecContext *ctx, const AVPixelFormat *pixFmts)
{
    const AVPixelFormat prefer =
        ctx && ctx->opaque ? *static_cast<const AVPixelFormat *>(ctx->opaque) : AV_PIX_FMT_NONE;

    for (const AVPixelFormat *p = pixFmts; *p != AV_PIX_FMT_NONE; ++p) {
        if (*p == prefer)
            return *p;
    }

    for (const AVPixelFormat *p = pixFmts; *p != AV_PIX_FMT_NONE; ++p) {
        if (!isHardwarePixelFormat(*p))
            return *p;
    }

    return pixFmts ? pixFmts[0] : AV_PIX_FMT_NONE;
}

QImage frameToRgba(const AVFrame *frame, SwsContext *&sws, int targetWidth, int targetHeight,
                   int rotation)
{
    if (!frame || targetWidth <= 0 || targetHeight <= 0)
        return {};
    if (isHardwarePixelFormat(static_cast<AVPixelFormat>(frame->format)))
        return {};

    const int flags = swsFlagsForResize(frame->width, frame->height, targetWidth, targetHeight);
    sws = sws_getCachedContext(sws, frame->width, frame->height,
                               static_cast<AVPixelFormat>(frame->format), targetWidth, targetHeight,
                               AV_PIX_FMT_RGBA, flags, nullptr, nullptr, nullptr);
    if (!sws)
        return {};
    configureDecodeSws(sws, frame, 1 /* full-range RGB */);

    AVFrame *rgba = av_frame_alloc();
    if (!rgba)
        return {};

    rgba->format = AV_PIX_FMT_RGBA;
    rgba->width = targetWidth;
    rgba->height = targetHeight;
    if (av_frame_get_buffer(rgba, 0) < 0) {
        av_frame_free(&rgba);
        return {};
    }

    sws_scale(sws, frame->data, frame->linesize, 0, frame->height, rgba->data, rgba->linesize);

    // Qt's y-axis points down, so a positive angle is the clockwise turn a player would
    // make — which is what displayRotationOf() reports. transformed() allocates its own
    // buffer; copy() is still needed at rotation 0 because `image` only wraps the
    // AVFrame that is freed just below.
    QImage image(rgba->data[0], targetWidth, targetHeight, rgba->linesize[0], QImage::Format_RGBA8888);
    const QImage copy = rotation == 0 ? image.copy() : image.transformed(QTransform().rotate(rotation));
    av_frame_free(&rgba);
    return copy;
}

// Software preview frames: NV12 at the decode size, still an AVFrame (no packed
// QByteArray, no CPU rotate). The GL importer honours linesize and applies rotation.
AVFrame *softwareFrameToNv12(const AVFrame *frame, SwsContext *&sws, int targetWidth, int targetHeight)
{
    if (!frame || targetWidth <= 0 || targetHeight <= 0)
        return nullptr;
    if (isHardwarePixelFormat(static_cast<AVPixelFormat>(frame->format)))
        return nullptr;

    targetWidth &= ~1;
    targetHeight &= ~1;
    if (targetWidth < 2 || targetHeight < 2)
        return nullptr;

    if (frame->format == AV_PIX_FMT_NV12 && frame->width == targetWidth
        && frame->height == targetHeight)
        return av_frame_clone(frame);

    const int flags = swsFlagsForResize(frame->width, frame->height, targetWidth, targetHeight);
    sws = sws_getCachedContext(sws, frame->width, frame->height,
                               static_cast<AVPixelFormat>(frame->format), targetWidth, targetHeight,
                               AV_PIX_FMT_NV12, flags, nullptr, nullptr, nullptr);
    if (!sws)
        return nullptr;
    configureDecodeSws(sws, frame, frame->color_range == AVCOL_RANGE_JPEG ? 1 : 0);

    AVFrame *nv12 = av_frame_alloc();
    if (!nv12)
        return nullptr;
    nv12->format = AV_PIX_FMT_NV12;
    nv12->width = targetWidth;
    nv12->height = targetHeight;
    nv12->colorspace = frame->colorspace;
    nv12->color_range = frame->color_range;
    nv12->pts = frame->pts;
    if (av_frame_get_buffer(nv12, 0) < 0) {
        av_frame_free(&nv12);
        return nullptr;
    }
    sws_scale(sws, frame->data, frame->linesize, 0, frame->height, nv12->data, nv12->linesize);
    return nv12;
}

drift::TimeUs ptsToUs(const AVFrame *frame, const AVRational &timeBase)
{
    if (!frame)
        return 0;
    const int64_t pts = frame->best_effort_timestamp != AV_NOPTS_VALUE
                            ? frame->best_effort_timestamp
                            : frame->pts;
    if (pts == AV_NOPTS_VALUE)
        return 0;
    return av_rescale_q(pts, timeBase, {1, drift::kUsPerSecond});
}

} // namespace

ClipReader::ClipReader() = default;

ClipReader::~ClipReader()
{
    close();
}

void ClipReader::teardownVideoDecoder()
{
    teardownSwFilterGraph();
    teardownHwScaler();
    // Hardware surfaces in the cursor belong to the decoder pool; drop them
    // before the context goes away.
    freeVideoCursor();
    if (m_sws) {
        sws_freeContext(m_sws);
        m_sws = nullptr;
    }
    if (m_swsNv12) {
        sws_freeContext(m_swsNv12);
        m_swsNv12 = nullptr;
    }
    if (m_videoCtx)
        avcodec_free_context(&m_videoCtx);
    if (m_hwDeviceCtx)
        av_buffer_unref(&m_hwDeviceCtx);
    m_hwAccelActive = false;
    m_mediaCodecActive = false;
    m_hwBackend = drift::hwaccel::Backend::None;
    m_hwPixFmt = AV_PIX_FMT_NONE;
    m_videoPositioned = false;
    m_lastVideoPtsUs = 0;
    m_decodeW = 0;
    m_decodeH = 0;
    m_videoCache.clear();
    m_previewCache.clear();
}

QSize ClipReader::decodeSizeFor(int maxWidth, int maxHeight) const
{
    const AVCodecParameters *par = m_fmt->streams[m_videoStream]->codecpar;
    const int srcW = par->width;
    const int srcH = par->height;
    if (srcW <= 0 || srcH <= 0)
        return {qMax(1, maxWidth), qMax(1, maxHeight)};
    if (maxWidth <= 0 || maxHeight <= 0)
        return {srcW, srcH};

    // The caller's box is in display orientation but srcW/srcH are coded, and the
    // returned size is the sws target — so match the box to the source instead of
    // the other way round. The transpose happens after conversion.
    if (m_sourceRotation == 90 || m_sourceRotation == 270)
        std::swap(maxWidth, maxHeight);

    // Never decode larger than the source; scaling up is the compositor's job.
    const double fit = qMin(static_cast<double>(maxWidth) / srcW, static_cast<double>(maxHeight) / srcH);
    if (fit >= 1.0)
        return {srcW, srcH};

    // Quantize up to 1/8 steps. A preview panel dragged a few pixels wider must
    // not change the decode size, or every resize would drop the frame cache.
    const double quantized = qMin(1.0, std::ceil(fit * 8.0) / 8.0);
    const int w = qMax(2, static_cast<int>(std::lround(srcW * quantized)) & ~1);
    const int h = qMax(2, static_cast<int>(std::lround(srcH * quantized)) & ~1);
    return {w, h};
}

void ClipReader::applyDecodeSize(const QSize &size)
{
    if (m_decodeW == size.width() && m_decodeH == size.height())
        return;

    // A new decode size invalidates the cached images (they are the wrong size)
    // but NOT the demux position — there is no reason to seek.
    m_decodeW = size.width();
    m_decodeH = size.height();
    m_videoCache.clear();
    m_previewCache.clear();
}

namespace {
std::atomic<quint64> g_videoFramesDecoded{0};
std::atomic<int> g_hardwareDecodeMode{static_cast<int>(ClipReader::HardwareDecodeMode::Auto)};
std::atomic<int> g_pinnedDecodeBackend{static_cast<int>(drift::hwaccel::Backend::None)};
// -1 until a video decoder opens; otherwise the Backend the last one landed on.
std::atomic<int> g_activeDecodeBackend{-1};
std::atomic<quint64> g_hwFallbackCount{0};
} // namespace

quint64 ClipReader::videoFramesDecoded()
{
    return g_videoFramesDecoded.load(std::memory_order_relaxed);
}

QString ClipReader::videoDecoderName() const
{
    if (!m_videoCtx || !m_videoCtx->codec || !m_videoCtx->codec->name)
        return {};
    return QString::fromUtf8(m_videoCtx->codec->name);
}

void ClipReader::setHardwareDecodeMode(HardwareDecodeMode mode, drift::hwaccel::Backend backend)
{
    g_pinnedDecodeBackend.store(static_cast<int>(backend), std::memory_order_relaxed);
    g_hardwareDecodeMode.store(static_cast<int>(mode), std::memory_order_relaxed);
}

ClipReader::HardwareDecodeMode ClipReader::hardwareDecodeMode()
{
    return static_cast<HardwareDecodeMode>(g_hardwareDecodeMode.load(std::memory_order_relaxed));
}

drift::hwaccel::Backend ClipReader::pinnedDecodeBackend()
{
    return static_cast<drift::hwaccel::Backend>(
        g_pinnedDecodeBackend.load(std::memory_order_relaxed));
}

std::optional<drift::hwaccel::Backend> ClipReader::activeDecodeBackend()
{
    const int value = g_activeDecodeBackend.load(std::memory_order_relaxed);
    if (value < 0)
        return std::nullopt;
    return static_cast<drift::hwaccel::Backend>(value);
}

quint64 ClipReader::hardwareFallbackCount()
{
    return g_hwFallbackCount.load(std::memory_order_relaxed);
}

void ClipReader::resetVideoDecoder()
{
    teardownVideoDecoder();
    m_hwAccelDisabled = false;
    m_hwScalerFailed = false;
}

drift::TimeUs ClipReader::frameToleranceUs() const
{
    // Half a source frame: the nearest-frame window. The old fixed 40 ms was
    // longer than a frame above ~25 fps, so it returned stale frames.
    if (m_sourceFrameDurationUs > 0)
        return qMax<drift::TimeUs>(1, m_sourceFrameDurationUs / 2);
    return 20'000;
}

bool ClipReader::lookupCachedFrame(drift::TimeUs sourceUs, QImage &out) const
{
    const drift::TimeUs tolerance = frameToleranceUs();
    drift::TimeUs bestDelta = tolerance + 1;
    int bestIndex = -1;
    for (int i = 0; i < m_videoCache.size(); ++i) {
        const drift::TimeUs delta = qAbs(m_videoCache.at(i).ptsUs - sourceUs);
        if (delta <= tolerance && delta < bestDelta) {
            bestDelta = delta;
            bestIndex = i;
        }
    }
    if (bestIndex < 0)
        return false;

    out = m_videoCache.at(bestIndex).image;
    return true;
}

void ClipReader::storeCachedFrame(drift::TimeUs ptsUs, const QImage &image)
{
    if (image.isNull())
        return;

    for (int i = 0; i < m_videoCache.size(); ++i) {
        if (m_videoCache.at(i).ptsUs == ptsUs) {
            m_videoCache.move(i, 0);
            return;
        }
    }

    m_videoCache.prepend(CachedFrame{ptsUs, image});
    while (m_videoCache.size() > kMaxCachedFrames)
        m_videoCache.removeLast();
}

bool ClipReader::lookupCachedPreview(drift::TimeUs sourceUs, PreviewVideoFrame &out) const
{
    const drift::TimeUs tolerance = frameToleranceUs();
    drift::TimeUs bestDelta = tolerance + 1;
    int bestIndex = -1;
    for (int i = 0; i < m_previewCache.size(); ++i) {
        const drift::TimeUs delta = qAbs(m_previewCache.at(i).ptsUs - sourceUs);
        if (delta <= tolerance && delta < bestDelta) {
            bestDelta = delta;
            bestIndex = i;
        }
    }
    if (bestIndex < 0)
        return false;

    out = m_previewCache.at(bestIndex).frame;
    return out.isValid();
}

void ClipReader::storeCachedPreview(drift::TimeUs ptsUs, const PreviewVideoFrame &frame)
{
    if (!frame.isValid())
        return;

    for (int i = 0; i < m_previewCache.size(); ++i) {
        if (m_previewCache.at(i).ptsUs == ptsUs) {
            m_previewCache.move(i, 0);
            return;
        }
    }

    m_previewCache.prepend(CachedPreview{ptsUs, frame});
    trimPreviewCache();
}

int ClipReader::previewCacheCapacity() const
{
    const int historyFrames = qMax(kMinCachedFrames, kMaxCachedFrames / m_previewCacheShares);

    const bool hw = !m_previewCache.isEmpty() && m_previewCache.constFirst().frame.isHardware();
    if (hw) {
        int frames = kMaxHwCachedFrames;
        if (m_sourceFrameDurationUs > 0) {
            frames = qBound(kMinHwCachedFrames,
                            static_cast<int>(kHwPreviewReadAheadUs / m_sourceFrameDurationUs),
                            kMaxHwCachedFrames);
        }
        return qMax(2, frames / m_previewCacheShares);
    }

    if (m_readAheadUs <= 0 || m_sourceFrameDurationUs <= 0)
        return kMaxCachedFrames;

    const int aheadFrames =
        qBound(0, static_cast<int>(m_readAheadUs / m_sourceFrameDurationUs), kMaxReadAheadFrames);
    int capacity = historyFrames + aheadFrames;

    qsizetype frameBytes = 0;
    if (!m_previewCache.isEmpty() && m_previewCache.constFirst().frame.frame) {
        const AVFrame *f = m_previewCache.constFirst().frame.frame.get();
        frameBytes = av_image_get_buffer_size(static_cast<AVPixelFormat>(f->format), f->width, f->height, 1);
    }
    if (frameBytes > 0) {
        const qsizetype budget = kPreviewCacheByteBudget / m_previewCacheShares;
        capacity = qMin<qsizetype>(capacity, qMax<qsizetype>(historyFrames, budget / frameBytes));
    }
    return capacity;
}

void ClipReader::trimPreviewCache()
{
    const int capacity = previewCacheCapacity();
    while (m_previewCache.size() > capacity) {
        int worst = 0;
        drift::TimeUs worstRank = std::numeric_limits<drift::TimeUs>::min();
        for (int i = 0; i < m_previewCache.size(); ++i) {
            const drift::TimeUs delta = m_lastRequestedPreviewUs - m_previewCache.at(i).ptsUs;
            const drift::TimeUs rank = delta >= 0 ? delta + std::numeric_limits<qint32>::max() : -delta;
            if (rank > worstRank) {
                worstRank = rank;
                worst = i;
            }
        }
        m_previewCache.removeAt(worst);
    }
}

bool ClipReader::wantsMorePreviewReadAhead() const
{
    if (m_readAheadUs <= 0 || !m_videoPositioned || m_sourceFrameDurationUs <= 0)
        return false;
    if (m_previewCache.size() >= previewCacheCapacity())
        return false;
    return m_lastVideoPtsUs - m_lastRequestedPreviewUs < m_readAheadUs;
}

void ClipReader::close()
{
    if (m_swr)
        swr_free(&m_swr);

    teardownVideoDecoder();

    if (m_audioCtx)
        avcodec_free_context(&m_audioCtx);
    if (m_fmt)
        avformat_close_input(&m_fmt);

    m_videoStream = -1;
    m_audioStream = -1;
    m_audioStreamOrdinal = 0;
    m_sourceRotation = 0;
    m_hwAccelDisabled = false;
    m_hwScalerFailed = false;
    m_audioPositioned = false;
    m_audioNextPtsUs = 0;
    m_audioLeftover.clear();
    m_path.clear();
}

bool ClipReader::open(const QString &path, int audioStreamOrdinal)
{
    if (path.isEmpty())
        return false;
    if (m_path == path && m_audioStreamOrdinal == audioStreamOrdinal && isOpen())
        return true;

    close();
    m_path = path;
    m_audioStreamOrdinal = audioStreamOrdinal;

    AVFormatContext *fmt = nullptr;
    if (avformat_open_input(&fmt, path.toUtf8().constData(), nullptr, nullptr) < 0)
        return false;
    if (avformat_find_stream_info(fmt, nullptr) < 0) {
        avformat_close_input(&fmt);
        return false;
    }

    m_fmt = fmt;
    int audioCount = 0;
    for (unsigned i = 0; i < m_fmt->nb_streams; ++i) {
        const AVMediaType type = m_fmt->streams[i]->codecpar->codec_type;
        if (type == AVMEDIA_TYPE_VIDEO && m_videoStream < 0)
            m_videoStream = static_cast<int>(i);
        else if (type == AVMEDIA_TYPE_AUDIO) {
            if (audioCount == m_audioStreamOrdinal)
                m_audioStream = static_cast<int>(i);
            ++audioCount;
        }
    }
    if (m_audioStream < 0 && audioCount > 0) {
        for (unsigned i = 0; i < m_fmt->nb_streams; ++i) {
            if (m_fmt->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
                m_audioStream = static_cast<int>(i);
                break;
            }
        }
    }

    if (m_videoStream >= 0) {
        m_sourceRotation = displayRotationOf(m_fmt->streams[m_videoStream]);
        const AVRational rate = m_fmt->streams[m_videoStream]->avg_frame_rate;
        if (rate.num > 0 && rate.den > 0) {
            m_sourceFrameDurationUs =
                static_cast<drift::TimeUs>(std::llround(drift::kUsPerSecond * double(rate.den) / rate.num));
        }
    }

    return hasVideo() || hasAudio();
}

void ClipReader::setAudioStreamOrdinal(int ordinal)
{
    if (m_audioStreamOrdinal == ordinal)
        return;
    m_audioStreamOrdinal = ordinal;
    if (!m_fmt)
        return;

    if (m_audioCtx)
        avcodec_free_context(&m_audioCtx);
    if (m_swr)
        swr_free(&m_swr);
    m_audioStream = -1;
    m_audioPositioned = false;
    m_audioNextPtsUs = 0;
    m_audioLeftover.clear();

    int audioCount = 0;
    for (unsigned i = 0; i < m_fmt->nb_streams; ++i) {
        if (m_fmt->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            if (audioCount == m_audioStreamOrdinal) {
                m_audioStream = static_cast<int>(i);
                break;
            }
            ++audioCount;
        }
    }
    if (m_audioStream < 0 && audioCount > 0) {
        for (unsigned i = 0; i < m_fmt->nb_streams; ++i) {
            if (m_fmt->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
                m_audioStream = static_cast<int>(i);
                break;
            }
        }
    }
}

bool ClipReader::openSoftwareVideoDecoder()
{
    if (!m_fmt || m_videoStream < 0)
        return false;

    const AVCodecParameters *par = m_fmt->streams[m_videoStream]->codecpar;
    const AVCodec *codec = avcodec_find_decoder(par->codec_id);
    if (!codec)
        return false;

    m_videoCtx = avcodec_alloc_context3(codec);
    if (!m_videoCtx)
        return false;

    if (avcodec_parameters_to_context(m_videoCtx, par) < 0) {
        avcodec_free_context(&m_videoCtx);
        return false;
    }

    // 0 lets libavcodec size the pool (typically one worker per core). Caps used
    // to leave 4K software decode short of realtime; overlapping readers can
    // still oversubscribe, which is preferable to stuttering a single clip.
    m_videoCtx->thread_count = 0;
    m_videoCtx->thread_type = FF_THREAD_FRAME | FF_THREAD_SLICE;

    if (avcodec_open2(m_videoCtx, codec, nullptr) < 0) {
        avcodec_free_context(&m_videoCtx);
        return false;
    }

    m_hwAccelActive = false;
    m_hwBackend = drift::hwaccel::Backend::None;
    m_hwPixFmt = AV_PIX_FMT_NONE;
    g_activeDecodeBackend.store(static_cast<int>(drift::hwaccel::Backend::None),
                                std::memory_order_relaxed);
    return true;
}

// Hardware decode is cheap, but the GPU→CPU readback the preview needs often costs
// more than software on light streams — more so on a backend with no surface scaler
// (D3D11VA), where the readback moves full-resolution pixels. Auto uses this to keep
// light clips on the CPU and send 4K / high-bitrate ones to the GPU.
constexpr double kHwAccelMinKbitPerFrame = 250.0;
// Pixels a second above which software decode is sent to the GPU whatever the bitrate.
// 1080p60 sits just over this; 1080p30 and 720p60 sit under it.
constexpr double kHwAccelMinPixelsPerSecond = 1920.0 * 1080.0 * 50.0;

bool ClipReader::hardwareDecodeIsWorthIt() const
{
    const AVStream *stream = m_fmt->streams[m_videoStream];
    const AVCodecParameters *par = stream->codecpar;

    if (int64_t(par->width) * par->height >= 3840LL * 2160)
        return true;

    const AVRational rate = stream->avg_frame_rate;
    const double fps = (rate.num > 0 && rate.den > 0) ? double(rate.num) / double(rate.den) : 0.0;

    // Frame rate belongs on this side of the comparison, not the other. The kbit-per-frame
    // test below divides by fps, so doubling the frame rate halves the figure and makes
    // hardware decode *less* likely — backwards, because the same content at 60 fps is twice
    // the decode work in half the time budget. A 1080p60 screen capture at a typical bitrate
    // lands just under the per-frame threshold while its 30 fps twin clears it comfortably,
    // which is exactly the pair users report as "60 fps stutters, 30 fps is fine".
    if (fps > 0.0 && double(par->width) * par->height * fps >= kHwAccelMinPixelsPerSecond)
        return true;

    int64_t bitRate = par->bit_rate;
    if (bitRate <= 0)
        bitRate = m_fmt->bit_rate; // Matroska usually omits the per-stream value
    if (bitRate <= 0)
        return true;

    if (fps <= 0.0)
        return true;

    return (double(bitRate) / fps / 1000.0) >= kHwAccelMinKbitPerFrame;
}

bool ClipReader::openHardwareDecoderWith(drift::hwaccel::Backend backend)
{
    const AVHWDeviceType type = drift::hwaccel::deviceType(backend);
    if (!drift::hwaccel::deviceAvailable(type))
        return false;

    const AVCodecParameters *par = m_fmt->streams[m_videoStream]->codecpar;
    AVPixelFormat pixFmt = AV_PIX_FMT_NONE;
    const AVCodec *codec = drift::hwaccel::findDecoder(par->codec_id, type, &pixFmt);
    if (!codec)
        return false;

    if (av_hwdevice_ctx_create(&m_hwDeviceCtx, type, nullptr, nullptr, 0) < 0) {
        if (m_hwDeviceCtx)
            av_buffer_unref(&m_hwDeviceCtx);
        return false;
    }

    m_videoCtx = avcodec_alloc_context3(codec);
    if (!m_videoCtx) {
        av_buffer_unref(&m_hwDeviceCtx);
        return false;
    }

    if (avcodec_parameters_to_context(m_videoCtx, par) < 0) {
        avcodec_free_context(&m_videoCtx);
        av_buffer_unref(&m_hwDeviceCtx);
        return false;
    }

    m_hwPixFmt = pixFmt;
    m_videoCtx->hw_device_ctx = av_buffer_ref(m_hwDeviceCtx);
    m_videoCtx->opaque = &m_hwPixFmt;
    m_videoCtx->get_format = hwGetFormat;
    // Preview caches a short ring of hardware surfaces. Without extra pool slots
    // the decoder stalls once those refs are outstanding.
    m_videoCtx->extra_hw_frames = kHwExtraFrames;

    if (avcodec_open2(m_videoCtx, codec, nullptr) < 0) {
        avcodec_free_context(&m_videoCtx);
        av_buffer_unref(&m_hwDeviceCtx);
        m_hwPixFmt = AV_PIX_FMT_NONE;
        return false;
    }

    m_hwBackend = backend;
    m_hwAccelActive = true;
    g_activeDecodeBackend.store(static_cast<int>(backend), std::memory_order_relaxed);
    return true;
}

bool ClipReader::tryOpenHardwareDecoder()
{
    if (!m_fmt || m_videoStream < 0 || m_hwAccelActive || m_hwAccelDisabled)
        return m_hwAccelActive;

    // Hardware vs software is a preview preference. Auto keeps the per-clip
    // heuristic (4K / heavy bitrates on the GPU, cheap streams on software);
    // Software and Hardware force that path. DRIFT_NO_HWACCEL still forces
    // software on a broken driver regardless of the toggle.
    if (drift::hwaccel::disabledByEnv())
        return false;

    const HardwareDecodeMode mode = hardwareDecodeMode();
    if (mode == HardwareDecodeMode::Software)
        return false;
    if (mode == HardwareDecodeMode::Auto && !hardwareDecodeIsWorthIt())
        return false;

#ifdef Q_OS_ANDROID
    // None of the backends below exist on Android: CUDA, VAAPI, D3D11VA and VideoToolbox are
    // all configured out of the Android FFmpeg. MediaCodec takes their place, but deliberately
    // not as a hwaccel — see tryOpenMediaCodecDecoder. Anything it declines leaves
    // m_hwAccelDisabled set, which is already how this reader says "software from here on".
    if (!qEnvironmentVariableIsSet("DRIFT_NO_MEDIACODEC") && tryOpenMediaCodecDecoder())
        return true;

    m_hwAccelDisabled = true;
    return false;
#else
    // An explicit pick is honoured on its own: falling back to a backend the user did
    // not choose would hide exactly the problem they picked around.
    const drift::hwaccel::Backend pinned = pinnedDecodeBackend();
    if (mode == HardwareDecodeMode::Hardware && pinned != drift::hwaccel::Backend::None) {
        if (openHardwareDecoderWith(pinned))
            return true;
    } else {
        // Auto used to ignore the pin entirely and always take the first backend that
        // opened, so choosing one in the picker changed nothing unless Hardware was also
        // forced. Try it first here, then fall through to the rest of the order — Auto's
        // promise is that it still finds something, not that it ignores the preference.
        if (pinned != drift::hwaccel::Backend::None && openHardwareDecoderWith(pinned))
            return true;
        for (const drift::hwaccel::Backend backend : drift::hwaccel::decodeBackendOrder()) {
            if (backend != pinned && openHardwareDecoderWith(backend))
                return true;
        }
    }

    // Nothing here takes this stream. Sticky so every later frame of this clip does
    // not re-walk the codec list.
    m_hwAccelDisabled = true;
    return false;
#endif // Q_OS_ANDROID
}

#ifdef Q_OS_ANDROID
// MediaCodec configured *without* a Surface. avcodec_default_get_format only picks
// AV_PIX_FMT_MEDIACODEC when an AV_HWDEVICE_TYPE_MEDIACODEC device is attached, and we attach
// none, so ff_get_format returns AV_PIX_FMT_NONE, mediacodecdec leaves its surface null, and each
// output buffer is copied out as an ordinary NV12/YUV420P AVFrame. That keeps the opaque
// SurfaceTexture — which would need a GL bridge and is hostile to the compositor's per-frame
// seeking — out of the picture entirely: sws, both frame caches and the compositor see exactly
// what the software decoder produces, while the entropy decode and motion compensation move to
// the video block.
//
// Restricted to content where that trade is not close. MediaCodec costs a codec instance (phones
// share a small pool of them), a pipeline fill after every flush, and it cannot downscale on the
// way out, so the preview's full-resolution sws downscale is unchanged. Below 4K H.264 / 1080p
// HEVC-VP9 the software decoder is not the bottleneck on a phone and this path has no on-device
// measurements behind it. AV1 is the exception and is offered at every resolution — see below.
//
// Seek cost is handled by the reader's existing sequential-decode state, not by anything new
// here: playback and export walk forward and never flush, and a scrub already has to decode from
// the preceding keyframe, so the one flush it does add is amortised over that whole GOP — which
// is precisely the bulk sequential work MediaCodec is fastest at.
bool ClipReader::tryOpenMediaCodecDecoder()
{
    const AVCodecParameters *par = m_fmt->streams[m_videoStream]->codecpar;

    const char *name = nullptr;
    switch (par->codec_id) {
    case AV_CODEC_ID_H264: name = "h264_mediacodec"; break;
    case AV_CODEC_ID_HEVC: name = "hevc_mediacodec"; break;
    case AV_CODEC_ID_AV1: name = "av1_mediacodec"; break;
    case AV_CODEC_ID_VP9: name = "vp9_mediacodec"; break;
    default: return false;
    }

    // AV1 has no floor: MediaCodec is the fast path for it, not the only one — libdav1d decodes it
    // in software — but dav1d on a phone is expensive enough at any size that the trade above is
    // no longer close, and prebuilts predating --enable-libdav1d have no software AV1 at all
    // (FFmpeg's native "av1" decoder is a hwaccel shell that returns ENOSYS per frame). There,
    // declining on resolution alone is a black preview.
    if (par->codec_id != AV_CODEC_ID_AV1) {
        const int64_t pixels = int64_t(par->width) * par->height;
        const int64_t floor = par->codec_id == AV_CODEC_ID_H264 ? 3840LL * 2160 : 1920LL * 1080;
        if (pixels < floor)
            return false;
    }

    // Null when FFmpeg was built without --enable-mediacodec, which is the only state the
    // prebuilt libraries have shipped in so far.
    const AVCodec *codec = avcodec_find_decoder_by_name(name);
    if (!codec)
        return false;

    m_videoCtx = avcodec_alloc_context3(codec);
    if (!m_videoCtx)
        return false;

    if (avcodec_parameters_to_context(m_videoCtx, par) < 0) {
        avcodec_free_context(&m_videoCtx);
        return false;
    }

    // Unsupported profile or dimensions, no decoder instance free, or no MediaCodec at all.
    if (avcodec_open2(m_videoCtx, codec, nullptr) < 0) {
        avcodec_free_context(&m_videoCtx);
        return false;
    }

    m_mediaCodecActive = true;
    m_hwPixFmt = AV_PIX_FMT_NONE;
    return true;
}
#endif // Q_OS_ANDROID

bool ClipReader::fallbackFromHardwareDecoder()
{
    if (!m_hwAccelActive && !m_hwDeviceCtx && !m_mediaCodecActive)
        return openSoftwareVideoDecoder();

    g_hwFallbackCount.fetch_add(1, std::memory_order_relaxed);
    teardownVideoDecoder();
    m_hwAccelDisabled = true;
    return openSoftwareVideoDecoder();
}

bool ClipReader::ensureVideoDecoder()
{
    if (!m_fmt || m_videoStream < 0)
        return false;
    if (m_videoCtx)
        return true;

    if (tryOpenHardwareDecoder())
        return true;

    return openSoftwareVideoDecoder();
}

void ClipReader::teardownHwScaler()
{
    if (m_vppGraph)
        avfilter_graph_free(&m_vppGraph);
    m_vppSrc = nullptr;
    m_vppSink = nullptr;
    if (m_vppFramesCtx)
        av_buffer_unref(&m_vppFramesCtx);
    av_frame_free(&m_vppScaled);
    av_frame_free(&m_swFrame);
    m_vppW = 0;
    m_vppH = 0;
}

void ClipReader::teardownSwFilterGraph()
{
    if (m_swFilterGraph)
        avfilter_graph_free(&m_swFilterGraph);
    m_swFilterGraph = nullptr;
    m_swFilterSrc = nullptr;
    m_swFilterSink = nullptr;
    m_swFilterW = 0;
    m_swFilterH = 0;
    m_swFilterFormat = AV_PIX_FMT_NONE;
    m_expectedNextFrameIndex = -1;
    if (!m_tempTrfPath.isEmpty()) {
        QFile::remove(m_tempTrfPath);
        m_tempTrfPath.clear();
    }
}

bool ClipReader::initSwFilterGraph(int width, int height, AVPixelFormat pixFmt)
{
    if (m_swFilterGraph) {
        if (m_swFilterW == width && m_swFilterH == height && m_swFilterFormat == pixFmt
            && m_swFilterSmoothing == m_stabilizeSmoothing && m_swFilterTripod == m_stabilizeTripod)
            return true;
        teardownSwFilterGraph();
    }

    m_swFilterGraph = avfilter_graph_alloc();
    if (!m_swFilterGraph)
        return false;

    const AVFilter *bufferFilter = avfilter_get_by_name("buffer");
    const AVFilter *sinkFilter = avfilter_get_by_name("buffersink");
    if (!bufferFilter || !sinkFilter) {
        teardownSwFilterGraph();
        return false;
    }

    m_swFilterSrc = avfilter_graph_alloc_filter(m_swFilterGraph, bufferFilter, "in");
    if (!m_swFilterSrc) {
        teardownSwFilterGraph();
        return false;
    }

    AVBufferSrcParameters *params = av_buffersrc_parameters_alloc();
    if (!params) {
        teardownSwFilterGraph();
        return false;
    }
    params->format = pixFmt;
    params->width = width;
    params->height = height;
    params->time_base = m_fmt->streams[m_videoStream]->time_base;
    const int paramsRc = av_buffersrc_parameters_set(m_swFilterSrc, params);
    av_free(params);
    if (paramsRc < 0 || avfilter_init_str(m_swFilterSrc, nullptr) < 0) {
        teardownSwFilterGraph();
        return false;
    }

    AVFilterContext *sink = nullptr;
    if (avfilter_graph_create_filter(&sink, sinkFilter, "out", nullptr, nullptr, m_swFilterGraph) < 0) {
        teardownSwFilterGraph();
        return false;
    }
    m_swFilterSink = sink;

    QString targetTrfPath = m_tempTrfPath.isEmpty() ? m_stabilizePath : m_tempTrfPath;
    int smoothing = m_stabilizeSmoothing > 0 ? m_stabilizeSmoothing : 15;
    int tripod = m_stabilizeTripod ? 1 : 0;
    QString filterDesc = QString("vidstabtransform=input='%1':zoom=15:smoothing=%2:tripod=%3")
                             .arg(targetTrfPath)
                             .arg(smoothing)
                             .arg(tripod);
    QByteArray filterStr = filterDesc.toUtf8();

    AVFilterInOut *outputs = avfilter_inout_alloc();
    AVFilterInOut *inputs = avfilter_inout_alloc();
    if (!outputs || !inputs) {
        if (outputs) avfilter_inout_free(&outputs);
        if (inputs) avfilter_inout_free(&inputs);
        teardownSwFilterGraph();
        return false;
    }

    outputs->name = av_strdup("in");
    outputs->filter_ctx = m_swFilterSrc;
    outputs->pad_idx = 0;
    outputs->next = nullptr;

    inputs->name = av_strdup("out");
    inputs->filter_ctx = m_swFilterSink;
    inputs->pad_idx = 0;
    inputs->next = nullptr;

    int rc = avfilter_graph_parse_ptr(m_swFilterGraph, filterStr.constData(), &inputs, &outputs, nullptr);
    avfilter_inout_free(&inputs);
    avfilter_inout_free(&outputs);

    if (rc < 0 || avfilter_graph_config(m_swFilterGraph, nullptr) < 0) {
        teardownSwFilterGraph();
        return false;
    }

    m_swFilterW = width;
    m_swFilterH = height;
    m_swFilterFormat = pixFmt;
    m_swFilterSmoothing = m_stabilizeSmoothing;
    m_swFilterTripod = m_stabilizeTripod;
    return true;
}

bool ClipReader::ensureHwScaler(const AVFrame *hwFrame, int targetWidth, int targetHeight)
{
    if (m_hwScalerFailed || !hwFrame->hw_frames_ctx)
        return false;

    // A backend with no surface scaler (D3D11VA) leaves the full-size hardware
    // frame for the GL importer to downscale.
    const char *scalerName = drift::hwaccel::scaleFilter(m_hwBackend);
    if (!scalerName) {
        m_hwScalerFailed = true;
        return false;
    }

    if (m_vppGraph && m_vppW == targetWidth && m_vppH == targetHeight && m_vppFramesCtx
        && m_vppFramesCtx->data == hwFrame->hw_frames_ctx->data) {
        return true;
    }

    const AVFilter *bufferFilter = avfilter_get_by_name("buffer");
    const AVFilter *sinkFilter = avfilter_get_by_name("buffersink");
    const AVFilter *scaleFilter = avfilter_get_by_name(scalerName);
    if (!bufferFilter || !sinkFilter || !scaleFilter) {
        m_hwScalerFailed = true;
        return false;
    }

    const QByteArray sizeArgs =
        QByteArray("w=") + QByteArray::number(targetWidth) + ":h=" + QByteArray::number(targetHeight);
    const QByteArray args[2] = {sizeArgs + ":format=nv12", sizeArgs};

    for (const QByteArray &scaleArgs : args) {
        teardownHwScaler();

        m_vppGraph = avfilter_graph_alloc();
        m_vppScaled = av_frame_alloc();
        m_swFrame = av_frame_alloc();
        if (!m_vppGraph || !m_vppScaled || !m_swFrame)
            continue;

        m_vppSrc = avfilter_graph_alloc_filter(m_vppGraph, bufferFilter, "in");
        if (!m_vppSrc)
            continue;

        AVBufferSrcParameters *params = av_buffersrc_parameters_alloc();
        if (!params)
            continue;
        params->format = hwFrame->format;
        params->width = hwFrame->width;
        params->height = hwFrame->height;
        params->time_base = m_fmt->streams[m_videoStream]->time_base;
        params->hw_frames_ctx = hwFrame->hw_frames_ctx;
        const int paramsRc = av_buffersrc_parameters_set(m_vppSrc, params);
        av_free(params);
        if (paramsRc < 0 || avfilter_init_str(m_vppSrc, nullptr) < 0)
            continue;

        AVFilterContext *scale = nullptr;
        if (avfilter_graph_create_filter(&scale, scaleFilter, "vpp", scaleArgs.constData(), nullptr,
                                         m_vppGraph)
                < 0
            || avfilter_graph_create_filter(&m_vppSink, sinkFilter, "out", nullptr, nullptr, m_vppGraph)
                < 0
            || avfilter_link(m_vppSrc, 0, scale, 0) < 0 || avfilter_link(scale, 0, m_vppSink, 0) < 0
            || avfilter_graph_config(m_vppGraph, nullptr) < 0)
            continue;

        m_vppFramesCtx = av_buffer_ref(hwFrame->hw_frames_ctx);
        m_vppW = targetWidth;
        m_vppH = targetHeight;
        return true;
    }

    teardownHwScaler();
    m_hwScalerFailed = true;
    return false;
}

const AVFrame *ClipReader::scaleHwFrame(const AVFrame *hwFrame, int targetWidth, int targetHeight)
{
    if (!hwFrame)
        return nullptr;

    bool needsFormat = false;
    if (hwFrame->hw_frames_ctx) {
        const auto *fc = reinterpret_cast<const AVHWFramesContext *>(hwFrame->hw_frames_ctx->data);
        needsFormat = fc && fc->sw_format != AV_PIX_FMT_NV12;
    }
    if (hwFrame->width == targetWidth && hwFrame->height == targetHeight && !needsFormat)
        return hwFrame;

    if (!ensureHwScaler(hwFrame, targetWidth, targetHeight))
        return hwFrame;

    av_frame_unref(m_vppScaled);
    if (av_buffersrc_add_frame_flags(m_vppSrc, const_cast<AVFrame *>(hwFrame),
                                     AV_BUFFERSRC_FLAG_KEEP_REF)
            >= 0
        && av_buffersink_get_frame(m_vppSink, m_vppScaled) >= 0)
        return m_vppScaled;

    m_hwScalerFailed = true;
    teardownHwScaler();
    return hwFrame;
}

AVFrame *ClipReader::hwFrameToSoftware(const AVFrame *hwFrame, int targetWidth, int targetHeight)
{
    const AVFrame *scaled = scaleHwFrame(hwFrame, targetWidth, targetHeight);
    if (!scaled)
        return nullptr;

    if (!m_swFrame) {
        m_swFrame = av_frame_alloc();
        if (!m_swFrame)
            return nullptr;
    }
    av_frame_unref(m_swFrame);
    if (av_hwframe_transfer_data(m_swFrame, scaled, 0) < 0) {
        av_frame_unref(m_swFrame);
        return nullptr;
    }
    if (scaled == m_vppScaled)
        av_frame_unref(m_vppScaled);
    return m_swFrame;
}

bool ClipReader::transferHwFrameToImage(const AVFrame *hwFrame, QImage &out, int targetWidth, int targetHeight)
{
    const AVFrame *swFrame = hwFrameToSoftware(hwFrame, targetWidth, targetHeight);
    if (!swFrame)
        return false;

    const QImage image = frameToRgba(swFrame, m_sws, targetWidth, targetHeight, m_sourceRotation);
    if (image.isNull())
        return false;

    out = image;
    return true;
}

AVFrame* ClipReader::filterFrameInPlace(AVFrame *frame, int targetWidth, int targetHeight)
{
    if (m_stabilizePath.isEmpty() || !QFile::exists(m_stabilizePath))
        return frame;

    const AVStream *videoStream = m_fmt->streams[m_videoStream];
    const AVRational timeBase = videoStream->time_base;
    const drift::TimeUs framePtsUs = av_rescale_q(frame->pts, timeBase, {1, drift::kUsPerSecond});
    drift::TimeUs startTimeUs = 0;
    if (videoStream->start_time != AV_NOPTS_VALUE) {
        startTimeUs = av_rescale_q(videoStream->start_time, videoStream->time_base, {1, drift::kUsPerSecond});
    }
    const drift::TimeUs relativePtsUs = framePtsUs - startTimeUs;
    double fps = av_q2d(videoStream->r_frame_rate);
    int frameIndex = qMax<int>(0, qRound(drift::usToSeconds(relativePtsUs) * fps));

    AVFrame *swFrame = frame;
    bool isHw = (m_hwAccelActive && frame->format == m_hwPixFmt)
                || isHardwarePixelFormat(static_cast<AVPixelFormat>(frame->format));
    if (isHw) {
        swFrame = hwFrameToSoftware(frame, targetWidth, targetHeight);
        if (!swFrame)
            return frame;
    }

    if (m_expectedNextFrameIndex == -1 || frameIndex != m_expectedNextFrameIndex) {
        if (m_tempTrfPath.isEmpty()) {
            const QString root = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
            const QString dir = QDir(root).filePath(QStringLiteral("stabilization_temp"));
            QDir().mkpath(dir);
            m_tempTrfPath = QDir(dir).filePath(QStringLiteral("temp-%1.trf").arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
        }
        int nativeWidth = m_fmt->streams[m_videoStream]->codecpar->width;
        int nativeHeight = m_fmt->streams[m_videoStream]->codecpar->height;
        double scaleX = nativeWidth > 0 ? double(swFrame->width) / double(nativeWidth) : 1.0;
        double scaleY = nativeHeight > 0 ? double(swFrame->height) / double(nativeHeight) : 1.0;
        if (sliceTrfFile(m_stabilizePath, m_tempTrfPath, frameIndex, scaleX, scaleY)) {
            teardownSwFilterGraph();
        }
    }

    if (initSwFilterGraph(swFrame->width, swFrame->height, static_cast<AVPixelFormat>(swFrame->format))) {
        int rc = av_buffersrc_add_frame_flags(m_swFilterSrc, swFrame, AV_BUFFERSRC_FLAG_KEEP_REF);
        if (rc >= 0) {
            AVFrame *filterOutFrame = av_frame_alloc();
            if (filterOutFrame) {
                rc = av_buffersink_get_frame(m_swFilterSink, filterOutFrame);
                if (rc >= 0) {
                    m_expectedNextFrameIndex = frameIndex + 1;
                    if (isHw) {
                        return filterOutFrame;
                    } else {
                        av_frame_unref(frame);
                        av_frame_move_ref(frame, filterOutFrame);
                        av_frame_free(&filterOutFrame);
                        return frame;
                    }
                }
                av_frame_free(&filterOutFrame);
            }
        }
    }

    return frame;
}

bool ClipReader::convertFrame(const AVFrame *frame, QImage &out, int targetWidth, int targetHeight)
{
    if (!frame)
        return false;

    if (m_hwAccelActive && frame->format == m_hwPixFmt)
        return transferHwFrameToImage(frame, out, targetWidth, targetHeight);

    if (isHardwarePixelFormat(static_cast<AVPixelFormat>(frame->format)))
        return transferHwFrameToImage(frame, out, targetWidth, targetHeight);

    const QImage image = frameToRgba(frame, m_sws, targetWidth, targetHeight, m_sourceRotation);
    if (image.isNull())
        return false;

    out = image;
    return true;
}

bool ClipReader::convertFramePreview(const AVFrame *frame, PreviewVideoFrame &out, int targetWidth,
                                     int targetHeight)
{
    if (!frame)
        return false;

    const bool hw = (m_hwAccelActive && frame->format == m_hwPixFmt)
        || isHardwarePixelFormat(static_cast<AVPixelFormat>(frame->format));
    if (hw) {
        const AVFrame *scaled = scaleHwFrame(frame, targetWidth, targetHeight);
        out = makePreviewFrame(scaled, m_sourceRotation);
        if (scaled == m_vppScaled)
            av_frame_unref(m_vppScaled);
        return out.isValid();
    }

    AVFrame *nv12 = softwareFrameToNv12(frame, m_swsNv12, targetWidth, targetHeight);
    out = takePreviewFrame(nv12, m_sourceRotation);
    return out.isValid();
}

bool ClipReader::ensureAudioDecoder()
{
    if (!m_fmt || m_audioStream < 0)
        return false;
    if (m_audioCtx)
        return true;

    const AVCodecParameters *par = m_fmt->streams[m_audioStream]->codecpar;
    const AVCodec *codec = avcodec_find_decoder(par->codec_id);
    if (!codec)
        return false;

    m_audioCtx = avcodec_alloc_context3(codec);
    if (!m_audioCtx)
        return false;
    if (avcodec_parameters_to_context(m_audioCtx, par) < 0) {
        avcodec_free_context(&m_audioCtx);
        return false;
    }
    if (avcodec_open2(m_audioCtx, codec, nullptr) < 0) {
        avcodec_free_context(&m_audioCtx);
        return false;
    }
    return true;
}

bool ClipReader::seekVideoStream(drift::TimeUs sourceUs)
{
    if (!ensureVideoDecoder())
        return false;

    AVStream *stream = m_fmt->streams[m_videoStream];
    const int64_t startTs = stream->start_time != AV_NOPTS_VALUE ? stream->start_time : 0;
    const int64_t targetTs = av_rescale_q(sourceUs, {1, AV_TIME_BASE}, stream->time_base) + startTs;
    // A seek invalidates decoder surfaces held in the cover/peek cursor.
    clearVideoCursor();
    if (av_seek_frame(m_fmt, m_videoStream, targetTs, AVSEEK_FLAG_BACKWARD) < 0) {
        if (sourceUs > 0)
            return false;
        av_seek_frame(m_fmt, m_videoStream, 0, AVSEEK_FLAG_BACKWARD);
    }
    avcodec_flush_buffers(m_videoCtx);
    m_videoPositioned = true;
    return true;
}

bool ClipReader::seekAudioStream(drift::TimeUs sourceUs)
{
    if (!ensureAudioDecoder())
        return false;

    AVStream *stream = m_fmt->streams[m_audioStream];
    const int64_t targetTs = av_rescale_q(sourceUs, {1, AV_TIME_BASE}, stream->time_base);
    if (av_seek_frame(m_fmt, m_audioStream, targetTs, AVSEEK_FLAG_BACKWARD) < 0)
        return false;
    avcodec_flush_buffers(m_audioCtx);
    if (m_swr)
        swr_free(&m_swr);
    return true;
}

drift::TimeUs ClipReader::videoPtsToUs(const AVFrame *frame) const
{
    if (!frame || !m_fmt || m_videoStream < 0)
        return 0;
    const AVStream *stream = m_fmt->streams[m_videoStream];
    int64_t pts = frame->best_effort_timestamp != AV_NOPTS_VALUE
                      ? frame->best_effort_timestamp
                      : frame->pts;
    if (pts == AV_NOPTS_VALUE)
        return 0;
    if (stream->start_time != AV_NOPTS_VALUE)
        pts -= stream->start_time;
    return av_rescale_q(pts, stream->time_base, {1, drift::kUsPerSecond});
}

void ClipReader::clearVideoCursor()
{
    if (m_coverFrame)
        av_frame_unref(m_coverFrame);
    if (m_peekFrame)
        av_frame_unref(m_peekFrame);
    m_hasCover = false;
    m_hasPeek = false;
    m_coverPtsUs = 0;
    m_peekPtsUs = 0;
}

void ClipReader::freeVideoCursor()
{
    av_frame_free(&m_coverFrame);
    av_frame_free(&m_peekFrame);
    m_hasCover = false;
    m_hasPeek = false;
    m_coverPtsUs = 0;
    m_peekPtsUs = 0;
}

bool ClipReader::coverHolds(drift::TimeUs sourceUs) const
{
    return m_hasCover && m_hasPeek && sourceUs >= m_coverPtsUs && sourceUs < m_peekPtsUs;
}

void ClipReader::promotePeekToCover()
{
    if (!m_hasPeek)
        return;
    if (m_coverFrame)
        av_frame_unref(m_coverFrame);
    std::swap(m_coverFrame, m_peekFrame);
    m_coverPtsUs = m_peekPtsUs;
    m_hasCover = true;
    m_hasPeek = false;
    m_peekPtsUs = 0;
}

bool ClipReader::refVideoFrame(AVFrame *&dst, const AVFrame *src)
{
    if (!src)
        return false;
    if (!dst) {
        dst = av_frame_alloc();
        if (!dst)
            return false;
    }
    av_frame_unref(dst);
    return av_frame_ref(dst, src) >= 0;
}

bool ClipReader::advanceVideoTo(drift::TimeUs sourceUs, int maxWidth, int maxHeight, bool *hwFailure)
{
    if (hwFailure)
        *hwFailure = false;
    if (!ensureVideoDecoder())
        return false;

    while (m_hasPeek && sourceUs >= m_peekPtsUs)
        promotePeekToCover();
    if (coverHolds(sourceUs))
        return true;
    if (m_hasCover && sourceUs == m_coverPtsUs)
        return true;

    const bool needSeek = !m_videoPositioned
                          || (m_hasCover && sourceUs < m_coverPtsUs)
                          || sourceUs - m_lastVideoPtsUs > kForwardSeekThresholdUs;
    if (needSeek && !seekVideoStream(sourceUs))
        return false;

    AVPacket *packet = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();
    if (!packet || !frame) {
        av_frame_free(&frame);
        av_packet_free(&packet);
        return false;
    }

    bool done = false;
    bool sawHwFailure = false;
    bool droppedPacket = false;

    auto markHwFailure = [&]() {
        if (m_hwAccelActive || m_mediaCodecActive) {
            sawHwFailure = true;
            done = true;
        }
    };

    auto receiveFrames = [&] {
        while (!done) {
            const int rc = avcodec_receive_frame(m_videoCtx, frame);
            if (rc == AVERROR(EAGAIN) || rc == AVERROR_EOF)
                break;
            if (rc < 0) {
                // VAAPI often fails here with "hardware accelerator failed to
                // decode picture". The frame may be partially initialized —
                // unref before any further use or free.
                av_frame_unref(frame);
                markHwFailure();
                break;
            }

            AVFrame *stabilized = filterFrameInPlace(frame, maxWidth, maxHeight);
            const drift::TimeUs ptsUs = videoPtsToUs(stabilized);
            m_lastVideoPtsUs = ptsUs;
            g_videoFramesDecoded.fetch_add(1, std::memory_order_relaxed);

            if (ptsUs <= sourceUs) {
                if (!refVideoFrame(m_coverFrame, stabilized)) {
                    if (stabilized != frame)
                        av_frame_free(&stabilized);
                    av_frame_unref(frame);
                    done = true;
                    break;
                }
                m_coverPtsUs = ptsUs;
                m_hasCover = true;
            } else {
                if (!refVideoFrame(m_peekFrame, stabilized)) {
                    if (stabilized != frame)
                        av_frame_free(&stabilized);
                    av_frame_unref(frame);
                    done = true;
                    break;
                }
                m_peekPtsUs = ptsUs;
                m_hasPeek = true;
                done = true;
            }
            if (stabilized != frame)
                av_frame_free(&stabilized);
            av_frame_unref(frame);
        }
    };

    bool eof = false;
    while (!done) {
        if (av_read_frame(m_fmt, packet) < 0) {
            eof = true;
            break;
        }
        if (packet->stream_index != m_videoStream) {
            av_packet_unref(packet);
            continue;
        }

        int sendRc = avcodec_send_packet(m_videoCtx, packet);
        // MediaCodec's input queue is finite, so EAGAIN here is routine rather than the
        // cannot-happen it is for a software decoder — and dropping the packet would corrupt
        // every frame up to the next keyframe. Drain, resend, and if it still will not fit,
        // give up the sequential position so the next read seeks instead of decoding from a hole.
        if (sendRc == AVERROR(EAGAIN) && m_mediaCodecActive) {
            receiveFrames();
            if (!done)
                sendRc = avcodec_send_packet(m_videoCtx, packet);
            if (sendRc == AVERROR(EAGAIN))
                droppedPacket = true;
        }
        av_packet_unref(packet);
        if (sendRc == AVERROR(EAGAIN)) {
            // Decoder is full; drain below then retry is handled by the next read.
            // Fall through to receive.
        } else if (sendRc < 0) {
            markHwFailure();
            continue;
        }

        receiveFrames();
    }

    // A frame-threaded decoder still holds several frames after the last packet is sent, so
    // running out of packets is not the same as running out of frames. Without this drain the
    // tail of every clip is undecodable — the loop above just ends and those frames are never
    // received, which is exactly what a seek near the end of a clip asks for. The audio path
    // has always drained here; the video path did not.
    bool drained = false;
    if (eof && !done && !sawHwFailure) {
        avcodec_send_packet(m_videoCtx, nullptr);
        receiveFrames();
        // Leaves the decoder usable; the demuxer is at EOF, so the next call has to seek.
        avcodec_flush_buffers(m_videoCtx);
        drained = true;
    }

    av_frame_unref(frame);
    av_frame_free(&frame);
    av_packet_free(&packet);

    if (sawHwFailure) {
        if (hwFailure)
            *hwFailure = true;
        m_videoPositioned = false;
        clearVideoCursor();
        return false;
    }

    if (!m_hasCover && m_hasPeek)
        promotePeekToCover();

    if (!m_hasCover) {
        m_videoPositioned = false;
        return false;
    }

    m_videoPositioned = !drained && !droppedPacket;
    return true;
}

bool ClipReader::decodeVideoFrameAtOnce(drift::TimeUs sourceUs, QImage &out, int maxWidth, int maxHeight,
                                        bool *hwFailure)
{
    if (hwFailure)
        *hwFailure = false;
    if (!ensureVideoDecoder())
        return false;

    applyDecodeSize(decodeSizeFor(maxWidth, maxHeight));

    while (m_hasPeek && sourceUs >= m_peekPtsUs)
        promotePeekToCover();
    if (coverHolds(sourceUs) && lookupCachedFrame(m_coverPtsUs, out))
        return true;
    if (lookupCachedFrame(sourceUs, out))
        return true;

    if (!advanceVideoTo(sourceUs, maxWidth, maxHeight, hwFailure))
        return false;
    if (!m_coverFrame)
        return false;

    QImage converted;
    if (!convertFrame(m_coverFrame, converted, m_decodeW, m_decodeH)) {
        if (m_hwAccelActive
            && (m_coverFrame->format == m_hwPixFmt
                || isHardwarePixelFormat(static_cast<AVPixelFormat>(m_coverFrame->format)))) {
            if (hwFailure)
                *hwFailure = true;
            m_videoPositioned = false;
        }
        return false;
    }
    out = converted;
    storeCachedFrame(m_coverPtsUs, converted);
    return true;
}

bool ClipReader::readVideoFrameAt(drift::TimeUs sourceUs, QImage &out, int maxWidth, int maxHeight)
{
    bool hwFailure = false;
    if (decodeVideoFrameAtOnce(sourceUs, out, maxWidth, maxHeight, &hwFailure))
        return true;

    if (!hwFailure)
        return false;

    // Sticky software fallback for this reader — continuing with a broken hardware
    // context is what triggers free(): invalid size on subsequent frames.
    if (!fallbackFromHardwareDecoder())
        return false;

    return decodeVideoFrameAtOnce(sourceUs, out, maxWidth, maxHeight, nullptr);
}

bool ClipReader::decodePreviewVideoFrameAtOnce(drift::TimeUs sourceUs, PreviewVideoFrame &out, int maxWidth,
                                               int maxHeight, bool *hwFailure)
{
    if (hwFailure)
        *hwFailure = false;
    if (!ensureVideoDecoder())
        return false;

    applyDecodeSize(decodeSizeFor(maxWidth, maxHeight));

    while (m_hasPeek && sourceUs >= m_peekPtsUs)
        promotePeekToCover();
    if (coverHolds(sourceUs) && lookupCachedPreview(m_coverPtsUs, out))
        return true;
    if (lookupCachedPreview(sourceUs, out))
        return true;

    if (!advanceVideoTo(sourceUs, maxWidth, maxHeight, hwFailure))
        return false;
    if (!m_coverFrame)
        return false;

    PreviewVideoFrame converted;
    if (!convertFramePreview(m_coverFrame, converted, m_decodeW, m_decodeH)) {
        if (m_hwAccelActive
            && (m_coverFrame->format == m_hwPixFmt
                || isHardwarePixelFormat(static_cast<AVPixelFormat>(m_coverFrame->format)))) {
            if (hwFailure)
                *hwFailure = true;
            m_videoPositioned = false;
        }
        return false;
    }
    out = converted;
    storeCachedPreview(m_coverPtsUs, converted);
    return true;
}

bool ClipReader::readPreviewVideoFrame(drift::TimeUs sourceUs, PreviewVideoFrame &out, int maxWidth,
                                       int maxHeight)
{
    if (!m_prefetching)
        m_lastRequestedPreviewUs = sourceUs;

    bool hwFailure = false;
    if (decodePreviewVideoFrameAtOnce(sourceUs, out, maxWidth, maxHeight, &hwFailure))
        return true;

    if (!hwFailure)
        return false;

    if (!fallbackFromHardwareDecoder())
        return false;

    return decodePreviewVideoFrameAtOnce(sourceUs, out, maxWidth, maxHeight, nullptr);
}

void ClipReader::prefetchNextVideoFrame(int maxWidth, int maxHeight)
{
    if (!m_videoPositioned || m_sourceFrameDurationUs <= 0)
        return;

    QImage ignored;
    readVideoFrameAt(m_lastVideoPtsUs + m_sourceFrameDurationUs, ignored, maxWidth, maxHeight);
}

bool ClipReader::prefetchNextPreviewVideoFrame(int maxWidth, int maxHeight, drift::TimeUs readAheadUs)
{
    m_readAheadUs = qMax<drift::TimeUs>(0, readAheadUs);
    trimPreviewCache();

    if (!m_videoPositioned || m_sourceFrameDurationUs <= 0)
        return false;

    drift::TimeUs target = m_lastVideoPtsUs + m_sourceFrameDurationUs;
    PreviewVideoFrame cached;
    while (target - m_lastRequestedPreviewUs < m_readAheadUs && lookupCachedPreview(target, cached))
        target += m_sourceFrameDurationUs;

    if (m_readAheadUs > 0 && target - m_lastRequestedPreviewUs >= m_readAheadUs)
        return false;

    PreviewVideoFrame ignored;
    m_prefetching = true;
    const bool decoded = readPreviewVideoFrame(target, ignored, maxWidth, maxHeight);
    m_prefetching = false;

    return decoded && wantsMorePreviewReadAhead();
}

int ClipReader::readAudioInterleaved(drift::TimeUs sourceStartUs, int sampleCount, int outputSampleRate,
                                     float *interleavedStereoOut)
{
    if (!interleavedStereoOut || sampleCount <= 0 || outputSampleRate <= 0)
        return 0;
    if (!ensureAudioDecoder())
        return 0;

    // Re-seek only on a real discontinuity. During normal playback the request
    // advances by exactly one buffer, so we keep decoding forward from where we
    // left off — no per-buffer seek, no resampler reset, no glitching.
    const bool rateChanged = m_outputSampleRate != outputSampleRate;
    m_outputSampleRate = outputSampleRate;
    const bool needSeek = rateChanged || !m_audioPositioned
                          || sourceStartUs < m_audioNextPtsUs - kAudioSeekToleranceUs
                          || sourceStartUs > m_audioNextPtsUs + kAudioForwardSeekThresholdUs;

    bool alignToStart = false;
    if (needSeek) {
        if (!seekAudioStream(sourceStartUs)) // flushes the codec and frees m_swr
            return 0;
        m_audioLeftover.clear();
        m_audioPositioned = true;
        alignToStart = true;
    }

    if (!m_swr) {
        AVChannelLayout outLayout = AV_CHANNEL_LAYOUT_STEREO;
        if (swr_alloc_set_opts2(&m_swr, &outLayout, AV_SAMPLE_FMT_FLT, outputSampleRate,
                                &m_audioCtx->ch_layout, static_cast<AVSampleFormat>(m_audioCtx->sample_fmt),
                                m_audioCtx->sample_rate, 0, nullptr)
                < 0
            || swr_init(m_swr) < 0) {
            if (m_swr)
                swr_free(&m_swr);
            return 0;
        }
    }

    AVPacket *packet = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();
    if (!packet || !frame) {
        av_frame_free(&frame);
        av_packet_free(&packet);
        return 0;
    }

    const AVRational timeBase = m_fmt->streams[m_audioStream]->time_base;
    QVector<float> scratch;
    int pendingDrop = 0; // leading output frames to discard so playback starts at sourceStartUs
    bool sentFlush = false;

    while (m_audioLeftover.size() < sampleCount * 2) {
        const int rc = avcodec_receive_frame(m_audioCtx, frame);
        if (rc == AVERROR(EAGAIN)) {
            if (sentFlush)
                break;
            if (av_read_frame(m_fmt, packet) < 0) {
                avcodec_send_packet(m_audioCtx, nullptr); // drain the decoder at EOF
                sentFlush = true;
                continue;
            }
            if (packet->stream_index != m_audioStream) {
                av_packet_unref(packet);
                continue;
            }
            avcodec_send_packet(m_audioCtx, packet);
            av_packet_unref(packet);
            continue;
        }
        if (rc < 0) { // AVERROR_EOF or a decode error
            av_frame_unref(frame);
            break;
        }

        if (alignToStart) {
            const drift::TimeUs framePtsUs = ptsToUs(frame, timeBase);
            m_audioNextPtsUs = framePtsUs;
            if (sourceStartUs > framePtsUs)
                pendingDrop = static_cast<int>(((sourceStartUs - framePtsUs) * outputSampleRate)
                                               / drift::kUsPerSecond);
            alignToStart = false;
        }

        const int maxOut = swr_get_out_samples(m_swr, frame->nb_samples);
        scratch.resize(maxOut * 2);
        uint8_t *outData[1] = {reinterpret_cast<uint8_t *>(scratch.data())};
        const int converted = swr_convert(m_swr, outData, maxOut,
                                          const_cast<const uint8_t **>(frame->data), frame->nb_samples);
        if (converted <= 0)
            continue;

        int offset = 0;
        if (pendingDrop > 0) {
            const int drop = qMin(pendingDrop, converted);
            offset = drop;
            pendingDrop -= drop;
            m_audioNextPtsUs += static_cast<drift::TimeUs>(drop) * drift::kUsPerSecond / outputSampleRate;
        }
        for (int i = offset * 2; i < converted * 2; ++i)
            m_audioLeftover.append(scratch[i]);
    }

    av_frame_unref(frame);
    av_frame_free(&frame);
    av_packet_free(&packet);

    const int outFrames = qMin(sampleCount, static_cast<int>(m_audioLeftover.size() / 2));
    if (outFrames > 0) {
        std::memcpy(interleavedStereoOut, m_audioLeftover.constData(),
                    static_cast<size_t>(outFrames) * 2 * sizeof(float));
        m_audioLeftover.remove(0, outFrames * 2);
        m_audioNextPtsUs += static_cast<drift::TimeUs>(outFrames) * drift::kUsPerSecond / outputSampleRate;
    }
    return outFrames;
}
