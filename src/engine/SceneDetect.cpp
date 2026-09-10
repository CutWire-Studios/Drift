#include "SceneDetect.h"

#include "ClipReaderPool.h"
#include "ObjectDetector.h"
#include "MediaProbe.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QHash>
#include <QDateTime>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>
#include <QSaveFile>
#include <QStandardPaths>

#include <algorithm>
#include <cmath>

namespace drift {
namespace {

// Scales the median absolute deviation into a consistent estimator of sigma for
// normally distributed data. Same constant the reference implementation uses.
constexpr double kMadToSigma = 1.4826;

// The smallest mean-HSV delta that counts as a picture change at all, on the 0..255 scale.
// The derived threshold is raised to meet this before anything else looks at it, which is
// what stops a locked-off shot from promoting sensor noise to cuts: there the statistics
// alone would happily land on a threshold of ~2, well inside the grain.
constexpr double kAbsoluteFloor = 4.0;

// Having been floored, the threshold must still sit this far above the typical frame
// difference to be believed. This is the other failure direction — handheld or grainy
// footage whose median difference is already high and whose spread is narrow, where the
// statistics suggest a threshold most frames would clear.
constexpr double kMinMedianRatio = 1.25;

// What counts as "the fixed threshold found implausibly little". Sampling runs at the
// source frame rate, so 1800 samples is about a minute of 30 fps footage: fewer than one
// cut per minute over a signal that long is the signature of a threshold the material
// never reaches, which is exactly the graded-footage failure the fallback exists for.
constexpr int kMinSamplesPerCut = 1800;

// Below this there is not enough signal for a median and MAD to mean anything.
constexpr int kMinSamplesForAdaptive = 120;

// Peaks above `threshold`, taken strongest-first, each suppressing its neighbourhood.
QList<int> peaksAbove(const QList<double> &diffs, double threshold, int minGapSamples)
{
    QList<int> candidates;
    for (int i = 0; i < diffs.size(); ++i) {
        if (diffs.at(i) > threshold)
            candidates.append(i);
    }
    if (candidates.isEmpty())
        return {};

    // Strongest first, and by index where two samples tie, so the result does not depend
    // on the sort being stable.
    std::sort(candidates.begin(), candidates.end(), [&diffs](int a, int b) {
        if (diffs.at(a) != diffs.at(b))
            return diffs.at(a) > diffs.at(b);
        return a < b;
    });

    QList<int> kept;
    for (int index : std::as_const(candidates)) {
        bool clashes = false;
        for (int already : std::as_const(kept)) {
            if (std::abs(index - already) < minGapSamples) {
                clashes = true;
                break;
            }
        }
        if (!clashes)
            kept.append(index);
    }

    std::sort(kept.begin(), kept.end());
    return kept;
}

} // namespace

void toHsv(const QImage &image, HsvFrame *out)
{
    const QImage src = image.format() == QImage::Format_RGBA8888
                           ? image
                           : image.convertToFormat(QImage::Format_RGBA8888);
    out->width = src.width();
    out->height = src.height();
    out->hsv.resize(qsizetype(out->width) * out->height * 3);

    uchar *dst = out->hsv.data();
    for (int y = 0; y < src.height(); ++y) {
        const uchar *row = src.constScanLine(y);
        for (int x = 0; x < src.width(); ++x) {
            const int r = row[x * 4];
            const int g = row[x * 4 + 1];
            const int b = row[x * 4 + 2];

            const int maxC = std::max({r, g, b});
            const int minC = std::min({r, g, b});
            const int delta = maxC - minC;

            int h = 0;
            if (delta != 0) {
                // 43 is 256/6: one sixth of the hue circle per primary sector.
                if (maxC == r)
                    h = (43 * (g - b) / delta + 256) % 256;
                else if (maxC == g)
                    h = 85 + 43 * (b - r) / delta;
                else
                    h = 171 + 43 * (r - g) / delta;
                h = (h + 256) % 256;
            }

            *dst++ = uchar(h);
            *dst++ = uchar(maxC == 0 ? 0 : 255 * delta / maxC);
            *dst++ = uchar(maxC);
        }
    }
}

FrameDelta compareFrames(const HsvFrame &a, const HsvFrame &b)
{
    FrameDelta result;
    if (!a.matches(b))
        return result;

    const int count = a.pixelCount();
    qint64 sumH = 0;
    qint64 sumS = 0;
    qint64 sumV = 0;
    int moved = 0;

    for (int i = 0; i < count; ++i) {
        const uchar *pa = a.hsv.data() + qsizetype(i) * 3;
        const uchar *pb = b.hsv.data() + qsizetype(i) * 3;

        // Hue is a circle, so 250 and 5 are neighbours rather than opposites. It is also
        // meaningless where there is no colour to speak of — grey pixels carry an
        // arbitrary hue that flickers freely between frames — so the difference is
        // weighted by whichever of the two pixels is less saturated. Without that,
        // desaturated and graded footage injects hue noise into the one signal that has
        // to stay clean for the adaptive threshold to work.
        int dh = std::abs(int(pa[0]) - int(pb[0]));
        if (dh > 128)
            dh = 256 - dh;
        sumH += qint64(dh) * std::min(pa[1], pb[1]) / 255;

        sumS += std::abs(int(pa[1]) - int(pb[1]));

        const int dv = std::abs(int(pa[2]) - int(pb[2]));
        sumV += dv;
        if (dv > kMotionPixelDelta)
            ++moved;
    }

    result.content = double(sumH + sumS + sumV) / (3.0 * count);
    result.motion = double(moved) / count;
    return result;
}

double medianOf(QList<double> values)
{
    if (values.isEmpty())
        return 0.0;
    const int mid = values.size() / 2;
    std::nth_element(values.begin(), values.begin() + mid, values.end());
    const double upper = values.at(mid);
    if (values.size() % 2 != 0)
        return upper;
    // Even count: the median is the mean of the two middle values, and everything below
    // `mid` is already partitioned, so the lower one is just the max of that side.
    const double lower = *std::max_element(values.begin(), values.begin() + mid);
    return (lower + upper) / 2.0;
}

double adaptiveThreshold(const QList<double> &values, double z)
{
    if (values.isEmpty())
        return 0.0;

    const double median = medianOf(values);

    QList<double> deviations;
    deviations.reserve(values.size());
    for (double v : values)
        deviations.append(std::abs(v - median));

    const double mad = medianOf(std::move(deviations)) * kMadToSigma;
    return median + z * std::max(mad, 1e-6);
}

QList<int> resolveCuts(const QList<double> &diffs, double threshold, int minGapSamples,
                       bool allowAdaptive, double adaptiveZ, double *usedThresholdOut,
                       bool *usedAdaptiveOut)
{
    minGapSamples = std::max(1, minGapSamples);

    if (usedThresholdOut)
        *usedThresholdOut = threshold;
    if (usedAdaptiveOut)
        *usedAdaptiveOut = false;

    if (diffs.isEmpty())
        return {};

    const QList<int> fixed = peaksAbove(diffs, threshold, minGapSamples);

    if (!allowAdaptive || diffs.size() < kMinSamplesForAdaptive)
        return fixed;

    // Only reach for the fallback when the fixed pass found implausibly little. A
    // threshold that is working must not be second-guessed.
    if (fixed.size() * kMinSamplesPerCut >= diffs.size())
        return fixed;

    // Floor first, then judge. A flat signal punctuated by real cuts has a MAD of nearly
    // zero, which puts the derived threshold right on top of the median — checking the
    // ratio before flooring would reject exactly the graded-footage case this exists for.
    const double derived = std::max(adaptiveThreshold(diffs, adaptiveZ), kAbsoluteFloor);
    const double median = medianOf(diffs);
    if (derived < median * kMinMedianRatio)
        return fixed;

    const QList<int> adaptive = peaksAbove(diffs, derived, minGapSamples);
    if (adaptive.size() <= fixed.size())
        return fixed;

    if (usedThresholdOut)
        *usedThresholdOut = derived;
    if (usedAdaptiveOut)
        *usedAdaptiveOut = true;
    return adaptive;
}

QList<double> perSecondLoudness(const float *mono, int frameCount, int sampleRate)
{
    if (!mono || frameCount <= 0 || sampleRate <= 0)
        return {};

    QList<double> dbfs;
    dbfs.reserve(frameCount / sampleRate + 1);

    for (int start = 0; start < frameCount; start += sampleRate) {
        const int count = std::min(sampleRate, frameCount - start);
        double energy = 0.0;
        for (int i = 0; i < count; ++i) {
            const double sample = double(mono[start + i]);
            energy += sample * sample;
        }
        const double rms = std::sqrt(energy / count);
        dbfs.append(rms > 0.0 ? std::max(kSilenceDbfs, 20.0 * std::log10(rms)) : kSilenceDbfs);
    }
    return dbfs;
}

double percentileOf(QList<double> values, double fraction)
{
    if (values.isEmpty())
        return 0.0;
    if (values.size() == 1)
        return values.first();

    std::sort(values.begin(), values.end());
    const double position = std::clamp(fraction, 0.0, 1.0) * (values.size() - 1);
    const int lower = int(position);
    const int upper = std::min(lower + 1, int(values.size()) - 1);
    const double weight = position - lower;
    return values.at(lower) * (1.0 - weight) + values.at(upper) * weight;
}

QList<double> normaliseByPercentileRange(const QList<double> &values)
{
    QList<double> out(values.size(), 0.0);
    if (values.isEmpty())
        return out;

    const double low = percentileOf(values, kNormalisePercentileLow);
    const double high = percentileOf(values, kNormalisePercentileHigh);
    if (high - low < 1e-6)
        return out;

    for (int i = 0; i < values.size(); ++i)
        out[i] = std::clamp((values.at(i) - low) / (high - low), 0.0, 1.0);
    return out;
}

QList<Scene> scenesFromCuts(const QList<int> &cutIndices, double sampleIntervalUs, TimeUs rangeIn,
                            TimeUs rangeOut)
{
    if (rangeOut <= rangeIn)
        return {};

    auto sceneAt = [](TimeUs in, TimeUs out) {
        Scene scene;
        scene.sourceIn = in;
        scene.sourceOut = out;
        scene.thumbnailUs = in + (out - in) / 2;
        return scene;
    };

    if (sampleIntervalUs <= 0.0 || cutIndices.isEmpty())
        return {sceneAt(rangeIn, rangeOut)};

    auto timeOfSample = [&](int index) {
        return rangeIn + TimeUs(std::llround(double(index) * sampleIntervalUs));
    };

    QList<Scene> scenes;
    TimeUs cursor = rangeIn;
    for (int index : cutIndices) {
        const TimeUs boundary = timeOfSample(index);
        // A cut landing on (or before) the running cursor would produce an empty or
        // inverted scene. Sorted input plus the minimum-gap suppression make that
        // unreachable in practice, but the partition guarantee is worth enforcing here
        // rather than trusting every caller.
        if (boundary <= cursor || boundary >= rangeOut)
            continue;
        scenes.append(sceneAt(cursor, boundary));
        cursor = boundary;
    }
    scenes.append(sceneAt(cursor, rangeOut));
    return scenes;
}

namespace {

constexpr int kCacheSchema = 1;

// Everything that would change the result, folded into one digest. The file's mtime and
// size are in here too, so replacing the media on disk invalidates the entry rather than
// silently describing footage that is no longer there.
QString cacheDigest(const SceneDetectRequest &request)
{
    const QFileInfo info(request.path);
    const SceneDetectOptions &o = request.options;

    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(info.absoluteFilePath().toUtf8());
    hash.addData(QByteArray::number(info.lastModified().toMSecsSinceEpoch()));
    hash.addData(QByteArray::number(info.size()));
    hash.addData(QByteArray::number(request.sourceIn));
    hash.addData(QByteArray::number(request.sourceOut));
    hash.addData(QByteArray::number(o.threshold, 'g', 10));
    hash.addData(QByteArray::number(o.minSceneSeconds, 'g', 10));
    hash.addData(QByteArray::number(o.adaptiveZ, 'g', 10));
    hash.addData(o.allowAdaptive ? "a1" : "a0");
    hash.addData(o.analyzeAudio ? "s1" : "s0");
    hash.addData(o.detectObjects ? "o1" : "o0");
    return QString::fromLatin1(hash.result().toHex());
}

QString cacheDir()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation))
        .filePath(QStringLiteral("scenes"));
}

// Rate the clip's audio is resampled to for loudness. Loudness needs no bandwidth, and
// this keeps the read cheap on long clips.
constexpr int kLoudnessSampleRate = 16000;

// How much audio is pulled per read. Long enough that the per-read overhead disappears,
// short enough that the buffer stays under a couple of megabytes.
constexpr int kLoudnessBlockSeconds = 10;

// Fill in Scene::loudness from the clip's audio track.
void applyLoudness(const SceneDetectRequest &request, SceneAnalysis *analysis,
                   const SceneProgressFn &progress)
{
    const TimeUs span = request.sourceOut - request.sourceIn;
    const int totalFrames = int(usToSeconds(span) * kLoudnessSampleRate);
    if (totalFrames <= 0)
        return;

    QList<float> mono;
    mono.reserve(totalFrames);

    const int blockFrames = kLoudnessBlockSeconds * kLoudnessSampleRate;
    std::vector<float> stereo(size_t(blockFrames) * 2);

    for (int offset = 0; offset < totalFrames; offset += blockFrames) {
        const int want = std::min(blockFrames, totalFrames - offset);
        const TimeUs at =
            request.sourceIn + secondsToUs(double(offset) / kLoudnessSampleRate);
        const int got = ClipReaderPool::instance().readAudioInterleaved(
            request.path, kSceneScanStreamId, at, want, kLoudnessSampleRate, stereo.data());
        if (got <= 0)
            break;

        for (int i = 0; i < got; ++i)
            mono.append(0.5f * (stereo[size_t(i) * 2] + stereo[size_t(i) * 2 + 1]));

        if (progress && !progress(1.0, QObject::tr("Measuring loudness…")))
            return;
    }

    if (mono.isEmpty())
        return;

    const QList<double> dbfs =
        perSecondLoudness(mono.constData(), int(mono.size()), kLoudnessSampleRate);
    if (dbfs.isEmpty())
        return;

    // Each scene's own mean level first, then one pass to rank them against each other.
    // Ranking has to happen across scenes: "loud" only means anything here relative to the
    // rest of this clip, and an absolute dBFS figure would just report the mastering.
    QList<double> perScene;
    perScene.reserve(analysis->scenes.size());
    for (const Scene &scene : std::as_const(analysis->scenes)) {
        const int from = int(usToSeconds(scene.sourceIn - request.sourceIn));
        const int to = std::max(from + 1, int(usToSeconds(scene.sourceOut - request.sourceIn)));

        double total = 0.0;
        int counted = 0;
        for (int i = std::max(0, from); i < std::min(to, int(dbfs.size())); ++i) {
            total += dbfs.at(i);
            ++counted;
        }
        perScene.append(counted > 0 ? total / counted : kSilenceDbfs);
    }

    const QList<double> ranked = normaliseByPercentileRange(perScene);
    for (int i = 0; i < analysis->scenes.size() && i < ranked.size(); ++i)
        analysis->scenes[i].loudness = ranked.at(i);
}

// Label each scene by sampling a few frames inside it. Runs as a second stage over the
// already-known scene list, which is the whole reason it is affordable.
void applyObjectLabels(const SceneDetectRequest &request, SceneAnalysis *analysis,
                       const SceneProgressFn &progress)
{
    ObjectDetector &detector = ObjectDetector::instance();
    if (!detector.available())
        return;

    QList<double> weights;
    weights.reserve(analysis->scenes.size());

    for (int i = 0; i < analysis->scenes.size(); ++i) {
        Scene &scene = analysis->scenes[i];

        // Per class, the best confidence seen anywhere in the shot, and the frame that
        // carried the most detected area — which becomes the scene's thumbnail.
        QHash<QString, double> best;
        double bestFrameArea = -1.0;
        double totalArea = 0.0;
        int totalCount = 0;

        for (int sample = 0; sample < kObjectSamplesPerScene; ++sample) {
            const double through = double(sample + 1) / (kObjectSamplesPerScene + 1);
            const TimeUs at = scene.sourceIn + TimeUs(double(scene.duration()) * through);

            const QImage frame = ClipReaderPool::instance().readVideoFrame(
                request.path, kSceneScanStreamId, at, 0, 0);
            if (frame.isNull())
                continue;

            const QList<Detection> detections = detector.detect(frame);
            const double frameArea = frame.width() > 0 && frame.height() > 0
                                         ? double(frame.width()) * frame.height()
                                         : 1.0;

            double sampleArea = 0.0;
            for (const Detection &detection : detections) {
                best[detection.label] = std::max(best.value(detection.label), detection.score);
                sampleArea += detection.box.width() * detection.box.height();
            }
            totalArea += sampleArea / frameArea;
            totalCount += int(detections.size());

            if (sampleArea > bestFrameArea) {
                bestFrameArea = sampleArea;
                if (!detections.isEmpty())
                    scene.thumbnailUs = at;
            }
        }

        QList<QString> names = best.keys();
        std::sort(names.begin(), names.end(), [&best](const QString &a, const QString &b) {
            return best.value(a) > best.value(b);
        });
        scene.labels = names;

        // Something worth looking at has subjects that fill some of the frame. Count and area
        // both matter, so combine them rather than picking one.
        const double meanArea = totalArea / kObjectSamplesPerScene;
        const double meanCount = double(totalCount) / kObjectSamplesPerScene;
        weights.append(meanArea * std::min(1.0, meanCount / 3.0));

        if (progress
            && !progress(double(i + 1) / analysis->scenes.size(),
                         QObject::tr("Identifying objects in scene %1 of %2…")
                             .arg(i + 1)
                             .arg(analysis->scenes.size()))) {
            return;
        }
    }

    const QList<double> ranked = normaliseByPercentileRange(weights);
    for (int i = 0; i < analysis->scenes.size() && i < ranked.size(); ++i)
        analysis->scenes[i].objects = ranked.at(i);

    analysis->objectsScanned = true;
}

// Compose the per-signal values into the single ranking score, renormalising over whichever
// signals actually ran so the result always spans 0..1.
void scoreScenes(SceneAnalysis *analysis)
{
    double weightSum = kMotionWeight;
    bool anyLoudness = false;
    for (const Scene &scene : std::as_const(analysis->scenes)) {
        if (scene.loudness > 0.0) {
            anyLoudness = true;
            break;
        }
    }
    if (anyLoudness)
        weightSum += kLoudnessWeight;
    if (analysis->objectsScanned)
        weightSum += kObjectWeight;

    for (Scene &scene : analysis->scenes) {
        double total = kMotionWeight * scene.motion;
        if (anyLoudness)
            total += kLoudnessWeight * scene.loudness;
        if (analysis->objectsScanned)
            total += kObjectWeight * scene.objects;
        scene.score = weightSum > 0.0 ? total / weightSum : 0.0;
    }
}

} // namespace

QString sceneCachePath(const SceneDetectRequest &request)
{
    return QDir(cacheDir()).filePath(cacheDigest(request) + QStringLiteral(".json"));
}

bool loadCachedAnalysis(const SceneDetectRequest &request, SceneAnalysis *out)
{
    if (!out || request.path.isEmpty())
        return false;

    QFile file(sceneCachePath(request));
    if (!file.open(QIODevice::ReadOnly))
        return false;

    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    if (root.value(QStringLiteral("schema")).toInt() != kCacheSchema)
        return false;

    const QJsonArray sceneArray = root.value(QStringLiteral("scenes")).toArray();
    if (sceneArray.isEmpty())
        return false;

    SceneAnalysis analysis;
    analysis.detector = root.value(QStringLiteral("detector")).toString();
    analysis.thresholdUsed = root.value(QStringLiteral("thresholdUsed")).toDouble();
    analysis.adaptiveUsed = root.value(QStringLiteral("adaptiveUsed")).toBool();
    analysis.objectsScanned = root.value(QStringLiteral("objectsScanned")).toBool();

    for (const QJsonValue &value : sceneArray) {
        const QJsonObject o = value.toObject();
        Scene scene;
        scene.sourceIn = TimeUs(o.value(QStringLiteral("in")).toDouble());
        scene.sourceOut = TimeUs(o.value(QStringLiteral("out")).toDouble());
        scene.thumbnailUs = TimeUs(o.value(QStringLiteral("thumb")).toDouble());
        scene.motion = o.value(QStringLiteral("motion")).toDouble();
        scene.loudness = o.value(QStringLiteral("loudness")).toDouble();
        scene.objects = o.value(QStringLiteral("objects")).toDouble();
        scene.score = o.value(QStringLiteral("score")).toDouble();
        for (const QJsonValue &label : o.value(QStringLiteral("labels")).toArray())
            scene.labels.append(label.toString());
        if (scene.sourceOut <= scene.sourceIn)
            return false; // a partition this broken is not worth trusting
        analysis.scenes.append(scene);
    }

    for (const QJsonValue &value : root.value(QStringLiteral("cuts")).toArray()) {
        const QJsonObject o = value.toObject();
        SceneCut cut;
        cut.sourceUs = TimeUs(o.value(QStringLiteral("at")).toDouble());
        cut.strength = o.value(QStringLiteral("strength")).toDouble();
        analysis.cuts.append(cut);
    }

    *out = analysis;
    return true;
}

void storeCachedAnalysis(const SceneDetectRequest &request, const SceneAnalysis &analysis)
{
    if (analysis.isEmpty() || request.path.isEmpty())
        return;
    if (!QDir().mkpath(cacheDir()))
        return;

    QJsonArray sceneArray;
    for (const Scene &scene : analysis.scenes) {
        QJsonArray labels;
        for (const QString &label : scene.labels)
            labels.append(label);
        sceneArray.append(QJsonObject{
            {QStringLiteral("in"), double(scene.sourceIn)},
            {QStringLiteral("out"), double(scene.sourceOut)},
            {QStringLiteral("thumb"), double(scene.thumbnailUs)},
            {QStringLiteral("motion"), scene.motion},
            {QStringLiteral("loudness"), scene.loudness},
            {QStringLiteral("objects"), scene.objects},
            {QStringLiteral("score"), scene.score},
            {QStringLiteral("labels"), labels},
        });
    }

    QJsonArray cutArray;
    for (const SceneCut &cut : analysis.cuts) {
        cutArray.append(QJsonObject{{QStringLiteral("at"), double(cut.sourceUs)},
                                    {QStringLiteral("strength"), cut.strength}});
    }

    const QJsonObject root{
        {QStringLiteral("schema"), kCacheSchema},
        // Not read back — the digest already covers identity. Here so a stale cache file
        // can be traced to its source by hand.
        {QStringLiteral("source"), request.path},
        {QStringLiteral("detector"), analysis.detector},
        {QStringLiteral("thresholdUsed"), analysis.thresholdUsed},
        {QStringLiteral("adaptiveUsed"), analysis.adaptiveUsed},
        {QStringLiteral("objectsScanned"), analysis.objectsScanned},
        {QStringLiteral("scenes"), sceneArray},
        {QStringLiteral("cuts"), cutArray},
    };

    QSaveFile file(sceneCachePath(request));
    if (!file.open(QIODevice::WriteOnly))
        return;
    file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
    file.commit();
}

SceneAnalysis detectScenes(const SceneDetectRequest &request, const SceneProgressFn &progress,
                           QString *errorOut)
{
    auto fail = [errorOut](const QString &message) {
        if (errorOut)
            *errorOut = message;
        return SceneAnalysis{};
    };

    if (request.path.isEmpty() || request.sourceOut <= request.sourceIn)
        return fail(QObject::tr("Nothing to scan in this clip"));

    const MediaInfo info = MediaProbe::probe(request.path);
    if (!info.ok)
        return fail(info.errorString.isEmpty() ? QObject::tr("Could not open the media file")
                                               : info.errorString);

    double sourceFps = 0.0;
    bool hasAudio = false;
    for (const StreamInfo &stream : info.streams) {
        if (stream.type == StreamInfo::Type::Video && !stream.attachedPicture && sourceFps <= 0.0)
            sourceFps = stream.fps;
        else if (stream.type == StreamInfo::Type::Audio)
            hasAudio = true;
    }
    if (sourceFps <= 0.0)
        return fail(QObject::tr("This file has no video to scan"));

    // The source rate, not the project rate: sampling off-grid would beat against the real
    // frames and manufacture differences that are not in the footage. Kept as a double so
    // rates that do not divide a second evenly (30, 24, 29.97) do not accumulate drift.
    const double sampleFps = std::min(sourceFps, kMaxSampleFps);
    const double intervalUs = std::max(1.0, double(kUsPerSecond) / sampleFps);
    const TimeUs span = request.sourceOut - request.sourceIn;
    const int sampleCount = int(std::ceil(double(span) / intervalUs));
    if (sampleCount <= 1) {
        // Too short to have an interior cut, but still a scene.
        SceneAnalysis analysis;
        analysis.scenes = scenesFromCuts({}, intervalUs, request.sourceIn, request.sourceOut);
        analysis.thresholdUsed = request.options.threshold;
        return analysis;
    }

    QList<double> diffs;
    QList<double> motion;
    diffs.reserve(sampleCount);
    motion.reserve(sampleCount);

    HsvFrame previous;
    HsvFrame current;
    bool havePrevious = false;

    for (int i = 0; i < sampleCount; ++i) {
        const TimeUs sourceUs = request.sourceIn + TimeUs(std::llround(double(i) * intervalUs));
        const QImage frame = ClipReaderPool::instance().readVideoFrame(
            request.path, kSceneScanStreamId, sourceUs, kScanFrameWidth, kScanFrameHeight);
        if (frame.isNull())
            return fail(QObject::tr("Could not decode frame %1").arg(i + 1));

        toHsv(frame, &current);

        if (havePrevious) {
            const FrameDelta delta = compareFrames(previous, current);
            diffs.append(delta.content);
            motion.append(delta.motion);
        } else {
            // Nothing precedes the first sample, so it can never be a cut.
            diffs.append(0.0);
            motion.append(0.0);
            havePrevious = true;
        }

        std::swap(previous, current);

        if (progress
            && !progress(double(i + 1) / sampleCount,
                         QObject::tr("Scanning frame %1 of %2…").arg(i + 1).arg(sampleCount))) {
            return fail(QString());
        }
    }

    const int minGapSamples = std::max(1, int(request.options.minSceneSeconds * sampleFps));

    SceneAnalysis analysis;
    analysis.detector = QStringLiteral("content");
    const QList<int> cutIndices =
        resolveCuts(diffs, request.options.threshold, minGapSamples, request.options.allowAdaptive,
                    request.options.adaptiveZ, &analysis.thresholdUsed, &analysis.adaptiveUsed);

    analysis.scenes =
        scenesFromCuts(cutIndices, intervalUs, request.sourceIn, request.sourceOut);

    // Cut strengths are relative to the strongest in this analysis, so the UI can shade
    // them without the caller needing to know the metric's scale.
    double strongest = 0.0;
    for (int index : cutIndices)
        strongest = std::max(strongest, diffs.at(index));
    for (int index : cutIndices) {
        SceneCut cut;
        cut.sourceUs = request.sourceIn + TimeUs(std::llround(double(index) * intervalUs));
        cut.strength = strongest > 0.0 ? diffs.at(index) / strongest : 0.0;
        analysis.cuts.append(cut);
    }

    // Per-scene motion, then ranked against the other scenes the same way loudness is, so
    // both answer "how active is this relative to the rest of the clip" rather than
    // depending on an absolute pixel scale.
    QList<double> perSceneMotion;
    perSceneMotion.reserve(analysis.scenes.size());
    for (const Scene &scene : std::as_const(analysis.scenes)) {
        const int from = int(double(scene.sourceIn - request.sourceIn) / intervalUs);
        const int to = int(double(scene.sourceOut - request.sourceIn) / intervalUs);
        double total = 0.0;
        int counted = 0;
        for (int i = std::max(0, from); i < std::min(to, int(motion.size())); ++i) {
            total += motion.at(i);
            ++counted;
        }
        perSceneMotion.append(counted > 0 ? total / counted : 0.0);
    }
    const QList<double> rankedMotion = normaliseByPercentileRange(perSceneMotion);
    for (int i = 0; i < analysis.scenes.size() && i < rankedMotion.size(); ++i)
        analysis.scenes[i].motion = rankedMotion.at(i);

    if (request.options.analyzeAudio && hasAudio)
        applyLoudness(request, &analysis, progress);

    if (request.options.detectObjects)
        applyObjectLabels(request, &analysis, progress);

    scoreScenes(&analysis);
    return analysis;
}

} // namespace drift
