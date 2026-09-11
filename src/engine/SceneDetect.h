#pragma once

#include "core/Time.h"

#include <QImage>
#include <QList>
#include <QString>
#include <QStringList>

#include <functional>

namespace drift {

// Offline scans need a decode cursor of their own so they never share one with timeline
// playback of the same media — see ClipReaderPool. This number comes from the same space
// as the kXxxStreamId constants in AppController.cpp; 0x01..0x05 are taken there.
constexpr quint64 kSceneScanStreamId = 0xA5'11'5C'A4'00'00'00'06ull;

// Shot-boundary detection over a clip's source range.
//
// Two layers, deliberately separated: the free functions below are pure — they take
// a per-frame difference signal and decide where the cuts are, with no decoding and
// no I/O, which is what makes the policy testable. SceneDetect::run() owns the
// decode loop that produces that signal.
//
// The metric is PySceneDetect's ContentDetector rather than the mean-|dY| the
// reference Python implementation uses: mean absolute delta of H, S and V averaged
// together, on the 0..255 scale. Frames are decoded into a 64x36 box, where the HSV
// conversion costs nothing and is far more robust to exposure drift and camera pans
// than a luma difference.

struct SceneCut
{
    TimeUs sourceUs = 0;   // absolute position in the source file
    double strength = 0.0; // 0..1, relative to the strongest cut in this analysis
};

struct Scene
{
    TimeUs sourceIn = 0;
    TimeUs sourceOut = 0;
    // Representative frame. The midpoint until the object pass picks something better.
    TimeUs thumbnailUs = 0;

    double motion = 0.0;   // 0..1, mean frame-delta energy across the scene
    double loudness = 0.0; // 0..1, squashed rolling-median z-score of the audio
    double objects = 0.0;  // 0..1, from the optional object pass; 0 when it did not run
    double score = 0.0;    // weighted combination of the three above

    QStringList labels; // COCO classes present, most confident first

    TimeUs duration() const { return sourceOut - sourceIn; }
};

struct SceneAnalysis
{
    QList<Scene> scenes; // gapless, non-overlapping partition of the scanned range
    QList<SceneCut> cuts;
    QString detector = QStringLiteral("content");
    bool objectsScanned = false;
    // The threshold actually applied, which differs from the requested one whenever
    // the adaptive fallback took over. Surfaced so the UI can explain itself.
    double thresholdUsed = 0.0;
    bool adaptiveUsed = false;

    bool isEmpty() const { return scenes.isEmpty(); }
};

// Tuning. Defaults match PySceneDetect's ContentDetector where there is a counterpart.
struct SceneDetectOptions
{
    double threshold = 27.0;      // mean HSV delta, 0..255
    double minSceneSeconds = 0.5; // shortest shot the detector will emit
    bool allowAdaptive = true;    // permit the median+MAD fallback
    double adaptiveZ = 4.0;
    // Score scenes by audio loudness as well as motion. Skipped automatically when the
    // file has no audio stream.
    bool analyzeAudio = true;
    // Label each scene with the objects in it. Needs the object-model addon; ignored when
    // that is not installed.
    bool detectObjects = false;
};

// How many frames of each scene the object pass looks at. Sampling inside known-homogeneous
// shots is what makes this cheap: three frames per scene rather than one per second means a
// ten-minute clip with sixty shots costs ~180 inferences instead of ~600, and the labels are
// better because every sampled frame is from a single shot rather than straddling a cut.
inline constexpr int kObjectSamplesPerScene = 3;

// Relative weights of the scored signals. They are kept separate on Scene, so these only
// decide how the single `score` column is composed. Objects contribute 0 until that pass
// has run, and the remaining two are renormalised so the score still spans 0..1.
inline constexpr double kMotionWeight = 0.4;
inline constexpr double kLoudnessWeight = 0.3;
inline constexpr double kObjectWeight = 0.3;

struct SceneDetectRequest
{
    QString path;
    TimeUs sourceIn = 0;
    TimeUs sourceOut = 0;
    SceneDetectOptions options;
};

// Reports progress and asks permission to continue: return false to cancel the scan.
// Same contract as WhisperTranscriber's callback.
using SceneProgressFn = std::function<bool(double fraction, const QString &status)>;

// Decode `request`'s range and work out where the shots begin. Returns an empty analysis
// on failure or cancellation, with the reason in `errorOut`.
//
// Frames are sampled at the source frame rate, capped at kMaxSampleFps. Sampling every
// frame matters: across a skipped gap a fast pan produces a larger difference than a real
// cut does, which is what forces the reference implementation into a threshold its own
// footage never reaches.
SceneAnalysis detectScenes(const SceneDetectRequest &request,
                           const SceneProgressFn &progress = {}, QString *errorOut = nullptr);

// --- caching ----------------------------------------------------------------
// Scene analysis describes the source media, not the project, so it is not stored in the
// project file — that would need a schema bump and a ProjectBundle role for something a
// rescan can always reproduce. It lives in the cache directory instead, keyed by the file's
// identity (path, mtime, size), the scanned range, and the options that shaped it.
//
// The range is part of the key, so re-trimming a clip and rescanning costs a fresh scan.
// That is the honest trade: analysing the whole file instead would make trims free but
// would scan an hour of source to place a thirty-second clip.
//
// There is deliberately only one key covering both stages. Labels from the object pass are
// attached to specific scene boundaries, so a threshold change that moves those boundaries
// invalidates them too — caching the two stages separately would risk pairing labels with
// scenes they were never computed for.

// Path of the cache file for this request, whether or not it exists yet.
QString sceneCachePath(const SceneDetectRequest &request);

// Returns false when nothing valid is cached for this exact request.
bool loadCachedAnalysis(const SceneDetectRequest &request, SceneAnalysis *out);

// Best-effort: a cache that cannot be written is not an error worth failing a scan over.
void storeCachedAnalysis(const SceneDetectRequest &request, const SceneAnalysis &analysis);

// Frames are decoded into this box. Small enough that the HSV conversion is free, large
// enough to survive a shot change that only moves part of the frame.
inline constexpr int kScanFrameWidth = 64;
inline constexpr int kScanFrameHeight = 36;

// High-frame-rate footage is sampled down to this. Past it the extra frames say nothing
// new about where the shots are and only cost decode time.
inline constexpr double kMaxSampleFps = 30.0;

// --- pure policy ------------------------------------------------------------
// Everything below operates on a difference signal alone. No decoding, no Qt beyond
// containers, no clock. These are what the unit tests drive.

// Median of `values`. Returns 0 for an empty list. Copies, so the caller keeps order.
double medianOf(QList<double> values);

// median + z * 1.4826 * MAD — the robust spread estimate the reference implementation
// falls back to when a fixed threshold finds nothing on graded footage. 1.4826 makes
// the MAD a consistent estimator of sigma for normally distributed data.
double adaptiveThreshold(const QList<double> &values, double z);

// Indices into `diffs` where a cut lands. `diffs[i]` is the difference between sampled
// frame i and frame i-1, so index i means "a cut immediately before frame i".
//
// Peaks above `threshold` are taken strongest-first, and each one suppresses everything
// within `minGapSamples` of it, so a dissolve spread over several frames yields one cut
// rather than a burst. When `allowAdaptive` is set and the fixed threshold finds
// implausibly little on a long signal, the adaptive threshold is tried instead — it is
// only accepted if it is meaningfully above the median and finds more than the fixed
// pass did.
//
// Returns ascending indices. `usedThresholdOut` / `usedAdaptiveOut` report which policy
// won, for the UI.
QList<int> resolveCuts(const QList<double> &diffs, double threshold, int minGapSamples,
                       bool allowAdaptive, double adaptiveZ = 4.0,
                       double *usedThresholdOut = nullptr, bool *usedAdaptiveOut = nullptr);

// Mean square energy per second, expressed in dBFS. `mono` is single-channel PCM in the
// usual -1..1 range. A second with no signal at all reports kSilenceDbfs rather than
// negative infinity, so the statistics downstream stay finite.
QList<double> perSecondLoudness(const float *mono, int frameCount, int sampleRate);

// Linear-interpolated percentile of `values`, with `fraction` in 0..1. Returns 0 for an
// empty list. Copies, so the caller keeps its order.
double percentileOf(QList<double> values, double fraction);

// Rescale `values` so the kNormalisePercentileLow percentile lands on 0 and the
// kNormalisePercentileHigh percentile lands on 1, clamping outside that. Returns all
// zeroes when the two percentiles are too close to separate — there is no meaningful
// ranking to report when every value is effectively the same.
//
// Percentiles rather than min/max because one outlier would otherwise flatten everything
// else: a single music sting at -6 dBFS in an hour of -30 dBFS dialogue would push every
// other scene to ~0 and destroy all discrimination among the scenes that matter.
QList<double> normaliseByPercentileRange(const QList<double> &values);

// A frame reduced to hue, saturation and value bytes, one triple per pixel. Keeping the
// converted form means the per-frame conversion happens once rather than twice.
struct HsvFrame
{
    QList<uchar> hsv; // h, s, v interleaved
    int width = 0;
    int height = 0;

    int pixelCount() const { return width * height; }
    bool matches(const HsvFrame &other) const
    {
        return width == other.width && height == other.height && width > 0 && height > 0;
    }
};

// RGB -> HSV with all three channels on the same 0..255 scale, so the per-channel deltas
// in compareFrames are directly comparable and can simply be averaged.
void toHsv(const QImage &image, HsvFrame *out);

struct FrameDelta
{
    double content = 0.0; // mean HSV delta, 0..255 — the cut metric
    double motion = 0.0;  // fraction of pixels whose value changed appreciably, 0..1
};

// How much a pixel's value must move before it counts as motion, on the 0..255 scale.
inline constexpr int kMotionPixelDelta = 12;

// Zero for frames of different sizes.
FrameDelta compareFrames(const HsvFrame &a, const HsvFrame &b);

inline constexpr double kSilenceDbfs = -100.0;
inline constexpr double kNormalisePercentileLow = 0.10;
inline constexpr double kNormalisePercentileHigh = 0.90;

// Turn cut indices into a gapless partition of [rangeIn, rangeOut). Sample `i` is taken at
// `rangeIn + i * sampleIntervalUs`, which must be the same grid the scan walked — deriving
// it from a sample count instead would compress the mapping, because the count is rounded
// up to cover the tail of the range.
//
// The interval is a double on purpose: at 30 fps the exact value is 33333.33…µs, and
// truncating it to whole microseconds loses 30µs per second — about three frames of drift
// across an hour of footage. Always returns at least one scene for a non-empty range: a
// single-shot clip is one scene, not an error.
QList<Scene> scenesFromCuts(const QList<int> &cutIndices, double sampleIntervalUs, TimeUs rangeIn,
                            TimeUs rangeOut);

} // namespace drift
