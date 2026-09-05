#pragma once

#include "HwAccel.h"
#include "PreviewVideoFrame.h"
#include "core/Time.h"

#include <QImage>
#include <QList>
#include <QSize>
#include <QString>
#include <QVector>

#include <optional>

extern "C" {
#include <libavutil/pixfmt.h>
struct AVFrame;
}

// Threaded-capable demux/decode for a single media file.
// Opens its own AVFormatContext; seeks via keyframe + forward decode.
class ClipReader
{
public:
    ClipReader();
    ~ClipReader();

    ClipReader(const ClipReader &) = delete;
    ClipReader &operator=(const ClipReader &) = delete;

    bool open(const QString &path, int audioStreamOrdinal = 0);
    void close();
    bool isOpen() const { return m_fmt != nullptr; }
    const QString &path() const { return m_path; }

    void setAudioStreamOrdinal(int ordinal);
    int audioStreamOrdinal() const { return m_audioStreamOrdinal; }

    bool hasVideo() const { return m_videoStream >= 0; }
    bool hasAudio() const { return m_audioStream >= 0; }

    // maxWidth/maxHeight bound the decode buffer; they are a hint, not an exact
    // size. The reader fits the source into that box (never upscaling) and keeps
    // the resulting decode size stable, so an animated clip transform does not
    // change the decode size — and therefore does not invalidate the frame cache
    // — on every frame. Callers scale the returned image to their layout rect.
    bool readVideoFrameAt(drift::TimeUs sourceUs, QImage &out, int maxWidth, int maxHeight);
    // Same seek/decode path as readVideoFrameAt, but returns an AVFrame handle for
    // the preview compositor (hardware surfaces stay on the GPU).
    bool readPreviewVideoFrame(drift::TimeUs sourceUs, PreviewVideoFrame &out, int maxWidth, int maxHeight);
    // Decode one frame past the current position into the cache, to overlap
    // decode with the caller's compositing work. Match the format of the last
    // read so prefetch does not consume a frame the other format still needs.
    void prefetchNextVideoFrame(int maxWidth, int maxHeight);
    // Preview read-ahead: decodes one frame and reports whether the cache is
    // still short of readAheadUs of decoded source past the last frame the
    // caller asked for. Callers step it one frame at a time so a real read never
    // waits behind more than a single decode. 0 keeps the old one-frame prefetch.
    bool prefetchNextPreviewVideoFrame(int maxWidth, int maxHeight, drift::TimeUs readAheadUs = 0);
    int readAudioInterleaved(drift::TimeUs sourceStartUs, int sampleCount, int outputSampleRate,
                             float *interleavedStereoOut);
    // Drop the sequential audio cursor so the next read seeks to the position it is given. The
    // fast path below only honours sourceStartUs on a discontinuity it can see; a seek of the
    // timeline playhead is one it cannot.
    void invalidateAudioPosition() { m_audioPositioned = false; }
    // How many readers on this media path divide the preview read-ahead budget between them. Several
    // exist when clips cut from one file overlap; each still keeps a cache, so without this the
    // memory ceiling would multiply by the number of them.
    void setPreviewCacheShare(int shares) { m_previewCacheShares = qMax(1, shares); }

    // Diagnostics, summed over every reader in this process. A reader asked for a position its
    // cursor is not near has to decode its way there from the preceding keyframe, so this is what
    // a badly shared decoder costs. Tests read the delta.
    static quint64 videoFramesDecoded();
    // Codec actually opened for video, empty until the first decode. Distinguishes libdav1d
    // (software AV1) from the native `av1` decoder that can drive hardware.
    QString videoDecoderName() const;
    bool hardwareAccelActive() const { return m_hwAccelActive; }

    // Preview toolbar: Auto (default) runs the per-clip heuristic, Software never
    // touches the GPU, Hardware always tries it. Drop the current video codec after
    // changing this so the next read opens on the new path.
    //
    // `backend` pins which one Hardware uses. None means "first that opens", which is
    // what Auto always does — a machine with both NVDEC and VAAPI is the only place
    // the distinction is visible, and it is the one place the auto-probe can pick the
    // worse of the two.
    enum class HardwareDecodeMode { Auto, Software, Hardware };
    static void setHardwareDecodeMode(HardwareDecodeMode mode,
                                      drift::hwaccel::Backend backend = drift::hwaccel::Backend::None);
    static HardwareDecodeMode hardwareDecodeMode();
    static drift::hwaccel::Backend pinnedDecodeBackend();
    void resetVideoDecoder();

    // What the last video decoder opened in this process actually landed on, summed
    // over every reader: Backend::None for software, empty until the first one opens.
    // Approximate by construction — clips of different codecs resolve differently —
    // but it is what the debug report needs to stop guessing.
    static std::optional<drift::hwaccel::Backend> activeDecodeBackend();
    // Times a reader gave up on hardware mid-decode and went sticky-software. Silent
    // otherwise: the preview just gets slower and nothing says why.
    static quint64 hardwareFallbackCount();

private:
    bool ensureVideoDecoder();
    bool ensureAudioDecoder();
    bool openSoftwareVideoDecoder();
    bool openHardwareDecoderWith(drift::hwaccel::Backend backend);
    bool tryOpenHardwareDecoder();
#ifdef Q_OS_ANDROID
    bool tryOpenMediaCodecDecoder();
#endif
    bool hardwareDecodeIsWorthIt() const;
    void teardownVideoDecoder();
    bool fallbackFromHardwareDecoder();

    // GPU-side downscale. Returns a hardware frame (the VPP output, or `hwFrame`
    // when the scaler is skipped/unavailable). Owned by the reader until the next
    // call. The QImage path then transfers; the preview path clones the surface.
    bool ensureHwScaler(const AVFrame *hwFrame, int targetWidth, int targetHeight);
    void teardownHwScaler();
    const AVFrame *scaleHwFrame(const AVFrame *hwFrame, int targetWidth, int targetHeight);
    AVFrame *hwFrameToSoftware(const AVFrame *hwFrame, int targetWidth, int targetHeight);

    bool transferHwFrameToImage(const AVFrame *hwFrame, QImage &out, int targetWidth, int targetHeight);
    bool convertFrame(const AVFrame *frame, QImage &out, int targetWidth, int targetHeight);
    bool convertFramePreview(const AVFrame *frame, PreviewVideoFrame &out, int targetWidth, int targetHeight);
    bool decodeVideoFrameAtOnce(drift::TimeUs sourceUs, QImage &out, int maxWidth, int maxHeight,
                                bool *hwFailure);
    bool decodePreviewVideoFrameAtOnce(drift::TimeUs sourceUs, PreviewVideoFrame &out, int maxWidth,
                                       int maxHeight, bool *hwFailure);
    bool seekVideoStream(drift::TimeUs sourceUs);
    bool seekAudioStream(drift::TimeUs sourceUs);

    // PTS in clip-local microseconds (best_effort, minus stream start_time).
    drift::TimeUs videoPtsToUs(const AVFrame *frame) const;
    // Cover/peek cursor: the last frame with pts <= the request, plus the next
    // decoded frame. Variable-frame-rate sources (game captures especially) have
    // gaps longer than a project tick; without the peek, that overshoot looks
    // like a backward jump and the next read re-seeks a whole GOP.
    void clearVideoCursor();
    void freeVideoCursor();
    bool coverHolds(drift::TimeUs sourceUs) const;
    void promotePeekToCover();
    bool refVideoFrame(AVFrame *&dst, const AVFrame *src);
    bool advanceVideoTo(drift::TimeUs sourceUs, int maxWidth, int maxHeight, bool *hwFailure);

    // Decode size fitted into the caller's box, quantized so small preview
    // resizes don't churn the sws context and the frame cache.
    QSize decodeSizeFor(int maxWidth, int maxHeight) const;
    void applyDecodeSize(const QSize &size);
    drift::TimeUs frameToleranceUs() const;
    bool lookupCachedFrame(drift::TimeUs sourceUs, QImage &out) const;
    void storeCachedFrame(drift::TimeUs ptsUs, const QImage &image);
    bool lookupCachedPreview(drift::TimeUs sourceUs, PreviewVideoFrame &out) const;
    void storeCachedPreview(drift::TimeUs ptsUs, const PreviewVideoFrame &frame);
    int previewCacheCapacity() const;
    void trimPreviewCache();
    bool wantsMorePreviewReadAhead() const;

    QString m_path;
    struct AVFormatContext *m_fmt = nullptr;
    struct AVCodecContext *m_videoCtx = nullptr;
    struct AVCodecContext *m_audioCtx = nullptr;
    struct AVBufferRef *m_hwDeviceCtx = nullptr;
    struct SwsContext *m_sws = nullptr;
    struct SwsContext *m_swsNv12 = nullptr;
    struct SwrContext *m_swr = nullptr;
    int m_videoStream = -1;
    int m_audioStream = -1;
    int m_audioStreamOrdinal = 0;
    // Source display-matrix rotation (0/90/180/270), applied to every decoded frame
    // so everything downstream sees upright pixels.
    int m_sourceRotation = 0;
    int m_outputSampleRate = 48000;
    bool m_hwAccelActive = false;
    bool m_hwAccelDisabled = false; // sticky after a failed hardware decode
    // Android MediaCodec decoding into ordinary system-memory frames. Never a hwaccel in the
    // AV_PIX_FMT_FLAG_HWACCEL sense — m_hwAccelActive and the surface scaler below stay
    // hwaccel-only — but it changes two things the decode loop cannot infer: send_packet may
    // legitimately return EAGAIN, and a decode error is recoverable by reopening in software.
    // Always false off Android.
    bool m_mediaCodecActive = false;
    drift::hwaccel::Backend m_hwBackend = drift::hwaccel::Backend::None;
    AVPixelFormat m_hwPixFmt = AV_PIX_FMT_NONE;

    // Surface-scaler graph, rebuilt when the decode size or the decoder's frame
    // pool changes. m_hwScalerFailed is sticky: a backend with no scaler, or a
    // driver whose scaler misbehaves, falls back to full-size transfers rather
    // than retrying per frame.
    struct AVFilterGraph *m_vppGraph = nullptr;
    struct AVFilterContext *m_vppSrc = nullptr;
    struct AVFilterContext *m_vppSink = nullptr;
    struct AVBufferRef *m_vppFramesCtx = nullptr;
    AVFrame *m_vppScaled = nullptr;
    AVFrame *m_swFrame = nullptr;
    int m_vppW = 0;
    int m_vppH = 0;
    bool m_hwScalerFailed = false;

    QString m_stabilizePath;
    int m_stabilizeSmoothing = 15;
    bool m_stabilizeTripod = false;
    int m_expectedNextFrameIndex = -1;
    QString m_tempTrfPath;
    struct AVFilterGraph *m_swFilterGraph = nullptr;
    struct AVFilterContext *m_swFilterSrc = nullptr;
    struct AVFilterContext *m_swFilterSink = nullptr;
    int m_swFilterW = 0;
    int m_swFilterH = 0;
    AVPixelFormat m_swFilterFormat = AV_PIX_FMT_NONE;
    int m_swFilterSmoothing = 15;
    bool m_swFilterTripod = false;

    bool initSwFilterGraph(int width, int height, AVPixelFormat pixFmt);
    void teardownSwFilterGraph();
    struct AVFrame* filterFrameInPlace(struct AVFrame *frame, int targetWidth, int targetHeight);

public:
    void setStabilizeParams(const QString &path, int smoothing, bool tripod) {
        m_stabilizePath = path;
        m_stabilizeSmoothing = smoothing;
        m_stabilizeTripod = tripod;
    }

private:

    // Sequential-decode state: lets playback decode forward without re-seeking
    // to a keyframe on every frame. Only seek on a backward jump or a large gap.
    bool m_videoPositioned = false;
    drift::TimeUs m_lastVideoPtsUs = 0;
    int m_decodeW = 0;
    int m_decodeH = 0;
    drift::TimeUs m_sourceFrameDurationUs = 0; // 0 until the stream is opened
    static constexpr drift::TimeUs kForwardSeekThresholdUs = 2 * drift::kUsPerSecond;

    // Cover = last source frame at or before the request; peek = the next one.
    // Playback holds cover until peek's PTS, so VFR gaps do not re-seek.
    AVFrame *m_coverFrame = nullptr;
    AVFrame *m_peekFrame = nullptr;
    drift::TimeUs m_coverPtsUs = 0;
    drift::TimeUs m_peekPtsUs = 0;
    bool m_hasCover = false;
    bool m_hasPeek = false;

    // Recently returned frames, most-recent first, keyed by source PTS. Backward
    // scrubbing and time_echo's history samples hit this instead of re-seeking.
    struct CachedFrame
    {
        drift::TimeUs ptsUs = 0;
        QImage image;
    };
    QList<CachedFrame> m_videoCache;
    struct CachedPreview
    {
        drift::TimeUs ptsUs = 0;
        PreviewVideoFrame frame;
    };
    QList<CachedPreview> m_previewCache;
    static constexpr int kMaxCachedFrames = 16;
    // Hardware surfaces live in FFmpeg's decoder pool. Holding 2 s of them would
    // exhaust extra_hw_frames and stall decode, so the GPU ring is short. Cover
    // and peek each hold one extra ref on top of the cache.
    //
    // Sized by time rather than by a flat frame count: a fixed 8 frames is a quarter second
    // of 30 fps material but only an eighth of 60 fps, so the buffer shrank exactly where
    // the frame budget was already halved. The cap is what the surface pool can spare, so
    // high frame rates get as much wall time as the pool allows rather than half of it.
    static constexpr drift::TimeUs kHwPreviewReadAheadUs = 300'000;
    static constexpr int kMinHwCachedFrames = 4;
    static constexpr int kMaxHwCachedFrames = 12;
    // Must stay ahead of kMaxHwCachedFrames plus the cover and peek refs and the decoder's
    // own reorder buffer, or the pool runs dry and decode stalls waiting for a free surface.
    static constexpr int kHwExtraFrames = 20;
    // Cover and peek hold one ref each on top of the cache; the rest is the decoder's own
    // reorder headroom. Raising the cache without raising the pool is what stalls decode.
    static_assert(kHwExtraFrames >= kMaxHwCachedFrames + 2 + 4,
                  "hardware surface pool must cover the preview cache, cover/peek, and the "
                  "decoder's reorder buffer");
    // Cover and peek hold one ref each on top of the cache; the rest is the decoder's own
    // reorder headroom. Raising the cache without raising the pool is what stalls decode.
    static_assert(kHwExtraFrames >= kMaxHwCachedFrames + 2 + 4,
                  "hardware surface pool must cover the preview cache, cover/peek, and the "
                  "decoder's reorder buffer");

    // Preview read-ahead. m_lastRequestedPreviewUs is the last position a caller
    // actually asked for — prefetch must not advance it, or the buffer would
    // always measure itself as one frame deep. m_prefetching marks those reads.
    drift::TimeUs m_readAheadUs = 0;
    drift::TimeUs m_lastRequestedPreviewUs = 0;
    bool m_prefetching = false;
    // Software read-ahead is held in RAM on top of the history above, so the depth
    // is capped by bytes as well as by time: the same 2 s is 60 frames of a 25 fps
    // 720p clip (~83 MB) but only a handful of 4K ones.
    static constexpr qsizetype kPreviewCacheByteBudget = 128 * 1024 * 1024;
    static constexpr int kMaxReadAheadFrames = 300;
    // Floor on the history slots even when the byte budget is tighter than they are. It shrinks
    // with the share so several readers on one path cannot each hold a full history.
    static constexpr int kMinCachedFrames = 4;
    int m_previewCacheShares = 1;

    // Sequential audio decode state (mirrors the video fast-path): keep the
    // resampler and demux position across buffers so contiguous playback decodes
    // straight through instead of re-seeking on every ~10 ms buffer.
    bool m_audioPositioned = false;
    drift::TimeUs m_audioNextPtsUs = 0; // source position of m_audioLeftover front
    QVector<float> m_audioLeftover;     // decoded-but-unreturned interleaved stereo
    static constexpr drift::TimeUs kAudioSeekToleranceUs = 50'000;
    static constexpr drift::TimeUs kAudioForwardSeekThresholdUs = 2 * drift::kUsPerSecond;
};
