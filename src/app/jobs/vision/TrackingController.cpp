#include "app/jobs/vision/TrackingController.h"

#include "app/editor/AppController.h"
#include "app/mcp/McpDetail.h"
#include "core/edit/TimelineOps.h"
#include "core/tracking/MotionTracker.h"
#include "engine/compositor/layers/MaskApplier.h"
#include "engine/decode/readers/ClipReaderPool.h"
#include "engine/tracking/MotionTracking.h"

#include <QElapsedTimer>
#include <QPainterPath>
#include <QUuid>
#include <QtConcurrent>

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <optional>

using drift::Clip;
using drift::MotionTracker;
using drift::TimeUs;
using drift::TrackPoint;
using drift::TrackSample;
using drift::mcpdetail::findClipById;
using drift::tracking::GrayImage;
using drift::tracking::Homography;

struct TrackingController::FrameResult
{
    TimeUs sourceUs = 0;
    TimeUs timelineUs = 0;
    // One per point. A negative confidence means "no sample for this point on this frame".
    QList<TrackSample> samples;
    bool hasHomography = false;
    std::array<double, 9> homography{};
};

struct TrackingController::Job
{
    struct Point
    {
        QPointF pos;                    // normalized, at the start frame
        double patternWidth = 0.05;     // normalized
        QMap<TimeUs, QPointF> userKeys; // normalized
        bool skip = false;              // no sample at the start frame (added after the last run)
    };

    QString clipId;
    QString trackerId;
    QString path;
    int rotationCorrection = 0;
    bool planar = false;
    double minConfidence = 0.7;
    // Processing order; [0] is the start frame. Source times are decreasing for a backward run
    // on a forward clip, or a forward run on a reversed one.
    QVector<TimeUs> sourceTimes;
    QVector<TimeUs> timelineTimes;
    QVector<Point> points;
    QPolygonF region;     // planar, normalized
    Homography start{};   // planar: reference -> start frame, normalized
};

namespace {

constexpr quint64 kTrackStreamId = 0xA5'11'5C'A4'00'00'00'20ull;
constexpr int kAnalysisWidth = 960;
constexpr int kBackwardChunk = 16;
constexpr int kMaxGapFrames = 5;
constexpr int kTrailFrames = 15;
constexpr int kMaxPoints = 4;
constexpr int kMaxPathVertices = 64;
// Frame timestamps are rarely whole microseconds, and a read lands on the frame at or before the
// time asked for, so a nominal k/fps time that rounds down would return the previous frame.
constexpr TimeUs kDecodeNudgeUs = 1000;
constexpr int kRefreshInterval = 10;
constexpr float kRefreshConfidence = 0.9f;
constexpr double kScalarTolerancePx = 0.5;
constexpr double kRotationToleranceDeg = 0.05;

QVariantMap pointMap(double x, double y)
{
    return {{QStringLiteral("x"), x}, {QStringLiteral("y"), y}};
}

Homography fromTransform(const QTransform &t)
{
    return {t.m11(), t.m21(), t.dx(), t.m12(), t.m22(), t.dy(), 0.0, 0.0, 1.0};
}

Homography scaleMatrix(double sx, double sy)
{
    return {sx, 0.0, 0.0, 0.0, sy, 0.0, 0.0, 0.0, 1.0};
}

Homography normalized(Homography h)
{
    if (std::abs(h[8]) > 1e-12 && h[8] != 1.0) {
        const double s = h[8];
        for (double &e : h)
            e /= s;
    }
    return h;
}

// S * h * S^-1: the same map expressed in coordinates scaled by (sx, sy).
Homography conjugated(const Homography &h, double sx, double sy)
{
    return normalized(drift::tracking::composeHomography(
        scaleMatrix(sx, sy), drift::tracking::composeHomography(h, scaleMatrix(1.0 / sx, 1.0 / sy))));
}

Homography lerp(const Homography &a, const Homography &b, double t)
{
    Homography out;
    for (int i = 0; i < 9; ++i)
        out[i] = a[i] + (b[i] - a[i]) * t;
    return normalized(out);
}

MotionTracker *trackerById(Clip &clip, const QString &id)
{
    for (MotionTracker &tracker : clip.motionTrackers) {
        if (tracker.id == id)
            return &tracker;
    }
    return nullptr;
}

const MotionTracker *trackerById(const Clip &clip, const QString &id)
{
    for (const MotionTracker &tracker : clip.motionTrackers) {
        if (tracker.id == id)
            return &tracker;
    }
    return nullptr;
}

template<typename V>
typename QMap<TimeUs, V>::const_iterator nearestKey(const QMap<TimeUs, V> &map, TimeUs time, TimeUs tolerance)
{
    auto best = map.constEnd();
    TimeUs bestDistance = tolerance + 1;
    const auto above = map.lowerBound(time);
    if (above != map.constEnd() && above.key() - time < bestDistance) {
        best = above;
        bestDistance = above.key() - time;
    }
    if (above != map.constBegin()) {
        const auto below = std::prev(above);
        if (time - below.key() < bestDistance)
            best = below;
    }
    return best;
}

// How far apart a sample and a wanted source time may be and still count as the same frame.
TimeUs matchTolerance(const drift::Project &project, const Clip &host)
{
    return qMax<TimeUs>(
        1, static_cast<TimeUs>(std::llround(double(drift::frameDurationUs(project.fps()))
                                            * qMax(1.0, host.effectiveSpeed()) / 2.0)));
}

// Clip-frame size in pixels at a timeline time. Tracker coordinates are normalized, so this is only
// the scale in which rotation and scale are fitted: it has to be the shape the layer is drawn at.
QSizeF frameSizeAt(const drift::Project &project, const Clip &clip, TimeUs timelineUs)
{
    const TimeUs relative = qMax<TimeUs>(0, timelineUs - clip.timelineStart);
    const double w = clip.transformW.isEmpty() ? project.width() : clip.transformW.evaluateAt(relative);
    const double h = clip.transformH.isEmpty() ? project.height() : clip.transformH.evaluateAt(relative);
    return QSizeF(qMax(1.0, w), qMax(1.0, h));
}

TimeUs referenceTimelineUs(const Clip &host, const MotionTracker &tracker)
{
    return host.timelineStart + host.sourceUsToClipLocalUs(tracker.referenceUs);
}

TimeUs sampleTimelineUs(const Clip &host, TimeUs sourceUs)
{
    return host.timelineStart + host.sourceUsToClipLocalUs(sourceUs);
}

std::optional<Homography> solvePoints(MotionTracker::Solve solve, const QVector<QPointF> &src,
                                      const QVector<QPointF> &dst)
{
    using namespace drift::tracking;
    if (src.isEmpty())
        return std::nullopt;
    const int n = src.size();
    if (solve == MotionTracker::Solve::Perspective && n >= 4) {
        Homography h;
        if (fitHomography(src, dst, &h))
            return normalized(h);
    }
    QTransform t;
    if (solve != MotionTracker::Solve::Translate && n >= 2
        && fitSimilarity(src, dst, solve != MotionTracker::Solve::TranslateRotate, true, &t)) {
        return fromTransform(t);
    }
    if (fitTranslation(src, dst, &t))
        return fromTransform(t);
    return std::nullopt;
}

// Reference -> `sourceUs`, in clip-frame pixels, from the points that have a trusted sample.
std::optional<Homography> pointTransform(const MotionTracker &tracker, TimeUs sourceUs, TimeUs tolerance,
                                         QSizeF px)
{
    QVector<QPointF> src;
    QVector<QPointF> dst;
    for (const TrackPoint &point : tracker.points) {
        const auto ref = point.samples.constFind(tracker.referenceUs);
        const auto cur = nearestKey(point.samples, sourceUs, tolerance);
        if (ref == point.samples.constEnd() || cur == point.samples.constEnd())
            continue;
        if (!cur->userKey && cur->confidence < tracker.minConfidence)
            continue;
        src.append(QPointF(ref->pos.x() * px.width(), ref->pos.y() * px.height()));
        dst.append(QPointF(cur->pos.x() * px.width(), cur->pos.y() * px.height()));
    }
    return solvePoints(tracker.solve, src, dst);
}

// The tracked point at the reference frame, in clip-frame pixels: the centroid of the points that
// have a reference sample, or of the planar outline.
std::optional<QPointF> trackAnchor(const MotionTracker &tracker, QSizeF px)
{
    QPointF sum;
    int count = 0;
    if (tracker.kind == MotionTracker::Kind::Planar) {
        for (const QPointF &corner : tracker.planarRegion) {
            sum += corner;
            ++count;
        }
    } else {
        for (const TrackPoint &point : tracker.points) {
            const auto ref = point.samples.constFind(tracker.referenceUs);
            if (ref == point.samples.constEnd())
                continue;
            sum += ref->pos;
            ++count;
        }
    }
    if (count == 0)
        return std::nullopt;
    return QPointF(sum.x() / count * px.width(), sum.y() / count * px.height());
}

struct FrameXform
{
    TimeUs timelineUs = 0;
    TimeUs sourceUs = 0;
    bool has = false;
    Homography h = drift::tracking::identityHomography();
};

// One transform per project frame across the host's span, reference -> that frame, in clip-frame
// pixels. Short holes are bridged; longer ones stay empty.
QVector<FrameXform> frameTransforms(const drift::Project &project, const Clip &host,
                                    const MotionTracker &tracker, QSizeF px)
{
    const TimeUs frame = drift::frameDurationUs(project.fps());
    const TimeUs tolerance = matchTolerance(project, host);
    QVector<FrameXform> frames;
    for (TimeUs t = host.timelineStart; t < host.timelineEnd(); t += frame) {
        FrameXform f;
        f.timelineUs = t;
        f.sourceUs = host.timelineToSourceUs(t);
        if (tracker.kind == MotionTracker::Kind::Planar) {
            const auto it = nearestKey(tracker.homographies, f.sourceUs, tolerance);
            if (it != tracker.homographies.constEnd()) {
                f.h = conjugated(it.value(), px.width(), px.height());
                f.has = true;
            }
        } else if (const auto h = pointTransform(tracker, f.sourceUs, tolerance, px)) {
            f.h = *h;
            f.has = true;
        }
        frames.append(f);
    }

    int i = 0;
    while (i < frames.size()) {
        if (frames[i].has) {
            ++i;
            continue;
        }
        int j = i;
        while (j < frames.size() && !frames[j].has)
            ++j;
        if (i > 0 && j < frames.size() && j - i <= kMaxGapFrames) {
            for (int k = i; k < j; ++k) {
                frames[k].h = lerp(frames[i - 1].h, frames[j].h, double(k - i + 1) / (j - i + 1));
                frames[k].has = true;
            }
        }
        i = j;
    }
    return frames;
}

// Indices of consecutive frames that can carry a key on `owner`.
QVector<QVector<int>> keyableRuns(const QVector<FrameXform> &frames, const Clip &owner)
{
    QVector<QVector<int>> runs;
    QVector<int> current;
    for (int i = 0; i < frames.size(); ++i) {
        const bool usable = frames[i].has && frames[i].timelineUs >= owner.timelineStart
                            && frames[i].timelineUs < owner.timelineEnd();
        if (usable) {
            current.append(i);
            continue;
        }
        if (!current.isEmpty())
            runs.append(current);
        current.clear();
    }
    if (!current.isEmpty())
        runs.append(current);
    return runs;
}

// Indices to keep so linear interpolation between kept neighbours stays within `tolerance`.
// deviation(k, a, b) is how far item k sits from the line through a and b.
template<typename Deviation>
QVector<int> simplified(int count, double tolerance, Deviation deviation)
{
    QVector<int> kept;
    if (count == 0)
        return kept;
    QVector<bool> keep(count, false);
    keep[0] = keep[count - 1] = true;
    QVector<QPair<int, int>> stack{{0, count - 1}};
    while (!stack.isEmpty()) {
        const auto [a, b] = stack.takeLast();
        int worst = -1;
        double worstDeviation = tolerance;
        for (int k = a + 1; k < b; ++k) {
            const double d = deviation(k, a, b);
            if (d > worstDeviation) {
                worstDeviation = d;
                worst = k;
            }
        }
        if (worst >= 0) {
            keep[worst] = true;
            stack.append({a, worst});
            stack.append({worst, b});
        }
    }
    for (int i = 0; i < count; ++i) {
        if (keep[i])
            kept.append(i);
    }
    return kept;
}

struct Pose
{
    TimeUs t = 0; // relative to the key owner's start
    double x = 0;
    double y = 0;
    double w = 0;
    double h = 0;
    double rotation = 0;
};
using PoseRun = QVector<Pose>;

void unwrapRotation(PoseRun *run)
{
    for (int i = 1; i < run->size(); ++i) {
        double &r = (*run)[i].rotation;
        const double previous = (*run)[i - 1].rotation;
        while (r - previous > 180.0)
            r -= 360.0;
        while (r - previous < -180.0)
            r += 360.0;
    }
}

// Replaces the keys of `track` inside the span the runs cover. The last key of a run that another
// run follows holds, so the value does not slide across a stretch that has no tracking.
void writeChannel(drift::KeyframeTrack<double> &track, const QVector<PoseRun> &runs, double Pose::*member,
                  double tolerance)
{
    if (runs.isEmpty())
        return;
    const TimeUs lo = runs.first().first().t;
    const TimeUs hi = runs.last().last().t;
    QList<TimeUs> stale;
    for (auto it = track.keyframes().constBegin(); it != track.keyframes().constEnd(); ++it) {
        if (it.key() >= lo && it.key() <= hi)
            stale.append(it.key());
    }
    for (const TimeUs t : stale)
        track.removeKeyframe(t);

    for (int r = 0; r < runs.size(); ++r) {
        const PoseRun &run = runs.at(r);
        const QVector<int> kept = simplified(run.size(), tolerance, [&](int k, int a, int b) {
            const double u = double(run[k].t - run[a].t) / double(run[b].t - run[a].t);
            return std::abs(run[k].*member - (run[a].*member + (run[b].*member - run[a].*member) * u));
        });
        for (const int k : kept) {
            drift::Keyframe<double> key;
            key.value = run[k].*member;
            key.hold = k == run.size() - 1 && r + 1 < runs.size();
            track.setKeyframe(run[k].t, key);
        }
    }
}

// Undoes writeChannel: drops the keys inside [lo, hi] and, if none are left, holds the value the
// track had at `restT` (the reference frame, where the track wrote the follower's own pose).
// Returns false when the track ends up empty, for callers whose static value lives elsewhere.
bool stripChannel(drift::KeyframeTrack<double> &track, TimeUs lo, TimeUs hi, TimeUs restT, double *rest)
{
    if (track.isEmpty())
        return true;
    *rest = track.evaluateAt(restT);
    QList<TimeUs> stale;
    for (auto it = track.keyframes().constBegin(); it != track.keyframes().constEnd(); ++it) {
        if (it.key() >= lo && it.key() <= hi)
            stale.append(it.key());
    }
    for (const TimeUs t : stale)
        track.removeKeyframe(t);
    return !track.isEmpty();
}

struct PathPose
{
    TimeUs t = 0;
    QVector<QPointF> points; // normalized
};

QVector<QPointF> resampled(const QPolygonF &polygon, int count)
{
    QVector<double> cumulative{0.0};
    for (int i = 0; i < polygon.size(); ++i) {
        const QPointF a = polygon.at(i);
        const QPointF b = polygon.at((i + 1) % polygon.size());
        cumulative.append(cumulative.last() + std::hypot(b.x() - a.x(), b.y() - a.y()));
    }
    const double total = cumulative.last();
    QVector<QPointF> out;
    int edge = 0;
    for (int i = 0; i < count; ++i) {
        const double at = total * i / count;
        while (edge + 1 < cumulative.size() - 1 && cumulative[edge + 1] < at)
            ++edge;
        const double span = cumulative[edge + 1] - cumulative[edge];
        const double u = span > 1e-12 ? (at - cumulative[edge]) / span : 0.0;
        const QPointF a = polygon.at(edge);
        const QPointF b = polygon.at((edge + 1) % polygon.size());
        out.append(a + (b - a) * u);
    }
    return out;
}

// The mask's outline in clip-frame pixels, as a polygon the tracker can move. Empty with `error`
// set when the shape has no such outline.
QVector<QPointF> restVertices(const drift::Mask &rest, QSizeF px, QString *error)
{
    const QPointF centre(rest.x * px.width(), rest.y * px.height());
    QTransform spin;
    spin.translate(centre.x(), centre.y());
    spin.rotate(rest.rotation);
    spin.translate(-centre.x(), -centre.y());

    QVector<QPointF> out;
    switch (rest.shape) {
    case drift::MaskShape::Freeform:
        for (const QPointF &p : rest.points)
            out.append(QPointF(p.x() * px.width(), p.y() * px.height()));
        break;
    case drift::MaskShape::Rectangle: {
        const double hw = rest.w * px.width() / 2.0;
        const double hh = rest.h * px.height() / 2.0;
        for (const QPointF &corner : {QPointF(-hw, -hh), QPointF(hw, -hh), QPointF(hw, hh), QPointF(-hw, hh)})
            out.append(spin.map(centre + corner));
        break;
    }
    case drift::MaskShape::Ellipse: {
        const double hw = rest.w * px.width() / 2.0;
        const double hh = rest.h * px.height() / 2.0;
        for (int i = 0; i < 32; ++i) {
            const double a = 2.0 * M_PI * i / 32.0;
            out.append(spin.map(centre + QPointF(hw * std::cos(a), hh * std::sin(a))));
        }
        break;
    }
    case drift::MaskShape::Star:
    case drift::MaskShape::Heart: {
        const int wi = qMax(1, qRound(px.width()));
        const int hi = qMax(1, qRound(px.height()));
        QTransform spinInt;
        spinInt.translate(rest.x * wi, rest.y * hi);
        spinInt.rotate(rest.rotation);
        spinInt.translate(-rest.x * wi, -rest.y * hi);
        const QList<QPolygonF> polygons = spinInt.map(drift::maskPath(rest, wi, hi)).toSubpathPolygons();
        if (polygons.isEmpty())
            break;
        QPolygonF polygon = *std::max_element(polygons.begin(), polygons.end(),
                                              [](const QPolygonF &a, const QPolygonF &b) { return a.size() < b.size(); });
        if (polygon.size() > 1 && polygon.first() == polygon.last())
            polygon.removeLast();
        QVector<QPointF> points = polygon.size() > kMaxPathVertices
                                      ? resampled(polygon, kMaxPathVertices)
                                      : QVector<QPointF>(polygon.begin(), polygon.end());
        for (const QPointF &p : points)
            out.append(QPointF(p.x() / wi * px.width(), p.y() / hi * px.height()));
        break;
    }
    case drift::MaskShape::Bars:
    case drift::MaskShape::Media:
    case drift::MaskShape::None:
        break;
    }
    if (out.size() < 3 && error)
        *error = TrackingController::tr("This mask shape cannot follow a perspective or planar track");
    return out.size() < 3 ? QVector<QPointF>() : out;
}

struct HostFrame
{
    QPointF centre;
    double w = 1;
    double h = 1;
    double rotation = 0;
};

HostFrame hostFrameAt(const drift::Project &project, const Clip &host, TimeUs timelineUs)
{
    const TimeUs relative = qMax<TimeUs>(0, timelineUs - host.timelineStart);
    const auto value = [&](const drift::KeyframeTrack<double> &track, double fallback) {
        return track.isEmpty() ? fallback : track.evaluateAt(relative);
    };
    HostFrame f;
    f.w = qMax(1.0, value(host.transformW, project.width()));
    f.h = qMax(1.0, value(host.transformH, project.height()));
    f.centre = QPointF(value(host.transformX, 0.0) + f.w / 2.0, value(host.transformY, 0.0) + f.h / 2.0);
    f.rotation = value(host.rotation, 0.0);
    return f;
}

QPointF rotated(QPointF p, double degrees)
{
    const double a = qDegreesToRadians(degrees);
    return QPointF(p.x() * std::cos(a) - p.y() * std::sin(a), p.x() * std::sin(a) + p.y() * std::cos(a));
}

QVector<QPointF> parseRegion(const QVariantList &list)
{
    QVector<QPointF> out;
    for (const QVariant &entry : list) {
        const QVariantMap map = entry.toMap();
        out.append(QPointF(map.value(QStringLiteral("x")).toDouble(), map.value(QStringLiteral("y")).toDouble()));
    }
    return out;
}

// Drops non-key samples beyond the playhead in timeline order. The reference sample stays.
// Returns whether anything was removed.
bool eraseSamples(const drift::Project &project, const Clip &host, MotionTracker &tracker, TimeUs playheadUs,
                  bool forward)
{
    const TimeUs margin = drift::frameDurationUs(project.fps()) / 2;
    const auto beyond = [&](TimeUs key) {
        if (key == tracker.referenceUs)
            return false;
        const TimeUs at = sampleTimelineUs(host, key);
        return forward ? at > playheadUs + margin : at < playheadUs - margin;
    };
    bool erased = false;
    for (TrackPoint &point : tracker.points) {
        for (auto it = point.samples.begin(); it != point.samples.end();) {
            if (!it->userKey && beyond(it.key())) {
                it = point.samples.erase(it);
                erased = true;
            } else {
                ++it;
            }
        }
    }
    for (auto it = tracker.homographies.begin(); it != tracker.homographies.end();) {
        if (beyond(it.key())) {
            it = tracker.homographies.erase(it);
            erased = true;
        } else {
            ++it;
        }
    }
    return erased;
}

// ---- worker ---------------------------------------------------------------------------------

// Frames in processing order. A run through the source backwards decodes a chunk forward and then
// serves it in reverse: one seek per chunk instead of one per frame, for a chunk of gray frames in
// memory (about 2 MB each at 960 px).
class FrameFeed
{
public:
    FrameFeed(const TrackingController::Job &job, const QAtomicInt &cancel)
        : m_job(job)
        , m_cancel(cancel)
        , m_ascending(job.sourceTimes.size() < 2 || job.sourceTimes.last() >= job.sourceTimes.first())
    {
    }

    std::shared_ptr<const GrayImage> frame(int index)
    {
        if (m_ascending)
            return read(index);
        const int end = m_chunkStart + m_chunk.size();
        if (m_chunk.isEmpty() || index < m_chunkStart || index >= end) {
            const int count = qMin<int>(kBackwardChunk, m_job.sourceTimes.size() - index);
            m_chunkStart = index;
            m_chunk = QVector<std::shared_ptr<const GrayImage>>(count);
            for (int j = count - 1; j >= 0; --j) {
                m_chunk[j] = read(index + j);
                if (!m_chunk[j])
                    break;
            }
        }
        return m_chunk[index - m_chunkStart];
    }

private:
    std::shared_ptr<const GrayImage> read(int index) const
    {
        if (m_cancel.loadRelaxed() != 0)
            return nullptr;
        const QImage image = ClipReaderPool::instance().readVideoFrame(
            m_job.path, kTrackStreamId, m_job.sourceTimes.at(index) + kDecodeNudgeUs, kAnalysisWidth, kAnalysisWidth,
            m_job.rotationCorrection);
        if (image.isNull())
            return nullptr;
        return std::make_shared<const GrayImage>(drift::tracking::toGray(image, kAnalysisWidth));
    }

    const TrackingController::Job &m_job;
    const QAtomicInt &m_cancel;
    const bool m_ascending;
    int m_chunkStart = 0;
    QVector<std::shared_ptr<const GrayImage>> m_chunk;
};

struct Outcome
{
    QString message;
    QString severity = QStringLiteral("info");
};

using Sink = std::function<void(QList<TrackingController::FrameResult> &&, TimeUs, double)>;

class BatchWriter
{
public:
    explicit BatchWriter(Sink sink) : m_sink(std::move(sink)) { m_timer.start(); }

    void add(TrackingController::FrameResult &&frame, double progress)
    {
        m_last = frame.timelineUs;
        m_batch.append(std::move(frame));
        if (m_timer.elapsed() >= 100)
            flush(progress);
    }

    void flush(double progress)
    {
        if (m_batch.isEmpty())
            return;
        m_sink(std::move(m_batch), m_last, progress);
        m_batch.clear();
        m_timer.restart();
    }

private:
    Sink m_sink;
    QList<TrackingController::FrameResult> m_batch;
    TimeUs m_last = 0;
    QElapsedTimer m_timer;
};

Outcome trackPoints(const TrackingController::Job &job, FrameFeed &feed, const QAtomicInt &cancel, BatchWriter &writer)
{
    const int count = job.sourceTimes.size();
    const auto first = feed.frame(0);
    if (!first)
        return {TrackingController::tr("Could not decode the clip"), QStringLiteral("error")};

    struct State
    {
        QPointF pos; // analysis pixels
        std::shared_ptr<const GrayImage> ref;
        QPointF refPos;
        int since = 0;
        int radius = 8;
    };
    const double aw = first->width;
    const double ah = first->height;
    const auto toAnalysis = [&](QPointF n) { return QPointF(n.x() * aw - 0.5, n.y() * ah - 0.5); };
    const auto toNormalized = [&](QPointF a) { return QPointF((a.x() + 0.5) / aw, (a.y() + 0.5) / ah); };

    QVector<State> states;
    for (const auto &point : job.points) {
        State s;
        s.pos = toAnalysis(point.pos);
        s.ref = first;
        s.refPos = s.pos;
        s.radius = qMax(3, qRound(point.patternWidth * aw / 2.0));
        states.append(s);
    }

    drift::tracking::Pyramid previous = drift::tracking::buildPyramid(*first);
    const TimeUs keyTolerance = count > 1 ? qMax<TimeUs>(1, std::abs(job.sourceTimes[1] - job.sourceTimes[0]) / 2) : 1;

    for (int i = 1; i < count; ++i) {
        if (cancel.loadRelaxed() != 0) {
            writer.flush(double(i) / count);
            return {TrackingController::tr("Tracking cancelled")};
        }
        const auto gray = feed.frame(i);
        if (!gray) {
            writer.flush(double(i) / count);
            return cancel.loadRelaxed() != 0 ? Outcome{TrackingController::tr("Tracking cancelled")}
                                             : Outcome{TrackingController::tr("Could not decode frame %1").arg(i),
                                                       QStringLiteral("error")};
        }
        drift::tracking::Pyramid next = drift::tracking::buildPyramid(*gray);

        TrackingController::FrameResult result;
        result.sourceUs = job.sourceTimes.at(i);
        result.timelineUs = job.timelineTimes.at(i);
        bool anyGood = false;
        for (int p = 0; p < states.size(); ++p) {
            State &s = states[p];
            TrackSample sample;
            if (job.points.at(p).skip) {
                sample.confidence = -1.0f;
                result.samples.append(sample);
                continue;
            }
            const auto key = nearestKey(job.points.at(p).userKeys, result.sourceUs, keyTolerance);
            if (key != job.points.at(p).userKeys.constEnd()) {
                // A hand-placed key is ground truth: continue from it with a fresh reference.
                s.pos = toAnalysis(key.value());
                s.ref = gray;
                s.refPos = s.pos;
                s.since = 0;
                sample.pos = key.value();
                sample.confidence = -1.0f;
                anyGood = true;
            } else {
                const auto step = drift::tracking::trackPointStep(previous, next, s.pos, *s.ref, s.refPos, s.radius);
                s.pos = step.position;
                sample.pos = toNormalized(step.position);
                sample.confidence = step.lost ? 0.0f : step.confidence;
                if (!step.lost && step.confidence >= job.minConfidence)
                    anyGood = true;
                // A template older than a few frames stops matching a turning or growing target;
                // re-key it while the match is still trusted.
                if (!step.lost && step.confidence > kRefreshConfidence && ++s.since >= kRefreshInterval) {
                    s.ref = gray;
                    s.refPos = s.pos;
                    s.since = 0;
                }
            }
            result.samples.append(sample);
        }
        writer.add(std::move(result), double(i) / (count - 1));
        previous = std::move(next);
        if (!anyGood) {
            writer.flush(double(i) / (count - 1));
            return {TrackingController::tr("Tracking stopped: confidence dropped below the minimum"),
                    QStringLiteral("warning")};
        }
    }
    writer.flush(1.0);
    return {};
}

Outcome trackPlanar(const TrackingController::Job &job, FrameFeed &feed, const QAtomicInt &cancel, BatchWriter &writer)
{
    using namespace drift::tracking;
    const int count = job.sourceTimes.size();
    const auto first = feed.frame(0);
    if (!first)
        return {TrackingController::tr("Could not decode the clip"), QStringLiteral("error")};

    const double aw = first->width;
    const double ah = first->height;
    // Pixel (i, j) is sampled at the integer coordinate, so normalized u maps to u * w - 0.5.
    const Homography toAnalysis{aw, 0, -0.5, 0, ah, -0.5, 0, 0, 1};
    const Homography toNormalized{1.0 / aw, 0, 0.5 / aw, 0, 1.0 / ah, 0.5 / ah, 0, 0, 1};
    const QPolygonF region = mapPolygon(toAnalysis, job.region);

    Homography referenceToPrev = composeHomography(toAnalysis, composeHomography(job.start, toNormalized));
    Pyramid previous = buildPyramid(*first);
    QVector<QPointF> features = goodFeatures(previous, mapPolygon(referenceToPrev, region));
    int initialCount = features.size();
    if (initialCount < 8)
        return {TrackingController::tr("Not enough texture inside the outline to track"), QStringLiteral("warning")};

    for (int i = 1; i < count; ++i) {
        if (cancel.loadRelaxed() != 0) {
            writer.flush(double(i) / count);
            return {TrackingController::tr("Tracking cancelled")};
        }
        const auto gray = feed.frame(i);
        if (!gray) {
            writer.flush(double(i) / count);
            return cancel.loadRelaxed() != 0 ? Outcome{TrackingController::tr("Tracking cancelled")}
                                             : Outcome{TrackingController::tr("Could not decode frame %1").arg(i),
                                                       QStringLiteral("error")};
        }
        Pyramid next = buildPyramid(*gray);
        const PlanarStep step = trackPlanarStep(previous, next, features, referenceToPrev);
        if (!step.ok) {
            writer.flush(double(i) / (count - 1));
            return {TrackingController::tr("Tracking stopped: the surface was lost"), QStringLiteral("warning")};
        }

        TrackingController::FrameResult result;
        result.sourceUs = job.sourceTimes.at(i);
        result.timelineUs = job.timelineTimes.at(i);
        result.hasHomography = true;
        result.homography = normalized(composeHomography(toNormalized, composeHomography(step.referenceToNext, toAnalysis)));
        writer.add(std::move(result), double(i) / (count - 1));

        referenceToPrev = step.referenceToNext;
        features = step.features;
        previous = std::move(next);
        if (features.size() < 0.4 * initialCount || step.inlierRatio < 0.5) {
            features = goodFeatures(previous, mapPolygon(referenceToPrev, region));
            initialCount = features.size();
            if (initialCount < 8) {
                writer.flush(double(i) / (count - 1));
                return {TrackingController::tr("Tracking stopped: not enough texture left to follow"),
                        QStringLiteral("warning")};
            }
        }
    }
    writer.flush(1.0);
    return {};
}

} // namespace

TrackingController::TrackingController(AppController *app, QObject *parent)
    : QObject(parent)
    , m_app(app)
{
    // Undo, redo and every other edit funnel through tracksChanged, and trackers ride in the clips.
    connect(m_app, &AppController::tracksChanged, this, &TrackingController::trackersChanged);
}

// Every edit copies the project for undo first and only then takes a pointer: the copy shares
// storage until the first non-const access, and a pointer taken earlier would write into both.
drift::Clip *TrackingController::hostAt(int trackIndex, int clipIndex) const
{
    if (!m_app->isValidClipIndex(trackIndex, clipIndex))
        return nullptr;
    return &m_app->m_project.tracks()[trackIndex].clips[clipIndex];
}

const drift::Clip *TrackingController::hostView(int trackIndex, int clipIndex) const
{
    if (!m_app->isValidClipIndex(trackIndex, clipIndex))
        return nullptr;
    return &std::as_const(m_app->m_project).tracks().at(trackIndex).clips.at(clipIndex);
}

bool TrackingController::editTracker(int trackIndex, int clipIndex, const QString &id, const QString &undoText,
                                     const std::function<bool(Clip &, MotionTracker &)> &edit)
{
    if (m_busy || !hostView(trackIndex, clipIndex) || !trackerById(*hostView(trackIndex, clipIndex), id))
        return false;
    const drift::Project before = m_app->m_project;
    Clip *host = hostAt(trackIndex, clipIndex);
    MotionTracker *tracker = trackerById(*host, id);
    if (!edit(*host, *tracker))
        return false;
    m_app->pushProjectEdit(before, undoText);
    m_app->finishEdit(undoText);
    return true;
}

QVariantList TrackingController::trackers(int trackIndex, int clipIndex) const
{
    QVariantList out;
    const Clip *host = hostView(trackIndex, clipIndex);
    if (!host)
        return out;
    for (const MotionTracker &tracker : host->motionTrackers) {
        QVariantList targets;
        for (const drift::TrackerTarget &target : tracker.targets) {
            int t = -1;
            int c = -1;
            QString name;
            if (findClipById(m_app->m_project, target.clipId, &t, &c)) {
                const Clip &clip = m_app->m_project.tracks().at(t).clips.at(c);
                name = clip.type == drift::ClipType::Adjustment && !clip.mask.name.isEmpty() ? clip.mask.name
                                                                                              : clip.name;
                if (name.isEmpty() && clip.type == drift::ClipType::Adjustment)
                    name = drift::maskShapeToString(clip.mask.shape);
            }
            targets.append(QVariantMap{{QStringLiteral("clipId"), target.clipId},
                                       {QStringLiteral("mode"), drift::trackerTargetModeToString(target.mode)},
                                       {QStringLiteral("center"), target.centerOnTrack},
                                       {QStringLiteral("name"), name}});
        }

        TimeUs lo = std::numeric_limits<TimeUs>::max();
        TimeUs hi = std::numeric_limits<TimeUs>::min();
        const auto widen = [&](TimeUs key) {
            if (key < host->srcIn || key > host->srcOut)
                return;
            const TimeUs at = sampleTimelineUs(*host, key);
            lo = qMin(lo, at);
            hi = qMax(hi, at);
        };
        for (const TrackPoint &point : tracker.points) {
            for (auto it = point.samples.constBegin(); it != point.samples.constEnd(); ++it)
                widen(it.key());
        }
        for (auto it = tracker.homographies.constBegin(); it != tracker.homographies.constEnd(); ++it)
            widen(it.key());

        out.append(QVariantMap{
            {QStringLiteral("id"), tracker.id},
            {QStringLiteral("name"), tracker.name},
            {QStringLiteral("kind"), drift::motionTrackerKindToString(tracker.kind)},
            {QStringLiteral("solve"), drift::motionTrackerSolveToString(tracker.solve)},
            {QStringLiteral("minConfidence"), tracker.minConfidence},
            {QStringLiteral("pointCount"), tracker.points.size()},
            {QStringLiteral("referenceSeconds"), drift::usToSeconds(referenceTimelineUs(*host, tracker))},
            {QStringLiteral("targets"), targets},
            {QStringLiteral("sampleRange"),
             lo > hi ? QVariantList{} : QVariantList{drift::usToSeconds(lo), drift::usToSeconds(hi)}},
        });
    }
    return out;
}

QVariantMap TrackingController::trackerFrame(int trackIndex, int clipIndex, const QString &id,
                                             double timelineSeconds) const
{
    const Clip *host = hostView(trackIndex, clipIndex);
    const MotionTracker *tracker = host ? trackerById(*host, id) : nullptr;
    if (!tracker)
        return {};
    const drift::Project &project = m_app->m_project;
    const TimeUs tolerance = matchTolerance(project, *host);
    const TimeUs frame = drift::frameDurationUs(project.fps());
    const TimeUs at = drift::secondsToUs(timelineSeconds);
    const TimeUs sourceUs = host->timelineToSourceUs(at);

    QVariantList points;
    QVariantList trails;
    for (const TrackPoint &point : tracker->points) {
        const auto it = nearestKey(point.samples, sourceUs, tolerance);
        const bool has = it != point.samples.constEnd();
        QVariantMap entry{
            {QStringLiteral("x"), has ? it->pos.x() : 0.0},
            {QStringLiteral("y"), has ? it->pos.y() : 0.0},
            {QStringLiteral("confidence"), has ? double(it->confidence) : 0.0},
            {QStringLiteral("userKey"), has && it->userKey},
            {QStringLiteral("patternW"), point.patternSize.width()},
            {QStringLiteral("patternH"), point.patternSize.height()},
            {QStringLiteral("searchW"), point.searchSize.width()},
            {QStringLiteral("searchH"), point.searchSize.height()},
            {QStringLiteral("has"), has},
        };
        points.append(entry);

        QVariantList trail;
        for (int k = -kTrailFrames; k <= kTrailFrames; ++k) {
            const auto near = nearestKey(point.samples, host->timelineToSourceUs(at + k * frame), tolerance);
            if (near != point.samples.constEnd())
                trail.append(pointMap(near->pos.x(), near->pos.y()));
        }
        trails.append(trail);
    }

    QVariantList region;
    if (tracker->kind == MotionTracker::Kind::Planar) {
        QPolygonF polygon(tracker->planarRegion);
        const auto it = nearestKey(tracker->homographies, sourceUs, tolerance);
        if (it != tracker->homographies.constEnd())
            polygon = drift::tracking::mapPolygon(it.value(), polygon);
        for (const QPointF &p : polygon)
            region.append(pointMap(p.x(), p.y()));
    }

    return {{QStringLiteral("points"), points}, {QStringLiteral("region"), region}, {QStringLiteral("trail"), trails}};
}

QVariantList TrackingController::confidenceStrip(int trackIndex, int clipIndex, const QString &id, int buckets) const
{
    QVariantList out;
    const Clip *host = hostView(trackIndex, clipIndex);
    const MotionTracker *tracker = host ? trackerById(*host, id) : nullptr;
    if (!tracker || buckets <= 0)
        return out;
    QVector<double> lowest(buckets, -1.0);
    const auto note = [&](TimeUs key, double confidence) {
        if (key < host->srcIn || key > host->srcOut || host->timelineDuration <= 0)
            return;
        const TimeUs local = host->sourceUsToClipLocalUs(key);
        const int bucket = qBound(0, int(double(local) / host->timelineDuration * buckets), buckets - 1);
        lowest[bucket] = lowest[bucket] < 0 ? confidence : qMin(lowest[bucket], confidence);
    };
    for (const TrackPoint &point : tracker->points) {
        for (auto it = point.samples.constBegin(); it != point.samples.constEnd(); ++it)
            note(it.key(), it->confidence);
    }
    for (auto it = tracker->homographies.constBegin(); it != tracker->homographies.constEnd(); ++it)
        note(it.key(), 1.0);
    for (const double v : lowest)
        out.append(v);
    return out;
}

QString TrackingController::addPointTracker(int trackIndex, int clipIndex, double x, double y)
{
    const Clip *view = hostView(trackIndex, clipIndex);
    if (m_busy || !view || view->type != drift::ClipType::Video || view->path.isEmpty())
        return {};
    const drift::Project before = m_app->m_project;
    Clip *host = hostAt(trackIndex, clipIndex);
    const TimeUs playhead = qBound(host->timelineStart, m_app->m_playheadUs, host->timelineEnd() - 1);
    const QSizeF px = frameSizeAt(m_app->m_project, *host, playhead);

    MotionTracker tracker;
    tracker.id = QStringLiteral("trk_") + QUuid::createUuid().toString(QUuid::WithoutBraces);
    tracker.name = tr("Tracker %1").arg(host->motionTrackers.size() + 1);
    tracker.referenceUs = host->timelineToSourceUs(playhead);
    TrackPoint point;
    const double aspect = px.width() / px.height();
    point.patternSize = QSizeF(0.05, 0.05 * aspect);
    point.searchSize = QSizeF(0.15, 0.15 * aspect);
    point.samples.insert(tracker.referenceUs, TrackSample{QPointF(x, y), 1.0f, true});
    tracker.points.append(point);
    const QString id = tracker.id;
    host->motionTrackers.append(tracker);

    m_app->pushProjectEdit(before, tr("Add tracker"));
    m_app->finishEdit(tr("Tracker added"));
    emit trackersChanged();
    return id;
}

QString TrackingController::addPlanarTracker(int trackIndex, int clipIndex, const QVariantList &region)
{
    const Clip *view = hostView(trackIndex, clipIndex);
    const QVector<QPointF> outline = parseRegion(region);
    if (m_busy || !view || view->type != drift::ClipType::Video || view->path.isEmpty() || outline.size() < 4)
        return {};
    const drift::Project before = m_app->m_project;
    Clip *host = hostAt(trackIndex, clipIndex);
    const TimeUs playhead = qBound(host->timelineStart, m_app->m_playheadUs, host->timelineEnd() - 1);

    MotionTracker tracker;
    tracker.id = QStringLiteral("trk_") + QUuid::createUuid().toString(QUuid::WithoutBraces);
    tracker.name = tr("Planar %1").arg(host->motionTrackers.size() + 1);
    tracker.kind = MotionTracker::Kind::Planar;
    tracker.solve = MotionTracker::Solve::Perspective;
    tracker.referenceUs = host->timelineToSourceUs(playhead);
    tracker.planarRegion = outline;
    tracker.homographies.insert(tracker.referenceUs, drift::tracking::identityHomography());
    const QString id = tracker.id;
    host->motionTrackers.append(tracker);

    m_app->pushProjectEdit(before, tr("Add planar tracker"));
    m_app->finishEdit(tr("Planar tracker added"));
    emit trackersChanged();
    return id;
}

void TrackingController::addTrackerPoint(int trackIndex, int clipIndex, const QString &id, double x, double y)
{
    editTracker(trackIndex, clipIndex, id, tr("Add tracking point"), [&](Clip &host, MotionTracker &tracker) {
        if (tracker.kind != MotionTracker::Kind::Point || tracker.points.size() >= kMaxPoints)
            return false;
        const drift::Project &project = m_app->m_project;
        const TimeUs playhead = qBound(host.timelineStart, m_app->m_playheadUs, host.timelineEnd() - 1);
        const TimeUs sourceUs = host.timelineToSourceUs(playhead);
        const TimeUs tolerance = matchTolerance(project, host);
        const QSizeF px = frameSizeAt(project, host, playhead);

        TrackPoint point = tracker.points.isEmpty() ? TrackPoint{} : tracker.points.first();
        point.samples.clear();
        QPointF referencePos(x, y);
        if (std::abs(sourceUs - tracker.referenceUs) > tolerance) {
            // Added away from the reference frame: put its reference position where the existing
            // points say the reference frame would map it, so the solve stays consistent.
            Homography inverse;
            const auto forward = pointTransform(tracker, sourceUs, tolerance, px);
            if (forward && drift::tracking::invertHomography(*forward, &inverse)) {
                const QPointF mapped = drift::tracking::applyHomography(inverse, QPointF(x * px.width(), y * px.height()));
                referencePos = QPointF(mapped.x() / px.width(), mapped.y() / px.height());
            }
            point.samples.insert(sourceUs, TrackSample{QPointF(x, y), 1.0f, true});
        }
        point.samples.insert(tracker.referenceUs, TrackSample{referencePos, 1.0f, true});
        tracker.points.append(point);
        return true;
    });
}

namespace {

bool writePointKey(const drift::Project &project, const Clip &host, MotionTracker &tracker, TimeUs playheadUs,
                   int pointIndex, double x, double y)
{
    if (tracker.kind != MotionTracker::Kind::Point || pointIndex < 0 || pointIndex >= tracker.points.size())
        return false;
    const TimeUs playhead = qBound(host.timelineStart, playheadUs, host.timelineEnd() - 1);
    const TimeUs sourceUs = host.timelineToSourceUs(playhead);
    const TimeUs tolerance = matchTolerance(project, host);
    TrackPoint &point = tracker.points[pointIndex];
    TimeUs key = sourceUs;
    if (std::abs(sourceUs - tracker.referenceUs) <= tolerance) {
        key = tracker.referenceUs;
    } else {
        const auto existing = nearestKey(point.samples, sourceUs, tolerance);
        if (existing != point.samples.constEnd())
            key = existing.key();
    }
    point.samples.insert(key, TrackSample{QPointF(x, y), 1.0f, true});
    return true;
}

} // namespace

void TrackingController::setTrackerPoint(int trackIndex, int clipIndex, const QString &id, int pointIndex,
                                         double x, double y)
{
    editTracker(trackIndex, clipIndex, id, tr("Move tracking point"), [&](Clip &host, MotionTracker &tracker) {
        return writePointKey(m_app->m_project, host, tracker, m_app->m_playheadUs, pointIndex, x, y);
    });
}

void TrackingController::previewSetTrackerPoint(int trackIndex, int clipIndex, const QString &id, int pointIndex,
                                                double x, double y)
{
    if (m_busy || !hostView(trackIndex, clipIndex) || !trackerById(*hostView(trackIndex, clipIndex), id))
        return;
    if (!m_app->previewDragActive())
        m_app->beginPreviewDrag(tr("Move tracking point"));
    Clip *host = hostAt(trackIndex, clipIndex);
    MotionTracker *tracker = trackerById(*host, id);
    if (!writePointKey(m_app->m_project, *host, *tracker, m_app->m_playheadUs, pointIndex, x, y))
        return;
    m_app->emitPreviewEdit(trackIndex, clipIndex, {QStringLiteral("tracker")});
    emit samplesUpdated(id);
}

void TrackingController::setTrackerRegion(int trackIndex, int clipIndex, const QString &id, const QVariantList &region)
{
    const QVector<QPointF> outline = parseRegion(region);
    if (outline.size() < 4)
        return;
    editTracker(trackIndex, clipIndex, id, tr("Change tracking outline"), [&](Clip &host, MotionTracker &tracker) {
        if (tracker.kind != MotionTracker::Kind::Planar)
            return false;
        const TimeUs playhead = qBound(host.timelineStart, m_app->m_playheadUs, host.timelineEnd() - 1);
        tracker.referenceUs = host.timelineToSourceUs(playhead);
        tracker.planarRegion = outline;
        tracker.homographies.clear();
        tracker.homographies.insert(tracker.referenceUs, drift::tracking::identityHomography());
        return true;
    });
}

void TrackingController::correctPlanarCorners(int trackIndex, int clipIndex, const QString &id,
                                              const QVariantList &region)
{
    const Clip *view = hostView(trackIndex, clipIndex);
    const MotionTracker *tracker = view ? trackerById(*view, id) : nullptr;
    if (m_busy || !tracker || tracker->kind != MotionTracker::Kind::Planar)
        return;
    const TimeUs tolerance = matchTolerance(m_app->m_project, *view);
    const TimeUs playhead = qBound(view->timelineStart, m_app->m_playheadUs, view->timelineEnd() - 1);
    const TimeUs sourceUs = view->timelineToSourceUs(playhead);
    if (tracker->homographies.isEmpty() || std::abs(sourceUs - tracker->referenceUs) <= tolerance) {
        setTrackerRegion(trackIndex, clipIndex, id, region);
        return;
    }

    // Stored homographies act on normalized coordinates (see trackerFrame), so the fit does too.
    const QVector<QPointF> outline = parseRegion(region);
    Homography h;
    if (outline.size() != tracker->planarRegion.size()
        || !drift::tracking::fitHomography(tracker->planarRegion, outline, &h)) {
        return;
    }
    editTracker(trackIndex, clipIndex, id, tr("Correct planar track"), [&](Clip &, MotionTracker &planar) {
        if (planar.kind != MotionTracker::Kind::Planar)
            return false;
        const auto near = nearestKey(planar.homographies, sourceUs, tolerance);
        planar.homographies.insert(near != planar.homographies.constEnd() ? near.key() : sourceUs, h);
        return true;
    });
}

void TrackingController::setTrackerProperty(int trackIndex, int clipIndex, const QString &id, const QString &key,
                                            const QVariant &value)
{
    editTracker(trackIndex, clipIndex, id, tr("Change tracker"), [&](Clip &host, MotionTracker &tracker) {
        if (key == QLatin1String("name")) {
            tracker.name = value.toString();
        } else if (key == QLatin1String("solve")) {
            tracker.solve = drift::motionTrackerSolveFromString(value.toString());
        } else if (key == QLatin1String("minConfidence")) {
            tracker.minConfidence = qBound(0.0, value.toDouble(), 1.0);
        } else if (key == QLatin1String("patternSize") || key == QLatin1String("searchSize")) {
            const double width = qBound(0.01, value.toDouble(), 0.9);
            const QSizeF px = frameSizeAt(m_app->m_project, host, host.timelineStart);
            const QSizeF size(width, qMin(0.9, width * px.width() / px.height()));
            for (TrackPoint &point : tracker.points)
                (key == QLatin1String("patternSize") ? point.patternSize : point.searchSize) = size;
        } else {
            return false;
        }
        return true;
    });
}

void TrackingController::removeTracker(int trackIndex, int clipIndex, const QString &id, bool removeKeys)
{
    editTracker(trackIndex, clipIndex, id, tr("Remove tracker"), [&](Clip &host, MotionTracker &tracker) {
        if (removeKeys) {
            for (const drift::TrackerTarget &target : tracker.targets)
                unapplyTarget(host, tracker, target);
        }
        for (int i = 0; i < host.motionTrackers.size(); ++i) {
            if (host.motionTrackers.at(i).id == id) {
                host.motionTrackers.removeAt(i);
                return true;
            }
        }
        return false;
    });
}

void TrackingController::clearSamples(int trackIndex, int clipIndex, const QString &id, bool forward)
{
    editTracker(trackIndex, clipIndex, id, tr("Clear track"), [&](Clip &host, MotionTracker &tracker) {
        eraseSamples(m_app->m_project, host, tracker, m_app->m_playheadUs, forward);
        return true;
    });
}

void TrackingController::track(int trackIndex, int clipIndex, const QString &id, bool forward, bool singleFrame)
{
    if (m_busy) {
        m_app->setLastMessage(tr("Tracking is already running"), QStringLiteral("warning"));
        return;
    }
    const Clip *host = hostView(trackIndex, clipIndex);
    const MotionTracker *tracker = host ? trackerById(*host, id) : nullptr;
    if (!tracker)
        return;
    if (host->path.isEmpty() || host->srcOut <= host->srcIn) {
        m_app->setLastMessage(tr("This clip has no video to track"), QStringLiteral("warning"));
        return;
    }

    const drift::Project &project = std::as_const(m_app->m_project);
    m_app->setPlaying(false);
    const TimeUs frame = drift::frameDurationUs(project.fps());
    const TimeUs tolerance = matchTolerance(project, *host);
    const TimeUs playhead = qBound(host->timelineStart, m_app->m_playheadUs, host->timelineEnd() - 1);
    const TimeUs startSource = host->timelineToSourceUs(playhead);

    Job job;
    job.clipId = host->id;
    job.trackerId = tracker->id;
    job.path = host->path;
    job.rotationCorrection = host->rotationCorrection;
    job.planar = tracker->kind == MotionTracker::Kind::Planar;
    job.minConfidence = tracker->minConfidence;
    job.sourceTimes.append(startSource);
    job.timelineTimes.append(playhead);
    for (int k = 1;; ++k) {
        const TimeUs t = forward ? playhead + k * frame : playhead - k * frame;
        if (t < host->timelineStart || t >= host->timelineEnd())
            break;
        const TimeUs s = host->timelineToSourceUs(t);
        if (s != job.sourceTimes.last()) {
            job.sourceTimes.append(s);
            job.timelineTimes.append(t);
        }
        if (singleFrame)
            break;
    }
    if (job.sourceTimes.size() < 2) {
        m_app->setLastMessage(tr("Nothing left to track in that direction"), QStringLiteral("warning"));
        return;
    }

    if (job.planar) {
        const auto it = nearestKey(tracker->homographies, startSource, tolerance);
        if (it == tracker->homographies.constEnd()) {
            m_app->setLastMessage(tr("Move the playhead to a tracked frame first"), QStringLiteral("warning"));
            return;
        }
        job.start = it.value();
        job.region = QPolygonF(tracker->planarRegion);
    } else {
        // A point added after the last run has no sample at the start yet; it sits this run out
        // rather than blocking the others.
        bool anyPoint = false;
        for (const TrackPoint &point : tracker->points) {
            const auto it = nearestKey(point.samples, startSource, tolerance);
            Job::Point entry;
            entry.skip = it == point.samples.constEnd();
            if (!entry.skip) {
                entry.pos = it->pos;
                anyPoint = true;
            }
            entry.patternWidth = point.patternSize.width();
            for (auto key = point.samples.constBegin(); key != point.samples.constEnd(); ++key) {
                if (key->userKey)
                    entry.userKeys.insert(key.key(), key->pos);
            }
            job.points.append(entry);
        }
        if (!anyPoint) {
            m_app->setLastMessage(tr("Move the playhead to a tracked frame first"), QStringLiteral("warning"));
            return;
        }
    }

    Clip *editable = hostAt(trackIndex, clipIndex);
    MotionTracker &liveTracker = *trackerById(*editable, id);
    m_jobClipId = editable->id;
    m_jobTrackerId = id;
    m_jobTrackerBefore = liveTracker;
    m_jobChanged = false;
    // Anything beyond the start is about to be redone; leaving it would keep stale samples past
    // wherever this run stops. A single frame overwrites only its own sample, so it keeps the rest.
    if (!singleFrame)
        m_jobChanged = eraseSamples(project, *editable, liveTracker, playhead, forward);

    m_cancel.storeRelaxed(0);
    m_progress = 0.0;
    m_busyTrackerId = id;
    m_busy = true;
    emit progressChanged();
    emit busyChanged();

    (void)QtConcurrent::run([this, job = std::move(job)]() {
        Sink sink = [this, clipId = job.clipId, trackerId = job.trackerId](QList<FrameResult> &&frames, TimeUs last,
                                                                           double progress) {
            QMetaObject::invokeMethod(
                this,
                [this, clipId, trackerId, frames = std::move(frames), last, progress]() {
                    m_progress = progress;
                    emit progressChanged();
                    mergeBatch(clipId, trackerId, frames, last);
                },
                Qt::QueuedConnection);
        };
        BatchWriter writer(sink);
        FrameFeed feed(job, m_cancel);
        const Outcome outcome = job.planar ? trackPlanar(job, feed, m_cancel, writer)
                                           : trackPoints(job, feed, m_cancel, writer);
        QMetaObject::invokeMethod(
            this, [this, outcome]() { finishJob(outcome.message, outcome.severity); }, Qt::QueuedConnection);
    });
}

void TrackingController::mergeBatch(const QString &clipId, const QString &trackerId, const QList<FrameResult> &frames,
                                    TimeUs lastTimelineUs)
{
    int t = -1;
    int c = -1;
    if (!findClipById(m_app->m_project, clipId, &t, &c))
        return;
    Clip &host = m_app->m_project.tracks()[t].clips[c];
    MotionTracker *tracker = trackerById(host, trackerId);
    if (!tracker)
        return;
    const TimeUs tolerance = matchTolerance(m_app->m_project, host);

    for (const FrameResult &frame : frames) {
        if (frame.hasHomography) {
            tracker->homographies.insert(frame.sourceUs, frame.homography);
            m_jobChanged = true;
            continue;
        }
        for (int p = 0; p < frame.samples.size() && p < tracker->points.size(); ++p) {
            const TrackSample &sample = frame.samples.at(p);
            if (sample.confidence < 0.0f)
                continue;
            QMap<TimeUs, TrackSample> &samples = tracker->points[p].samples;
            const auto existing = nearestKey(samples, frame.sourceUs, tolerance);
            if (existing != samples.constEnd() && existing->userKey)
                continue;
            samples.insert(frame.sourceUs, sample);
            m_jobChanged = true;
        }
    }
    // Following the worker would fight a scrub; the playhead is the user's while they drag it.
    if (!frames.isEmpty() && !m_app->scrubbing())
        m_app->setPlayheadUs(lastTimelineUs);
    emit samplesUpdated(trackerId);
}

void TrackingController::finishJob(const QString &message, const QString &severity)
{
    m_busy = false;
    m_busyTrackerId.clear();
    if (m_cancel.loadRelaxed() == 0 && severity != QLatin1String("error"))
        m_progress = 1.0;
    emit progressChanged();
    emit busyChanged();
    QString shownMessage = message;
    QString shownSeverity = severity;
    int t = -1;
    int c = -1;
    const MotionTracker *live = nullptr;
    if (findClipById(m_app->m_project, m_jobClipId, &t, &c))
        live = trackerById(m_app->m_project.tracks().at(t).clips.at(c), m_jobTrackerId);
    const bool applyWanted = live && severity != QLatin1String("error") && !live->targets.isEmpty();
    if (live && (m_jobChanged || applyWanted)) {
        // Copied before the apply, so undo also reverts the keys it writes. Only this tracker is
        // swapped back, so edits made during the run survive an undo.
        drift::Project before = m_app->m_project;
        for (MotionTracker &tracker : before.tracks()[t].clips[c].motionTrackers) {
            if (tracker.id == m_jobTrackerId)
                tracker = m_jobTrackerBefore;
        }
        if (applyWanted) {
            const QString error = applyTargets(m_jobClipId, m_jobTrackerId);
            if (!error.isEmpty() && shownMessage.isEmpty()) {
                shownMessage = error;
                shownSeverity = QStringLiteral("warning");
            }
        }
        m_app->pushProjectEdit(before, tr("Track"));
        m_app->finishEdit(tr("Tracked"));
    }
    m_jobClipId.clear();
    m_jobTrackerId.clear();
    m_jobTrackerBefore = MotionTracker();
    m_jobChanged = false;
    emit trackersChanged();
    if (!shownMessage.isEmpty())
        m_app->setLastMessage(shownMessage, shownSeverity);
}

void TrackingController::cancel()
{
    if (m_busy)
        m_cancel.storeRelaxed(1);
}

QVariantList TrackingController::targetCandidates(int trackIndex, int clipIndex) const
{
    QVariantList out;
    const Clip *host = hostView(trackIndex, clipIndex);
    if (!host)
        return out;
    const drift::Project &project = m_app->m_project;
    for (const drift::ClipRef &ref : drift::linkedMaskAdjustments(project, trackIndex, clipIndex)) {
        const Clip &mask = project.tracks().at(ref.trackIndex).clips.at(ref.clipIndex);
        out.append(QVariantMap{
            {QStringLiteral("clipId"), mask.id},
            {QStringLiteral("name"), mask.mask.name.isEmpty() ? drift::maskShapeToString(mask.mask.shape) : mask.mask.name},
            {QStringLiteral("kind"), QStringLiteral("mask")},
        });
    }
    for (int t = 0; t < project.tracks().size(); ++t) {
        if (t == trackIndex)
            continue;
        for (const Clip &clip : project.tracks().at(t).clips) {
            if (clip.type == drift::ClipType::Audio || clip.type == drift::ClipType::Subtitle
                || clip.type == drift::ClipType::Adjustment)
                continue;
            if (clip.timelineStart >= host->timelineEnd() || clip.timelineEnd() <= host->timelineStart)
                continue;
            out.append(QVariantMap{{QStringLiteral("clipId"), clip.id},
                                   {QStringLiteral("name"), clip.name},
                                   {QStringLiteral("kind"), QStringLiteral("clip")}});
        }
    }
    return out;
}

void TrackingController::setTarget(int trackIndex, int clipIndex, const QString &id, const QString &targetClipId,
                                   const QString &mode)
{
    QString error;
    const bool ok = editTracker(trackIndex, clipIndex, id, tr("Apply track"), [&](Clip &host, MotionTracker &tracker) {
        if (targetClipId.isEmpty() || targetClipId == host.id)
            return false;
        const drift::TrackerTarget::Mode targetMode = drift::trackerTargetModeFromString(mode);
        bool found = false;
        for (drift::TrackerTarget &target : tracker.targets) {
            if (target.clipId == targetClipId) {
                target.mode = targetMode;
                found = true;
            }
        }
        if (!found) {
            drift::TrackerTarget target{targetClipId, targetMode};
            int t = -1;
            int c = -1;
            if (targetMode == drift::TrackerTarget::Mode::FollowClip
                && findClipById(m_app->m_project, targetClipId, &t, &c)) {
                // A new follower lands on the tracked point; Keep offset is the opt-in.
                target.centerOnTrack = true;
                const Clip &owner = m_app->m_project.tracks().at(t).clips.at(c);
                const TimeUs rel = qMax<TimeUs>(0, referenceTimelineUs(host, tracker) - owner.timelineStart);
                const auto own = [&](const drift::KeyframeTrack<double> &track, double fallback) {
                    return track.isEmpty() ? fallback : track.evaluateAt(rel);
                };
                target.hasRest = true;
                target.restRect = QRectF(own(owner.transformX, 0.0), own(owner.transformY, 0.0),
                                         own(owner.transformW, m_app->m_project.width()),
                                         own(owner.transformH, m_app->m_project.height()));
                target.restRotation = own(owner.rotation, 0.0);
            }
            tracker.targets.append(target);
        }
        error = applyTargets(host.id, tracker.id, targetClipId);
        return true;
    });
    if (ok && !error.isEmpty())
        m_app->setLastMessage(error, QStringLiteral("warning"));
}

void TrackingController::removeTarget(int trackIndex, int clipIndex, const QString &id, const QString &targetClipId)
{
    editTracker(trackIndex, clipIndex, id, tr("Remove tracking target"), [&](Clip &host, MotionTracker &tracker) {
        for (int i = 0; i < tracker.targets.size(); ++i) {
            if (tracker.targets.at(i).clipId == targetClipId) {
                unapplyTarget(host, tracker, tracker.targets.at(i));
                tracker.targets.removeAt(i);
                return true;
            }
        }
        return false;
    });
}

// Removes the animation applyTargets wrote, so the target is left at its own pose rather than
// carrying sparse baked keys that a later drag would only partly edit.
void TrackingController::unapplyTarget(const Clip &host, const MotionTracker &tracker, const drift::TrackerTarget &target)
{
    drift::Project &project = m_app->m_project;
    int t = -1;
    int c = -1;
    if (!findClipById(project, target.clipId, &t, &c))
        return;
    Clip &owner = project.tracks()[t].clips[c];

    const TimeUs refTimeline = referenceTimelineUs(host, tracker);
    const QSizeF px = frameSizeAt(project, host, refTimeline);
    const QVector<FrameXform> frames = frameTransforms(project, host, tracker, px);
    const QVector<QVector<int>> runs = keyableRuns(frames, owner);
    if (runs.isEmpty())
        return;
    const TimeUs lo = frames[runs.first().first()].timelineUs - owner.timelineStart;
    const TimeUs hi = frames[runs.last().last()].timelineUs - owner.timelineStart;
    const TimeUs restT = qMax<TimeUs>(0, refTimeline - owner.timelineStart);

    const bool planar = tracker.kind == MotionTracker::Kind::Planar;
    const bool writesRotation = planar || tracker.solve != MotionTracker::Solve::Translate;
    const bool writesScale = planar || tracker.solve == MotionTracker::Solve::TranslateRotateScale
                             || tracker.solve == MotionTracker::Solve::Perspective;

    if (target.mode == drift::TrackerTarget::Mode::Mask) {
        if (owner.type != drift::ClipType::Adjustment || owner.adjustmentKind != drift::AdjustmentKind::Mask)
            return;
        drift::Mask &mask = owner.mask;
        const std::pair<QString, double drift::Mask::*> channels[] = {
            {QStringLiteral("x"), &drift::Mask::x}, {QStringLiteral("y"), &drift::Mask::y},
            {QStringLiteral("rotation"), &drift::Mask::rotation}, {QStringLiteral("w"), &drift::Mask::w},
            {QStringLiteral("h"), &drift::Mask::h}};
        for (const auto &[key, field] : channels) {
            if ((key == QLatin1String("rotation") && !writesRotation)
                || ((key == QLatin1String("w") || key == QLatin1String("h")) && !writesScale))
                continue;
            if (!mask.keyframes.contains(key))
                continue;
            double rest = mask.*field;
            if (!stripChannel(mask.keyframes[key], lo, hi, restT, &rest)) {
                mask.keyframes.remove(key);
                mask.*field = rest;
            }
        }
        for (auto it = mask.pathKeys.begin(); it != mask.pathKeys.end();)
            it = (it.key() >= lo && it.key() <= hi) ? mask.pathKeys.erase(it) : std::next(it);
        return;
    }

    const auto strip = [&](drift::KeyframeTrack<double> &track, double stored) {
        double rest = 0;
        if (!stripChannel(track, lo, hi, restT, &rest))
            track.setKeyframe(0, target.hasRest ? stored : rest);
    };
    strip(owner.transformX, target.restRect.x());
    strip(owner.transformY, target.restRect.y());
    if (writesRotation)
        strip(owner.rotation, target.restRotation);
    if (writesScale) {
        strip(owner.transformW, target.restRect.width());
        strip(owner.transformH, target.restRect.height());
    }
}

void TrackingController::setTargetCenter(int trackIndex, int clipIndex, const QString &id,
                                         const QString &targetClipId, bool center)
{
    QString error;
    const bool ok = editTracker(trackIndex, clipIndex, id, tr("Change follow anchor"), [&](Clip &host, MotionTracker &tracker) {
        for (drift::TrackerTarget &target : tracker.targets) {
            if (target.clipId != targetClipId || target.mode != drift::TrackerTarget::Mode::FollowClip
                || target.centerOnTrack == center)
                continue;
            target.centerOnTrack = center;
            error = applyTargets(host.id, tracker.id, targetClipId);
            return true;
        }
        return false;
    });
    if (ok && !error.isEmpty())
        m_app->setLastMessage(error, QStringLiteral("warning"));
}

void TrackingController::apply(int trackIndex, int clipIndex, const QString &id)
{
    QString error;
    const bool ok = editTracker(trackIndex, clipIndex, id, tr("Apply track"), [&](Clip &host, MotionTracker &tracker) {
        error = applyTargets(host.id, tracker.id);
        return !tracker.targets.isEmpty();
    });
    if (ok && !error.isEmpty())
        m_app->setLastMessage(error, QStringLiteral("warning"));
}

QString TrackingController::applyTargets(const QString &clipId, const QString &trackerId, const QString &onlyTarget)
{
    drift::Project &project = m_app->m_project;
    int hostTrack = -1;
    int hostIndex = -1;
    if (!findClipById(project, clipId, &hostTrack, &hostIndex))
        return {};
    const Clip &host = project.tracks().at(hostTrack).clips.at(hostIndex);
    const MotionTracker *tracker = trackerById(host, trackerId);
    if (!tracker)
        return {};

    const TimeUs refTimeline = referenceTimelineUs(host, *tracker);
    const QSizeF px = frameSizeAt(project, host, refTimeline);
    const QVector<FrameXform> frames = frameTransforms(project, host, *tracker, px);
    const bool anyFrame = std::any_of(frames.begin(), frames.end(), [](const FrameXform &f) { return f.has; });
    if (!anyFrame)
        return tr("Nothing is tracked yet");

    const bool planar = tracker->kind == MotionTracker::Kind::Planar;
    const MotionTracker::Solve solve = tracker->solve;
    const bool writesRotation = planar || solve != MotionTracker::Solve::Translate;
    const bool writesScale = planar || solve == MotionTracker::Solve::TranslateRotateScale
                             || solve == MotionTracker::Solve::Perspective;
    QString firstError;

    for (const drift::TrackerTarget &target : tracker->targets) {
        if (!onlyTarget.isEmpty() && target.clipId != onlyTarget)
            continue;
        int t = -1;
        int c = -1;
        if (!findClipById(project, target.clipId, &t, &c))
            continue;
        Clip &owner = project.tracks()[t].clips[c];
        const QVector<QVector<int>> runs = keyableRuns(frames, owner);
        if (runs.isEmpty())
            continue;

        if (target.mode == drift::TrackerTarget::Mode::Mask) {
            if (owner.type != drift::ClipType::Adjustment || owner.adjustmentKind != drift::AdjustmentKind::Mask) {
                if (firstError.isEmpty())
                    firstError = tr("%1 is not a mask").arg(owner.name);
                continue;
            }
            drift::Mask &mask = owner.mask;
            const drift::Mask rest = mask.resolvedAt(refTimeline - owner.timelineStart);
            const bool parametric = !planar && solve != MotionTracker::Solve::Perspective
                                    && rest.shape != drift::MaskShape::Freeform
                                    && rest.shape != drift::MaskShape::Bars && rest.shape != drift::MaskShape::None;
            if (parametric) {
                QVector<PoseRun> poseRuns;
                const QPointF centre(rest.x * px.width(), rest.y * px.height());
                for (const QVector<int> &run : runs) {
                    PoseRun poses;
                    for (const int i : run) {
                        const Homography &h = frames[i].h;
                        const QPointF moved = drift::tracking::applyHomography(h, centre);
                        Pose pose;
                        pose.t = frames[i].timelineUs - owner.timelineStart;
                        pose.x = moved.x() / px.width();
                        pose.y = moved.y() / px.height();
                        pose.rotation = rest.rotation + (writesRotation ? qRadiansToDegrees(std::atan2(h[3], h[0])) : 0.0);
                        const double scale = writesScale ? std::hypot(h[0], h[3]) : 1.0;
                        pose.w = rest.w * scale;
                        pose.h = rest.h * scale;
                        poses.append(pose);
                    }
                    unwrapRotation(&poses);
                    poseRuns.append(poses);
                }
                writeChannel(mask.keyframes[QStringLiteral("x")], poseRuns, &Pose::x, kScalarTolerancePx / px.width());
                writeChannel(mask.keyframes[QStringLiteral("y")], poseRuns, &Pose::y, kScalarTolerancePx / px.height());
                if (writesRotation)
                    writeChannel(mask.keyframes[QStringLiteral("rotation")], poseRuns, &Pose::rotation,
                                 kRotationToleranceDeg);
                if (writesScale) {
                    writeChannel(mask.keyframes[QStringLiteral("w")], poseRuns, &Pose::w, kScalarTolerancePx / px.width());
                    writeChannel(mask.keyframes[QStringLiteral("h")], poseRuns, &Pose::h, kScalarTolerancePx / px.height());
                }
                continue;
            }

            QString error;
            const QVector<QPointF> vertices = restVertices(rest, px, &error);
            if (vertices.isEmpty()) {
                if (firstError.isEmpty())
                    firstError = error;
                continue;
            }
            if (mask.shape != drift::MaskShape::Freeform) {
                mask.shape = drift::MaskShape::Freeform;
                mask.points.clear();
                for (const QPointF &v : vertices)
                    mask.points.append(QPointF(v.x() / px.width(), v.y() / px.height()));
                for (const QString &key : {QStringLiteral("x"), QStringLiteral("y"), QStringLiteral("w"),
                                           QStringLiteral("h"), QStringLiteral("rotation")}) {
                    mask.keyframes.remove(key);
                }
            }
            QVector<QVector<PathPose>> pathRuns;
            for (const QVector<int> &run : runs) {
                QVector<PathPose> poses;
                for (const int i : run) {
                    PathPose pose;
                    pose.t = frames[i].timelineUs - owner.timelineStart;
                    for (const QPointF &v : vertices) {
                        const QPointF moved = drift::tracking::applyHomography(frames[i].h, v);
                        pose.points.append(QPointF(moved.x() / px.width(), moved.y() / px.height()));
                    }
                    poses.append(pose);
                }
                pathRuns.append(poses);
            }
            const TimeUs lo = pathRuns.first().first().t;
            const TimeUs hi = pathRuns.last().last().t;
            for (auto it = mask.pathKeys.begin(); it != mask.pathKeys.end();)
                it = (it.key() >= lo && it.key() <= hi) ? mask.pathKeys.erase(it) : std::next(it);
            for (const QVector<PathPose> &poses : pathRuns) {
                const QVector<int> kept = simplified(poses.size(), kScalarTolerancePx, [&](int k, int a, int b) {
                    const double u = double(poses[k].t - poses[a].t) / double(poses[b].t - poses[a].t);
                    double worst = 0;
                    for (int v = 0; v < vertices.size(); ++v) {
                        const QPointF expected = poses[a].points[v] + (poses[b].points[v] - poses[a].points[v]) * u;
                        worst = qMax(worst, std::hypot((poses[k].points[v].x() - expected.x()) * px.width(),
                                                       (poses[k].points[v].y() - expected.y()) * px.height()));
                    }
                    return worst;
                });
                for (const int k : kept)
                    mask.pathKeys.insert(poses[k].t, poses[k].points);
            }
            continue;
        }

        // Follow clip: pose(t) = M(t) o pose(reference), with M the tracker transform carried from
        // the tracked clip's frame into canvas space at each time. Perspective and planar solves
        // contribute their similarity part only.
        const HostFrame hostRef = hostFrameAt(project, host, refTimeline);
        const TimeUs followerRel = qMax<TimeUs>(0, refTimeline - owner.timelineStart);
        const auto ownValue = [&](const drift::KeyframeTrack<double> &track, double fallback) {
            return track.isEmpty() ? fallback : track.evaluateAt(followerRel);
        };
        const double x0 = target.hasRest ? target.restRect.x() : ownValue(owner.transformX, 0.0);
        const double y0 = target.hasRest ? target.restRect.y() : ownValue(owner.transformY, 0.0);
        const double w0 = qMax(1.0, target.hasRest ? target.restRect.width() : ownValue(owner.transformW, project.width()));
        const double h0 = qMax(1.0, target.hasRest ? target.restRect.height() : ownValue(owner.transformH, project.height()));
        const double rotation0 = target.hasRest ? target.restRotation : ownValue(owner.rotation, 0.0);
        QPointF centre0(x0 + w0 / 2.0, y0 + h0 / 2.0);
        if (target.centerOnTrack) {
            if (const std::optional<QPointF> anchor = trackAnchor(*tracker, px)) {
                const QPointF local((anchor->x() / px.width() - 0.5) * hostRef.w,
                                    (anchor->y() / px.height() - 0.5) * hostRef.h);
                centre0 = hostRef.centre + rotated(local, hostRef.rotation);
            }
        }

        QVector<PoseRun> poseRuns;
        for (const QVector<int> &run : runs) {
            PoseRun poses;
            for (const int i : run) {
                const HostFrame now = hostFrameAt(project, host, frames[i].timelineUs);
                const auto carry = [&](QPointF canvas) {
                    const QPointF q = rotated(canvas - hostRef.centre, -hostRef.rotation);
                    const QPointF local((q.x() / hostRef.w + 0.5) * px.width(), (q.y() / hostRef.h + 0.5) * px.height());
                    const QPointF moved = drift::tracking::applyHomography(frames[i].h, local);
                    const QPointF q2((moved.x() / px.width() - 0.5) * now.w, (moved.y() / px.height() - 0.5) * now.h);
                    return now.centre + rotated(q2, now.rotation);
                };
                const QPointF c = carry(centre0);
                const QPointF e = carry(centre0 + QPointF(1.0, 0.0)) - c;
                Pose pose;
                pose.t = frames[i].timelineUs - owner.timelineStart;
                pose.rotation = rotation0 + (writesRotation ? qRadiansToDegrees(std::atan2(e.y(), e.x())) : 0.0);
                const double scale = writesScale ? std::hypot(e.x(), e.y()) : 1.0;
                pose.w = w0 * scale;
                pose.h = h0 * scale;
                pose.x = c.x() - pose.w / 2.0;
                pose.y = c.y() - pose.h / 2.0;
                poses.append(pose);
            }
            unwrapRotation(&poses);
            poseRuns.append(poses);
        }
        writeChannel(owner.transformX, poseRuns, &Pose::x, kScalarTolerancePx);
        writeChannel(owner.transformY, poseRuns, &Pose::y, kScalarTolerancePx);
        if (writesRotation)
            writeChannel(owner.rotation, poseRuns, &Pose::rotation, kRotationToleranceDeg);
        if (writesScale) {
            writeChannel(owner.transformW, poseRuns, &Pose::w, kScalarTolerancePx);
            writeChannel(owner.transformH, poseRuns, &Pose::h, kScalarTolerancePx);
        }
    }
    return firstError;
}
