#include "app/jobs/vision/TrackingController.h"

#include "app/editor/AppController.h"
#include "app/editor/ProjectFileController.h"
#include "app/jobs/vision/TrackerListModel.h"
#include "app/mcp/McpDetail.h"
#include "core/edit/MaskSpace.h"
#include "core/edit/TimelineOps.h"
#include "core/tracking/MotionTracker.h"
#include "engine/compositor/layers/MaskApplier.h"
#include "engine/decode/readers/ClipReaderPool.h"
#include "engine/render3d/editor/ClipGizmo.h"
#include "engine/render3d/scene/SceneCamera3d.h"
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
using drift::HostFrame;
using drift::hostFrameAt;
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
    double fps = 30.0;
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
constexpr double kMaxStabilizeZoom = 4.0;

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
    // A camera's keys: x and y are its position, rotation its roll.
    double z = 0;
    double pitch = 0;
    double yaw = 0;
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

QPointF rotated(QPointF p, double degrees)
{
    const double a = qDegreesToRadians(degrees);
    return QPointF(p.x() * std::cos(a) - p.y() * std::sin(a), p.x() * std::sin(a) + p.y() * std::cos(a));
}

// A canvas-space point carried by the tracker: into the host's frame at the reference time, through
// the tracker's transform, and back out to the canvas through the host's placement at `now`. The
// host's own motion and rotation between the two are part of the result.
QPointF carryThroughHost(const HostFrame &ref, const HostFrame &now, const Homography &h, QSizeF px,
                         QPointF canvas)
{
    const QPointF q = rotated(canvas - ref.centre, -ref.rotation);
    const QPointF local((q.x() / ref.w + 0.5) * px.width(), (q.y() / ref.h + 0.5) * px.height());
    const QPointF moved = drift::tracking::applyHomography(h, local);
    const QPointF q2((moved.x() / px.width() - 0.5) * now.w, (moved.y() / px.height() - 0.5) * now.h);
    return now.centre + rotated(q2, now.rotation);
}

// Clip-mode stabilize: the clip's rest pose on the canvas, the tracked point carried to the canvas
// through it, and where that point should sit.
struct StabilizeSetup
{
    QSizeF px; // the clip frame at rest, which is what the tracker's pixel space is built from
    QPointF centre0;
    double w0 = 1;
    double h0 = 1;
    double rotation0 = 0;
    QPointF anchorOnScreen;
    QPointF lockPoint;

    QPointF toCanvas(QPointF local) const
    {
        return centre0 + rotated(QPointF((local.x() / px.width() - 0.5) * w0, (local.y() / px.height() - 0.5) * h0),
                                 rotation0);
    }
};

StabilizeSetup stabilizeSetup(const drift::Project &project, const Clip &host, const MotionTracker &tracker,
                              const drift::TrackerTarget &target, TimeUs refTimeline)
{
    const TimeUs rel = qMax<TimeUs>(0, refTimeline - host.timelineStart);
    const auto own = [&](const drift::KeyframeTrack<double> &track, double fallback) {
        return track.isEmpty() ? fallback : track.evaluateAt(rel);
    };
    StabilizeSetup s;
    s.w0 = qMax(1.0, target.hasRest ? target.restRect.width() : own(host.transformW, project.width()));
    s.h0 = qMax(1.0, target.hasRest ? target.restRect.height() : own(host.transformH, project.height()));
    s.px = QSizeF(s.w0, s.h0);
    s.centre0 = QPointF((target.hasRest ? target.restRect.x() : own(host.transformX, 0.0)) + s.w0 / 2.0,
                        (target.hasRest ? target.restRect.y() : own(host.transformY, 0.0)) + s.h0 / 2.0);
    s.rotation0 = target.hasRest ? target.restRotation : own(host.rotation, 0.0);
    const std::optional<QPointF> anchor = trackAnchor(tracker, s.px);
    s.anchorOnScreen = s.toCanvas(anchor.value_or(QPointF(s.px.width() / 2.0, s.px.height() / 2.0)));
    s.lockPoint = target.centerOnTrack ? QPointF(project.width() / 2.0, project.height() / 2.0) : s.anchorOnScreen;
    return s;
}

// pose(t) = Z o rest o H_t^-1: the clip placed so what the tracker followed lands back where it was.
// The scale is always the real one (zoom needs it even for solves that carry none).
Pose stabilizedPose(const StabilizeSetup &s, const Homography &h, double zoom, bool writesRotation)
{
    Homography inverse;
    if (!drift::tracking::invertHomography(h, &inverse))
        inverse = drift::tracking::identityHomography();
    const auto place = [&](QPointF local) {
        const QPointF canvas = s.toCanvas(drift::tracking::applyHomography(inverse, local));
        return s.lockPoint + (canvas - s.anchorOnScreen) * zoom;
    };
    const QPointF middle(s.px.width() / 2.0, s.px.height() / 2.0);
    const QPointF c = place(middle);
    const QPointF ex = place(middle + QPointF(1.0, 0.0)) - c;
    const QPointF ey = place(middle + QPointF(0.0, 1.0)) - c;
    Pose pose;
    pose.rotation = writesRotation ? qRadiansToDegrees(std::atan2(ex.y(), ex.x())) : s.rotation0;
    pose.w = std::hypot(ex.x(), ex.y()) * s.px.width();
    pose.h = std::hypot(ey.x(), ey.y()) * s.px.height();
    pose.x = c.x() - pose.w / 2.0;
    pose.y = c.y() - pose.h / 2.0;
    return pose;
}

bool coversCanvas(const Pose &pose, QSizeF canvas)
{
    const QPointF centre(pose.x + pose.w / 2.0, pose.y + pose.h / 2.0);
    for (const QPointF &corner : {QPointF(0, 0), QPointF(canvas.width(), 0), QPointF(canvas.width(), canvas.height()),
                                  QPointF(0, canvas.height())}) {
        const QPointF q = rotated(corner - centre, -pose.rotation);
        if (std::abs(q.x()) > pose.w / 2.0 + 1e-6 || std::abs(q.y()) > pose.h / 2.0 + 1e-6)
            return false;
    }
    return true;
}

// ---- camera-mode stabilize --------------------------------------------------------------------
//
// The camera views the world plane z = 0 (canvas px about the canvas centre, y down) through
//   screen = c + d * (Xc.x, Xc.y) / (d - Xc.z),  Xc = R^T X - P,  R = Rx(rotX) Ry(rotY) Rz(rotZ)
// which is what cameraViewProjection builds. The solvers below work in doubles on that formula and
// the tests check them against the real matrix.

constexpr double kCameraRmsWarnPx = 2.0;

using CameraParams = std::array<double, 6>; // posX, posY, posZ (px), rotX, rotY, rotZ (degrees)
using Mat3 = std::array<double, 9>;         // row-major

Mat3 mulMat3(const Mat3 &a, const Mat3 &b)
{
    Mat3 out{};
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            for (int k = 0; k < 3; ++k)
                out[i * 3 + j] += a[i * 3 + k] * b[k * 3 + j];
        }
    }
    return out;
}

Mat3 cameraRotation(const CameraParams &c)
{
    const double ax = qDegreesToRadians(c[3]);
    const double ay = qDegreesToRadians(c[4]);
    const double az = qDegreesToRadians(c[5]);
    const Mat3 rx{1, 0, 0, 0, std::cos(ax), -std::sin(ax), 0, std::sin(ax), std::cos(ax)};
    const Mat3 ry{std::cos(ay), 0, std::sin(ay), 0, 1, 0, -std::sin(ay), 0, std::cos(ay)};
    const Mat3 rz{std::cos(az), -std::sin(az), 0, std::sin(az), std::cos(az), 0, 0, 0, 1};
    return mulMat3(mulMat3(rx, ry), rz);
}

// `world` is relative to the canvas centre. False when the point is at or behind the eye.
bool projectThroughCamera(const CameraParams &c, double d, QSizeF canvas, QPointF world, QPointF *out)
{
    const Mat3 r = cameraRotation(c);
    const double x = r[0] * world.x() + r[3] * world.y() - c[0];
    const double y = r[1] * world.x() + r[4] * world.y() - c[1];
    const double z = r[2] * world.x() + r[5] * world.y() - c[2];
    const double depth = d - z;
    if (depth < 1.0)
        return false;
    *out = QPointF(canvas.width() / 2.0 + d * x / depth, canvas.height() / 2.0 + d * y / depth);
    return true;
}

bool solveLinear(QVector<double> a, QVector<double> b, QVector<double> *x)
{
    const int n = b.size();
    for (int col = 0; col < n; ++col) {
        int pivot = col;
        for (int row = col + 1; row < n; ++row) {
            if (std::abs(a[row * n + col]) > std::abs(a[pivot * n + col]))
                pivot = row;
        }
        if (std::abs(a[pivot * n + col]) < 1e-12)
            return false;
        if (pivot != col) {
            for (int k = 0; k < n; ++k)
                std::swap(a[col * n + k], a[pivot * n + k]);
            std::swap(b[col], b[pivot]);
        }
        for (int row = col + 1; row < n; ++row) {
            const double f = a[row * n + col] / a[col * n + col];
            for (int k = col; k < n; ++k)
                a[row * n + k] -= f * a[col * n + k];
            b[row] -= f * b[col];
        }
    }
    x->resize(n);
    for (int row = n - 1; row >= 0; --row) {
        double sum = b[row];
        for (int k = row + 1; k < n; ++k)
            sum -= a[row * n + k] * (*x)[k];
        (*x)[row] = sum / a[row * n + row];
    }
    return true;
}

// Levenberg-Marquardt on the free parameters, minimising where `world` lands against `screen`.
// Returns the RMS error in px per point.
double refineCamera(CameraParams *params, const std::array<bool, 6> &free, double d, QSizeF canvas,
                    const QVector<QPointF> &world, const QVector<QPointF> &screen)
{
    constexpr double kMiss = 1e4;
    const int n = world.size() * 2;
    const auto residuals = [&](const CameraParams &p) {
        QVector<double> r(n);
        for (int i = 0; i < world.size(); ++i) {
            QPointF at;
            if (projectThroughCamera(p, d, canvas, world.at(i), &at)) {
                r[2 * i] = at.x() - screen.at(i).x();
                r[2 * i + 1] = at.y() - screen.at(i).y();
            } else {
                r[2 * i] = r[2 * i + 1] = kMiss;
            }
        }
        return r;
    };
    const auto cost = [](const QVector<double> &r) {
        double sum = 0;
        for (const double v : r)
            sum += v * v;
        return sum;
    };
    QVector<int> idx;
    for (int i = 0; i < 6; ++i) {
        if (free[i])
            idx.append(i);
    }
    const int k = idx.size();
    QVector<double> r = residuals(*params);
    double current = cost(r);
    double mu = 1e-3;
    for (int iteration = 0; iteration < 40 && current > 1e-12; ++iteration) {
        QVector<double> jacobian(n * k);
        for (int c = 0; c < k; ++c) {
            const double step = idx[c] < 3 ? 0.01 : 1e-4;
            CameraParams hi = *params;
            CameraParams lo = *params;
            hi[idx[c]] += step;
            lo[idx[c]] -= step;
            const QVector<double> rh = residuals(hi);
            const QVector<double> rl = residuals(lo);
            for (int i = 0; i < n; ++i)
                jacobian[i * k + c] = (rh[i] - rl[i]) / (2.0 * step);
        }
        QVector<double> jtj(k * k, 0.0);
        QVector<double> jtr(k, 0.0);
        for (int i = 0; i < n; ++i) {
            for (int a = 0; a < k; ++a) {
                jtr[a] += jacobian[i * k + a] * r[i];
                for (int b = 0; b < k; ++b)
                    jtj[a * k + b] += jacobian[i * k + a] * jacobian[i * k + b];
            }
        }
        bool accepted = false;
        double largest = 0;
        for (int attempt = 0; attempt < 10 && !accepted; ++attempt) {
            QVector<double> damped = jtj;
            QVector<double> rhs(k);
            for (int a = 0; a < k; ++a) {
                damped[a * k + a] += mu * (jtj[a * k + a] + 1e-9);
                rhs[a] = -jtr[a];
            }
            QVector<double> delta;
            if (solveLinear(damped, rhs, &delta)) {
                CameraParams trial = *params;
                largest = 0;
                for (int a = 0; a < k; ++a) {
                    trial[idx[a]] += delta[a];
                    largest = qMax(largest, std::abs(delta[a]) / (idx[a] < 3 ? 1.0 : 0.01));
                }
                const QVector<double> trialR = residuals(trial);
                const double trialCost = cost(trialR);
                if (trialCost < current) {
                    *params = trial;
                    r = trialR;
                    current = trialCost;
                    mu = qMax(1e-9, mu * 0.3);
                    accepted = true;
                    continue;
                }
            }
            mu *= 10.0;
        }
        if (!accepted || largest < 1e-6)
            break;
    }
    return std::sqrt(current / qMax(1, world.size()));
}

QPointF hostLocalToCanvas(const HostFrame &frame, QSizeF px, QPointF local)
{
    return frame.centre + rotated(QPointF((local.x() / px.width() - 0.5) * frame.w,
                                          (local.y() / px.height() - 0.5) * frame.h),
                                  frame.rotation);
}

// A planar track or a four-point perspective solve gives a whole homography to match, and so
// needs all six camera parameters. Anything else is a similarity: position, roll and distance.
bool usesFullCamera(const MotionTracker &tracker)
{
    if (tracker.kind == MotionTracker::Kind::Planar)
        return true;
    if (tracker.solve != MotionTracker::Solve::Perspective)
        return false;
    int count = 0;
    for (const TrackPoint &point : tracker.points) {
        if (point.samples.contains(tracker.referenceUs))
            ++count;
    }
    return count >= 4;
}

struct CameraChannels
{
    bool distance = false;
    bool roll = false;
    bool tilt = false; // rotationX and rotationY
};

CameraChannels cameraChannels(const MotionTracker &tracker, const drift::TrackerTarget &target)
{
    const bool full = usesFullCamera(tracker);
    const bool planar = tracker.kind == MotionTracker::Kind::Planar;
    const bool writesRotation = planar || tracker.solve != MotionTracker::Solve::Translate;
    const bool writesScale = planar || tracker.solve == MotionTracker::Solve::TranslateRotateScale
                             || tracker.solve == MotionTracker::Solve::Perspective;
    return {full || writesScale || target.zoom != 1.0, full || writesRotation, full};
}

// Everything that does not change from frame to frame: the points followed, where each must end
// up on screen, and the camera the clip had at the reference.
struct CameraSetup
{
    QSizeF px;
    QSizeF canvas;
    QVector<QPointF> locals;  // clip-frame px
    QVector<QPointF> targets; // canvas px
    CameraChannels channels;
    bool full = false;
    CameraParams rest{};
    double restPerspective = drift::kDefaultClipPerspective;
};

std::optional<CameraSetup> cameraSetup(const drift::Project &project, const Clip &host, const MotionTracker &tracker,
                                       const drift::TrackerTarget &target, const Clip &camera, TimeUs refTimeline,
                                       double zoom)
{
    CameraSetup s;
    s.px = frameSizeAt(project, host, refTimeline);
    s.canvas = QSizeF(project.width(), project.height());
    drift::TrackerTarget zoomed = target;
    zoomed.zoom = zoom;
    s.channels = cameraChannels(tracker, zoomed);
    s.full = usesFullCamera(tracker);
    for (int i = 0; i < qMin<int>(6, target.restCamera.size()); ++i)
        s.rest[i] = target.restCamera.at(i);
    const TimeUs at = camera.timelineStart + std::clamp<TimeUs>(refTimeline - camera.timelineStart, 0, camera.timelineDuration);
    s.restPerspective = std::max(1.0, drift::sceneCameraFromClip(camera, at, 1.0).perspective);

    const std::optional<QPointF> anchor = trackAnchor(tracker, s.px);
    if (!anchor)
        return std::nullopt;
    if (s.full) {
        if (tracker.kind == MotionTracker::Kind::Planar) {
            QVector<QPointF> region;
            for (const QPointF &corner : tracker.planarRegion)
                region.append(QPointF(corner.x() * s.px.width(), corner.y() * s.px.height()));
            if (region.size() < 4) {
                const QRectF box = QPolygonF(region).boundingRect();
                region = {box.topLeft(), box.topRight(), box.bottomRight(), box.bottomLeft()};
            }
            s.locals = region;
        } else {
            for (const TrackPoint &point : tracker.points) {
                const auto ref = point.samples.constFind(tracker.referenceUs);
                if (ref != point.samples.constEnd())
                    s.locals.append(QPointF(ref->pos.x() * s.px.width(), ref->pos.y() * s.px.height()));
            }
        }
    } else {
        const double length = qMax(10.0, qMin(s.px.width(), s.px.height()) / 4.0);
        s.locals = {*anchor, *anchor + QPointF(length, 0.0), *anchor + QPointF(0.0, length)};
    }

    const QPointF centre(s.canvas.width() / 2.0, s.canvas.height() / 2.0);
    const HostFrame ref = hostFrameAt(project, host, refTimeline);
    const auto restScreen = [&](QPointF local) {
        const QPointF canvas = hostLocalToCanvas(ref, s.px, local);
        QPointF out = canvas;
        projectThroughCamera(s.rest, s.restPerspective, s.canvas, canvas - centre, &out);
        return out;
    };
    const QPointF anchorOnScreen = restScreen(*anchor);
    const QPointF lock = target.centerOnTrack ? centre : anchorOnScreen;
    for (const QPointF &local : s.locals)
        s.targets.append(lock + (restScreen(local) - anchorOnScreen) * zoom);
    return s;
}

struct CameraFrame
{
    CameraParams params{};
    double rms = 0;
};

CameraFrame solveCameraFrame(const CameraSetup &s, const Homography &h, const HostFrame &now, double d,
                             const CameraParams &previous)
{
    using namespace drift::tracking;
    const QPointF centre(s.canvas.width() / 2.0, s.canvas.height() / 2.0);
    QVector<QPointF> world;
    for (const QPointF &local : s.locals)
        world.append(hostLocalToCanvas(now, s.px, applyHomography(h, local)) - centre);

    CameraFrame out;
    out.params = previous;
    std::array<bool, 6> free{true, true, true, true, true, true};
    if (s.full) {
        Homography m;
        if (fitHomography(world, s.targets, &m)) {
            // K^-1 M = lambda [F a1, F a2, t] with F = diag(1, 1, -1) and the a's the first two
            // columns of R^T.
            const double cx = s.canvas.width() / 2.0;
            const double cy = s.canvas.height() / 2.0;
            const auto column = [&](int c) {
                return std::array<double, 3>{(m[c] - cx * m[6 + c]) / d, (m[3 + c] - cy * m[6 + c]) / d, m[6 + c]};
            };
            std::array<double, 3> a1 = column(0);
            std::array<double, 3> a2 = column(1);
            std::array<double, 3> t = column(2);
            const auto length = [](const std::array<double, 3> &v) { return std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]); };
            const double la = length(a1);
            const double lb = length(a2);
            const double lambda = (la + lb) / 2.0;
            if (la > 1e-9 && lb > 1e-9) {
                const double sign = t[2] < 0 ? -1.0 : 1.0;
                std::array<double, 3> m1;
                std::array<double, 3> m2;
                for (int i = 0; i < 3; ++i) {
                    m1[i] = a1[i] / la;
                    m2[i] = a2[i] / lb;
                    t[i] *= sign / lambda;
                }
                // The unit sum and difference of two unit vectors are orthogonal, so this is the
                // nearest orthonormal pair without favouring either column.
                std::array<double, 3> sum;
                std::array<double, 3> diff;
                for (int i = 0; i < 3; ++i) {
                    sum[i] = m1[i] + m2[i];
                    diff[i] = m1[i] - m2[i];
                }
                const double ls = qMax(1e-12, length(sum));
                const double ld = qMax(1e-12, length(diff));
                for (int i = 0; i < 3; ++i) {
                    a1[i] = (sum[i] / ls + diff[i] / ld) * M_SQRT1_2;
                    a2[i] = (sum[i] / ls - diff[i] / ld) * M_SQRT1_2;
                }
                a1[2] = -a1[2];
                a2[2] = -a2[2];
                const std::array<double, 3> a3{a1[1] * a2[2] - a1[2] * a2[1], a1[2] * a2[0] - a1[0] * a2[2],
                                               a1[0] * a2[1] - a1[1] * a2[0]};
                const QMatrix4x4 rotation(float(a1[0]), float(a1[1]), float(a1[2]), 0.f, float(a2[0]), float(a2[1]),
                                          float(a2[2]), 0.f, float(a3[0]), float(a3[1]), float(a3[2]), 0.f, 0.f, 0.f,
                                          0.f, 1.f);
                double nearest[3] = {previous[3], previous[4], previous[5]};
                double angles[3];
                drift::gizmo::eulerFromMatrix(rotation, nearest, angles);
                out.params = {-t[0], -t[1], t[2] - d, angles[0], angles[1], angles[2]};
            }
        }
    } else {
        const QPointF wx = world[1] - world[0];
        const QPointF sx = s.targets[1] - s.targets[0];
        const double worldLength = std::hypot(wx.x(), wx.y());
        const double screenLength = std::hypot(sx.x(), sx.y());
        out.params = s.rest;
        if (worldLength > 1e-9 && screenLength > 1e-9) {
            const double sigma = s.channels.distance ? screenLength / worldLength : d / (d + s.rest[2]);
            const double phi = s.channels.roll ? std::atan2(sx.y(), sx.x()) - std::atan2(wx.y(), wx.x())
                                               : -qDegreesToRadians(s.rest[5]);
            double roll = -qRadiansToDegrees(phi);
            roll -= 360.0 * std::round((roll - previous[5]) / 360.0);
            const QPointF turned = rotated(world[0], qRadiansToDegrees(phi));
            const QPointF onScreen = s.targets[0] - centre;
            out.params[0] = turned.x() - onScreen.x() / sigma;
            out.params[1] = turned.y() - onScreen.y() / sigma;
            out.params[5] = roll;
            if (s.channels.distance)
                out.params[2] = d / sigma - d;
        }
        free = {true, true, s.channels.distance, false, false, s.channels.roll};
    }
    out.rms = refineCamera(&out.params, free, d, s.canvas, world, s.targets);
    return out;
}

struct CameraSolution
{
    QVector<PoseRun> runs;
    double worstRms = 0;
};

// The camera clip's keys that cancel the track, for the frames the camera clip covers.
CameraSolution solveCamera(const drift::Project &project, const Clip &host, const MotionTracker &tracker,
                           const drift::TrackerTarget &target, const Clip &camera, double zoom)
{
    CameraSolution solution;
    const TimeUs refTimeline = referenceTimelineUs(host, tracker);
    const std::optional<CameraSetup> setup = cameraSetup(project, host, tracker, target, camera, refTimeline, zoom);
    if (!setup)
        return solution;
    const QVector<FrameXform> frames = frameTransforms(project, host, tracker, setup->px);
    CameraParams previous = setup->rest;
    for (const QVector<int> &run : keyableRuns(frames, camera)) {
        PoseRun poses;
        for (const int i : run) {
            const TimeUs at = frames[i].timelineUs;
            const double d = std::max(1.0, drift::sceneCameraFromClip(camera, at, 1.0).perspective);
            const CameraFrame frame = solveCameraFrame(*setup, frames[i].h, hostFrameAt(project, host, at), d, previous);
            previous = frame.params;
            if (setup->full)
                solution.worstRms = qMax(solution.worstRms, frame.rms);
            Pose pose;
            pose.t = at - camera.timelineStart;
            pose.x = frame.params[0];
            pose.y = frame.params[1];
            pose.z = frame.params[2];
            pose.pitch = frame.params[3];
            pose.yaw = frame.params[4];
            pose.rotation = frame.params[5];
            poses.append(pose);
        }
        unwrapRotation(&poses);
        solution.runs.append(poses);
    }
    return solution;
}

// Keys the stabilizing camera on `camera`. Returns a warning to show, or empty.
QString writeCameraKeys(const drift::Project &project, const Clip &host, const MotionTracker &tracker,
                        const drift::TrackerTarget &target, Clip &camera)
{
    const CameraSolution solution = solveCamera(project, host, tracker, target, camera, target.zoom);
    if (solution.runs.isEmpty())
        return {};
    const CameraChannels channels = cameraChannels(tracker, target);
    const double d = std::max(1.0, drift::sceneCameraFromClip(camera, camera.timelineStart, 1.0).perspective);
    const double radius = std::hypot(project.width(), project.height()) / 2.0;
    writeChannel(camera.transformX, solution.runs, &Pose::x, kScalarTolerancePx);
    writeChannel(camera.transformY, solution.runs, &Pose::y, kScalarTolerancePx);
    if (channels.distance)
        writeChannel(camera.positionZ, solution.runs, &Pose::z, kScalarTolerancePx);
    if (channels.roll)
        writeChannel(camera.rotation, solution.runs, &Pose::rotation, qRadiansToDegrees(kScalarTolerancePx / radius));
    if (channels.tilt) {
        writeChannel(camera.rotationX, solution.runs, &Pose::pitch, qRadiansToDegrees(kScalarTolerancePx / d));
        writeChannel(camera.rotationY, solution.runs, &Pose::yaw, qRadiansToDegrees(kScalarTolerancePx / d));
    }
    if (solution.worstRms > kCameraRmsWarnPx) {
        return TrackingController::tr("The camera can't match this surface exactly; some frames drift by up to %1 px")
            .arg(qRound(solution.worstRms));
    }
    return {};
}

// True when the four canvas corners all lie inside the convex quad.
bool quadCoversCanvas(const QPointF (&quad)[4], QSizeF canvas)
{
    double area = 0;
    for (int i = 0; i < 4; ++i) {
        const QPointF a = quad[i];
        const QPointF b = quad[(i + 1) % 4];
        area += a.x() * b.y() - b.x() * a.y();
    }
    if (std::abs(area) < 1e-6)
        return false;
    const double orientation = area > 0 ? 1.0 : -1.0;
    for (const QPointF &corner : {QPointF(0, 0), QPointF(canvas.width(), 0), QPointF(canvas.width(), canvas.height()),
                                  QPointF(0, canvas.height())}) {
        for (int i = 0; i < 4; ++i) {
            const QPointF a = quad[i];
            const QPointF b = quad[(i + 1) % 4];
            const double cross = (b.x() - a.x()) * (corner.y() - a.y()) - (b.y() - a.y()) * (corner.x() - a.x());
            if (cross * orientation < -1e-6)
                return false;
        }
    }
    return true;
}

double fillZoomForCamera(const drift::Project &project, const Clip &host, const MotionTracker &tracker,
                         const drift::TrackerTarget &target)
{
    int t = -1;
    int c = -1;
    if (!findClipById(project, target.clipId, &t, &c))
        return 1.0;
    const Clip &camera = project.tracks().at(t).clips.at(c);
    const QSizeF canvas(project.width(), project.height());
    const QPointF centre(canvas.width() / 2.0, canvas.height() / 2.0);
    const auto covers = [&](double zoom) {
        const CameraSolution solution = solveCamera(project, host, tracker, target, camera, zoom);
        for (const PoseRun &run : solution.runs) {
            for (const Pose &pose : run) {
                const TimeUs at = camera.timelineStart + pose.t;
                const double d = std::max(1.0, drift::sceneCameraFromClip(camera, at, 1.0).perspective);
                const CameraParams params{pose.x, pose.y, pose.z, pose.pitch, pose.yaw, pose.rotation};
                const HostFrame frame = hostFrameAt(project, host, at);
                QPointF quad[4];
                int k = 0;
                for (const QPointF &corner : {QPointF(-0.5, -0.5), QPointF(0.5, -0.5), QPointF(0.5, 0.5), QPointF(-0.5, 0.5)}) {
                    const QPointF onCanvas = frame.centre + rotated(QPointF(corner.x() * frame.w, corner.y() * frame.h), frame.rotation);
                    if (!projectThroughCamera(params, d, canvas, onCanvas - centre, &quad[k++]))
                        return false;
                }
                if (!quadCoversCanvas(quad, canvas))
                    return false;
            }
        }
        return true;
    };
    if (covers(1.0))
        return 1.0;
    if (!covers(kMaxStabilizeZoom))
        return kMaxStabilizeZoom;
    double lo = 1.0;
    double hi = kMaxStabilizeZoom;
    for (int i = 0; i < 16; ++i) {
        const double mid = (lo + hi) / 2.0;
        (covers(mid) ? hi : lo) = mid;
    }
    return hi;
}

// The smallest zoom in [1, kMaxStabilizeZoom] at which the stabilized clip covers the canvas on every
// keyed frame.
double fillZoomFor(const drift::Project &project, const Clip &host, const MotionTracker &tracker,
                   const drift::TrackerTarget &target)
{
    if (target.via == drift::TrackerTarget::Via::Camera)
        return fillZoomForCamera(project, host, tracker, target);
    const StabilizeSetup setup = stabilizeSetup(project, host, tracker, target, referenceTimelineUs(host, tracker));
    const QVector<FrameXform> frames = frameTransforms(project, host, tracker, setup.px);
    const bool writesRotation = tracker.kind == MotionTracker::Kind::Planar
                                || tracker.solve != MotionTracker::Solve::Translate;
    const QSizeF canvas(project.width(), project.height());
    const auto covers = [&](double zoom) {
        for (const QVector<int> &run : keyableRuns(frames, host)) {
            for (const int i : run) {
                if (!coversCanvas(stabilizedPose(setup, frames[i].h, zoom, writesRotation), canvas))
                    return false;
            }
        }
        return true;
    };
    if (covers(1.0))
        return 1.0;
    if (!covers(kMaxStabilizeZoom))
        return kMaxStabilizeZoom;
    double lo = 1.0;
    double hi = kMaxStabilizeZoom;
    for (int i = 0; i < 24; ++i) {
        const double mid = (lo + hi) / 2.0;
        (covers(mid) ? hi : lo) = mid;
    }
    return hi;
}

// A mask a tracker can drive: a mask pinned to a clip, in that clip's frame, or the mask of a
// standalone effects layer, in canvas space.
bool isMaskTargetOwner(const drift::Project &project, int trackIndex, const Clip &owner)
{
    if (owner.type != drift::ClipType::Adjustment)
        return false;
    if (owner.adjustmentKind == drift::AdjustmentKind::Mask)
        return true;
    return owner.adjustmentKind == drift::AdjustmentKind::VideoEffects
           && owner.mask.shape != drift::MaskShape::None
           && !project.tracks().at(trackIndex).isAdjustmentLane();
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

QString timecodeText(TimeUs us, double fps)
{
    const int rate = qMax(1, qRound(fps));
    const qint64 frames = qRound64(drift::usToSeconds(us) * rate);
    return QStringLiteral("%1:%2:%3:%4")
        .arg(frames / (rate * 3600), 2, 10, QLatin1Char('0'))
        .arg(frames / (rate * 60) % 60, 2, 10, QLatin1Char('0'))
        .arg(frames / rate % 60, 2, 10, QLatin1Char('0'))
        .arg(frames % rate, 2, 10, QLatin1Char('0'));
}

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
            return {TrackingController::tr("Lost the feature at %1. Drag the marker back onto it and track again, "
                                           "or lower the stop threshold.")
                        .arg(timecodeText(job.timelineTimes.at(i), job.fps)),
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
        return {TrackingController::tr("Not enough detail inside the outline. Outline a larger area with visible texture."),
                QStringLiteral("warning")};

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
            return {TrackingController::tr("Lost the surface at %1. Drag its corners back into place and track again.")
                        .arg(timecodeText(job.timelineTimes.at(i), job.fps)),
                    QStringLiteral("warning")};
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

// The timeline span the tracker has samples in (inside the clip's source range); lo > hi when none.
void trackedSpan(const Clip &host, const MotionTracker &tracker, TimeUs *lo, TimeUs *hi)
{
    *lo = std::numeric_limits<TimeUs>::max();
    *hi = std::numeric_limits<TimeUs>::min();
    const auto widen = [&](TimeUs key) {
        if (key < host.srcIn || key > host.srcOut)
            return;
        const TimeUs at = sampleTimelineUs(host, key);
        *lo = qMin(*lo, at);
        *hi = qMax(*hi, at);
    };
    for (const TrackPoint &point : tracker.points) {
        for (auto it = point.samples.constBegin(); it != point.samples.constEnd(); ++it)
            widen(it.key());
    }
    for (auto it = tracker.homographies.constBegin(); it != tracker.homographies.constEnd(); ++it)
        widen(it.key());
}

} // namespace

TrackingController::TrackingController(AppController *app, QObject *parent)
    : QObject(parent)
    , m_app(app)
{
    // Undo, redo and every other edit funnel through tracksChanged, and trackers ride in the clips.
    connect(m_app, &AppController::tracksChanged, this, &TrackingController::trackersChanged);
    m_trackerList = new TrackerListModel(this);
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
                                       {QStringLiteral("via"), target.via == drift::TrackerTarget::Via::Camera
                                                                   ? QStringLiteral("camera")
                                                                   : QStringLiteral("clip")},
                                       {QStringLiteral("zoom"), target.zoom},
                                       {QStringLiteral("name"), name}});
        }

        TimeUs lo = 0;
        TimeUs hi = -1;
        trackedSpan(*host, tracker, &lo, &hi);

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

TrackingController::FrameView TrackingController::frameView(int trackIndex, int clipIndex, const QString &id,
                                                            double timelineSeconds) const
{
    FrameView out;
    const Clip *host = hostView(trackIndex, clipIndex);
    const MotionTracker *tracker = host ? trackerById(*host, id) : nullptr;
    if (!tracker)
        return out;
    const drift::Project &project = m_app->m_project;
    const TimeUs tolerance = matchTolerance(project, *host);
    const TimeUs frame = drift::frameDurationUs(project.fps());
    const TimeUs at = drift::secondsToUs(timelineSeconds);
    const TimeUs sourceUs = host->timelineToSourceUs(at);

    for (const TrackPoint &point : tracker->points) {
        const auto it = nearestKey(point.samples, sourceUs, tolerance);
        const bool has = it != point.samples.constEnd();
        // Off the tracked span the marker stays where the closest sample left it, so it can be dragged
        // back onto the feature; that drag writes a key here.
        const auto ghostIt = has ? it : nearestKey(point.samples, sourceUs, std::numeric_limits<TimeUs>::max() / 4);
        const bool ghost = !has && ghostIt != point.samples.constEnd();
        FramePoint entry;
        entry.x = has ? it->pos.x() : (ghost ? ghostIt->pos.x() : 0.0);
        entry.y = has ? it->pos.y() : (ghost ? ghostIt->pos.y() : 0.0);
        entry.confidence = has ? double(it->confidence) : 0.0;
        entry.userKey = has && it->userKey;
        entry.patternW = point.patternSize.width();
        entry.patternH = point.patternSize.height();
        entry.searchW = point.searchSize.width();
        entry.searchH = point.searchSize.height();
        entry.has = has;
        entry.ghost = ghost;
        out.points.append(entry);

        QVector<QPointF> trail;
        for (int k = -kTrailFrames; k <= kTrailFrames; ++k) {
            const auto near = nearestKey(point.samples, host->timelineToSourceUs(at + k * frame), tolerance);
            if (near != point.samples.constEnd())
                trail.append(near->pos);
        }
        out.trails.append(trail);
    }

    if (tracker->kind == MotionTracker::Kind::Planar) {
        QPolygonF polygon(tracker->planarRegion);
        auto it = nearestKey(tracker->homographies, sourceUs, tolerance);
        if (it == tracker->homographies.constEnd())
            it = nearestKey(tracker->homographies, sourceUs, std::numeric_limits<TimeUs>::max() / 4);
        if (it != tracker->homographies.constEnd())
            polygon = drift::tracking::mapPolygon(it.value(), polygon);
        for (const QPointF &p : polygon)
            out.region.append(p);
    }
    return out;
}

TrackingController::FrameView TrackingController::frameView(const QString &clipId, const QString &id,
                                                            double timelineSeconds) const
{
    int t = -1;
    int c = -1;
    return locateClip(clipId, &t, &c) ? frameView(t, c, id, timelineSeconds) : FrameView{};
}

QVariantList TrackingController::trackers(const QString &clipId) const
{
    int t = -1;
    int c = -1;
    return locateClip(clipId, &t, &c) ? trackers(t, c) : QVariantList{};
}

QString TrackingController::addPointTracker(const QString &clipId, double x, double y)
{
    int t = -1;
    int c = -1;
    return locateClip(clipId, &t, &c) ? addPointTracker(t, c, x, y) : QString{};
}

QString TrackingController::addPlanarTracker(const QString &clipId, const QVariantList &region)
{
    int t = -1;
    int c = -1;
    return locateClip(clipId, &t, &c) ? addPlanarTracker(t, c, region) : QString{};
}

void TrackingController::addTrackerPoint(const QString &clipId, const QString &id, double x, double y)
{
    int t = -1;
    int c = -1;
    if (locateClip(clipId, &t, &c))
        addTrackerPoint(t, c, id, x, y);
}

void TrackingController::setTrackerProperty(const QString &clipId, const QString &id, const QString &key,
                                            const QVariant &value)
{
    int t = -1;
    int c = -1;
    if (locateClip(clipId, &t, &c))
        setTrackerProperty(t, c, id, key, value);
}

void TrackingController::removeTracker(const QString &clipId, const QString &id, bool removeKeys)
{
    int t = -1;
    int c = -1;
    if (locateClip(clipId, &t, &c))
        removeTracker(t, c, id, removeKeys);
}

void TrackingController::clearSamples(const QString &clipId, const QString &id, bool forward)
{
    int t = -1;
    int c = -1;
    if (locateClip(clipId, &t, &c))
        clearSamples(t, c, id, forward);
}

void TrackingController::track(const QString &clipId, const QString &id, bool forward, bool singleFrame)
{
    int t = -1;
    int c = -1;
    if (locateClip(clipId, &t, &c))
        track(t, c, id, forward, singleFrame);
}

void TrackingController::setTarget(const QString &clipId, const QString &id, const QString &targetClipId,
                                   const QString &mode)
{
    int t = -1;
    int c = -1;
    if (locateClip(clipId, &t, &c))
        setTarget(t, c, id, targetClipId, mode);
}

void TrackingController::removeTarget(const QString &clipId, const QString &id, const QString &targetClipId)
{
    int t = -1;
    int c = -1;
    if (locateClip(clipId, &t, &c))
        removeTarget(t, c, id, targetClipId);
}

void TrackingController::setTargetCenter(const QString &clipId, const QString &id, const QString &targetClipId,
                                         bool center)
{
    int t = -1;
    int c = -1;
    if (locateClip(clipId, &t, &c))
        setTargetCenter(t, c, id, targetClipId, center);
}

void TrackingController::setStabilize(const QString &clipId, const QString &trackerId, const QString &via)
{
    int t = -1;
    int c = -1;
    if (!locateClip(clipId, &t, &c))
        return;
    QString error;
    const drift::TrackerTarget::Via mode =
        via == QLatin1String("camera") ? drift::TrackerTarget::Via::Camera : drift::TrackerTarget::Via::Clip;
    editTracker(t, c, trackerId, tr("Lock to screen"), [&](Clip &host, MotionTracker &tracker) {
        return addTarget(host, tracker, host.id, QStringLiteral("stabilize"), &error, mode);
    });
    if (!error.isEmpty())
        m_app->setLastMessage(error, QStringLiteral("warning"));
}

void TrackingController::setTargetZoom(const QString &clipId, const QString &trackerId, const QString &targetClipId,
                                       double zoom)
{
    int t = -1;
    int c = -1;
    if (!locateClip(clipId, &t, &c))
        return;
    QString error;
    const bool ok = editTracker(t, c, trackerId, tr("Change zoom"), [&](Clip &host, MotionTracker &tracker) {
        return zoomTarget(host, tracker, targetClipId, zoom, &error);
    });
    if (ok && !error.isEmpty())
        m_app->setLastMessage(error, QStringLiteral("warning"));
}

double TrackingController::fillZoom(const QString &clipId, const QString &trackerId) const
{
    int t = -1;
    int c = -1;
    if (!locateClip(clipId, &t, &c))
        return 1.0;
    const Clip *host = hostView(t, c);
    const MotionTracker *tracker = trackerById(*host, trackerId);
    if (!tracker)
        return 1.0;
    double zoom = 1.0;
    for (const drift::TrackerTarget &target : tracker->targets) {
        if (target.mode == drift::TrackerTarget::Mode::Stabilize)
            zoom = qMax(zoom, fillZoomFor(m_app->m_project, *host, *tracker, target));
    }
    return zoom;
}

void TrackingController::autoZoom(const QString &clipId, const QString &trackerId)
{
    int t = -1;
    int c = -1;
    if (!locateClip(clipId, &t, &c))
        return;
    QString error;
    const bool ok = editTracker(t, c, trackerId, tr("Zoom to fill"), [&](Clip &host, MotionTracker &tracker) {
        double zoom = 1.0;
        QString stabilizeId;
        for (const drift::TrackerTarget &target : tracker.targets) {
            if (target.mode != drift::TrackerTarget::Mode::Stabilize)
                continue;
            zoom = qMax(zoom, fillZoomFor(m_app->m_project, host, tracker, target));
            stabilizeId = target.clipId;
        }
        return !stabilizeId.isEmpty() && zoomTarget(host, tracker, stabilizeId, zoom, &error);
    });
    if (ok && !error.isEmpty())
        m_app->setLastMessage(error, QStringLiteral("warning"));
}

void TrackingController::apply(const QString &clipId, const QString &id)
{
    int t = -1;
    int c = -1;
    if (locateClip(clipId, &t, &c))
        apply(t, c, id);
}

void TrackingController::retargetMask(const QString &fromId, const QString &toId)
{
    QList<QPair<QString, QString>> moved;
    for (drift::Track &track : m_app->m_project.tracks()) {
        for (Clip &host : track.clips) {
            for (MotionTracker &tracker : host.motionTrackers) {
                bool changed = false;
                for (drift::TrackerTarget &target : tracker.targets) {
                    if (target.clipId == fromId && target.mode == drift::TrackerTarget::Mode::Mask) {
                        target.clipId = toId;
                        changed = true;
                    }
                }
                if (changed)
                    moved.append({host.id, tracker.id});
            }
        }
    }
    for (const QPair<QString, QString> &pair : std::as_const(moved))
        applyTargets(pair.first, pair.second, toId);
}

QString TrackingController::trackerForTarget(const QString &targetClipId) const
{
    if (targetClipId.isEmpty())
        return {};
    for (const drift::Track &track : std::as_const(m_app->m_project).tracks()) {
        for (const Clip &host : track.clips) {
            for (const MotionTracker &tracker : host.motionTrackers) {
                for (const drift::TrackerTarget &target : tracker.targets) {
                    if (target.clipId == targetClipId)
                        return tracker.id;
                }
            }
        }
    }
    return {};
}

bool TrackingController::maskHostIds(const QString &maskClipId, QString *hostId) const
{
    const drift::Project &project = m_app->m_project;
    int t = -1;
    int c = -1;
    if (!locateClip(maskClipId, &t, &c))
        return false;
    const Clip &mask = project.tracks().at(t).clips.at(c);
    if (mask.type != drift::ClipType::Adjustment)
        return false;
    int ht = -1;
    int hc = -1;
    if (!mask.linkedClipId.isEmpty()) {
        if (!locateClip(mask.linkedClipId, &ht, &hc))
            return false;
    } else {
        ht = drift::adjustmentLaneParentIndex(project, t);
        if (ht < 0)
            return false;
        const QList<Clip> &clips = project.tracks().at(ht).clips;
        for (hc = 0; hc < clips.size(); ++hc) {
            if (clips.at(hc).containsTime(m_app->m_playheadUs) && clips.at(hc).type == drift::ClipType::Video)
                break;
        }
        if (hc >= clips.size())
            return false;
    }
    const Clip &host = project.tracks().at(ht).clips.at(hc);
    if (host.type != drift::ClipType::Video || host.path.isEmpty())
        return false;
    *hostId = host.id;
    return true;
}

namespace {

// Where a mask sits at a timeline time, normalized to `host`'s frame: its centre (a polygon's
// centroid).
QPointF maskCentreInHost(const drift::Project &project, const Clip &host, const Clip &owner, TimeUs timelineUs)
{
    const drift::Mask at = owner.mask.resolvedAt(timelineUs - owner.timelineStart);
    QPointF centre(at.x, at.y);
    if (at.shape == drift::MaskShape::Freeform && !at.points.isEmpty()) {
        centre = QPointF();
        for (const QPointF &p : at.points)
            centre += p;
        centre /= double(at.points.size());
    }
    return owner.adjustmentKind == drift::AdjustmentKind::VideoEffects
               ? drift::canvasToHostFrame(project, host, timelineUs, centre)
               : centre;
}

bool writePointKey(const drift::Project &project, const Clip &host, MotionTracker &tracker, TimeUs playheadUs,
                   int pointIndex, double x, double y);

} // namespace

QString TrackingController::ensureMaskTracker(const QString &maskClipId, double x, double y)
{
    if (m_busy)
        return {};
    const QString existing = trackerForTarget(maskClipId);
    if (!existing.isEmpty())
        return existing;
    QString hostId;
    int ht = -1;
    int hc = -1;
    if (!maskHostIds(maskClipId, &hostId) || !locateClip(hostId, &ht, &hc))
        return {};

    if (x < 0.0) {
        int mt = -1;
        int mc = -1;
        locateClip(maskClipId, &mt, &mc);
        const drift::Project &project = std::as_const(m_app->m_project);
        const Clip &host = project.tracks().at(ht).clips.at(hc);
        const TimeUs playhead = qBound(host.timelineStart, m_app->m_playheadUs, host.timelineEnd() - 1);
        const QPointF centre = maskCentreInHost(project, host, project.tracks().at(mt).clips.at(mc), playhead);
        x = qBound(0.0, centre.x(), 1.0);
        y = qBound(0.0, centre.y(), 1.0);
    }

    const drift::Project before = m_app->m_project;
    Clip *host = hostAt(ht, hc);
    host->motionTrackers.append(buildPointTracker(*host, x, y));
    MotionTracker &tracker = host->motionTrackers.last();
    const QString id = tracker.id;
    QString error;
    addTarget(*host, tracker, maskClipId, QStringLiteral("mask"), &error);

    m_app->pushProjectEdit(before, tr("Track mask"));
    m_app->finishEdit(tr("Tracker added"));
    if (!error.isEmpty())
        m_app->setLastMessage(error, QStringLiteral("warning"));
    emit trackersChanged();
    return id;
}

void TrackingController::correctFromMask(const QString &maskClipId)
{
    const QString id = trackerForTarget(maskClipId);
    QString hostId;
    int ht = -1;
    int hc = -1;
    int mt = -1;
    int mc = -1;
    if (m_busy || id.isEmpty() || !locateClip(maskClipId, &mt, &mc))
        return;
    for (const drift::Track &track : std::as_const(m_app->m_project).tracks()) {
        for (const Clip &clip : track.clips) {
            if (trackerById(clip, id))
                hostId = clip.id;
        }
    }
    if (!locateClip(hostId, &ht, &hc))
        return;

    const drift::Project &project = std::as_const(m_app->m_project);
    const Clip &hostClip = project.tracks().at(ht).clips.at(hc);
    const Clip &owner = project.tracks().at(mt).clips.at(mc);
    const MotionTracker *view = trackerById(hostClip, id);
    if (view->kind != MotionTracker::Kind::Point || view->points.size() != 1)
        return;
    const TimeUs playhead = qBound(hostClip.timelineStart, m_app->m_playheadUs, hostClip.timelineEnd() - 1);
    const QPointF now = maskCentreInHost(project, hostClip, owner, playhead);
    const QPointF rest = maskCentreInHost(project, hostClip, owner, referenceTimelineUs(hostClip, *view));
    const auto reference = view->points.first().samples.constFind(view->referenceUs);
    if (reference == view->points.first().samples.constEnd())
        return;
    const QPointF target(qBound(0.0, reference->pos.x() + now.x() - rest.x(), 1.0),
                         qBound(0.0, reference->pos.y() + now.y() - rest.y(), 1.0));

    QString error;
    const bool ok = editTracker(ht, hc, id, tr("Correct track"), [&](Clip &host, MotionTracker &tracker) {
        if (!writePointKey(m_app->m_project, host, tracker, m_app->m_playheadUs, 0, target.x(), target.y()))
            return false;
        error = applyTargets(host.id, tracker.id);
        return true;
    });
    if (ok && !error.isEmpty())
        m_app->setLastMessage(error, QStringLiteral("warning"));
}

QVariantMap TrackingController::videoUnder(const QString &followerClipId) const
{
    const drift::Project &project = m_app->m_project;
    int ft = -1;
    int fc = -1;
    if (!locateClip(followerClipId, &ft, &fc))
        return {};
    for (int t = ft + 1; t < project.tracks().size(); ++t) {
        const drift::Track &track = project.tracks().at(t);
        if (track.type != drift::TrackType::Video)
            continue;
        for (int c = 0; c < track.clips.size(); ++c) {
            const Clip &clip = track.clips.at(c);
            if (clip.type == drift::ClipType::Video && !clip.path.isEmpty() && clip.containsTime(m_app->m_playheadUs)) {
                return QVariantMap{{QStringLiteral("clipId"), clip.id},
                                   {QStringLiteral("track"), t},
                                   {QStringLiteral("clip"), c},
                                   {QStringLiteral("name"), clip.name}};
            }
        }
    }
    return {};
}

QString TrackingController::followWithNewTracker(const QString &hostClipId, const QString &followerClipId,
                                                 double x, double y)
{
    int ht = -1;
    int hc = -1;
    int ft = -1;
    int fc = -1;
    const Clip *view = locateClip(hostClipId, &ht, &hc) ? hostView(ht, hc) : nullptr;
    if (m_busy || !view || view->type != drift::ClipType::Video || view->path.isEmpty()
        || !locateClip(followerClipId, &ft, &fc) || followerClipId == hostClipId)
        return {};
    const drift::Project before = m_app->m_project;
    Clip *host = hostAt(ht, hc);
    host->motionTrackers.append(buildPointTracker(*host, x, y));
    MotionTracker &tracker = host->motionTrackers.last();
    const QString id = tracker.id;
    QString error;
    addTarget(*host, tracker, followerClipId, QStringLiteral("followClip"), &error);

    m_app->pushProjectEdit(before, tr("Follow object"));
    m_app->finishEdit(tr("Tracker added"));
    if (!error.isEmpty())
        m_app->setLastMessage(error, QStringLiteral("warning"));
    emit trackersChanged();
    return id;
}

QString TrackingController::addMaskForTracker(const QString &clipId, const QString &trackerId)
{
    int ht = -1;
    int hc = -1;
    const Clip *view = locateClip(clipId, &ht, &hc) ? hostView(ht, hc) : nullptr;
    const MotionTracker *viewTracker = view ? trackerById(*view, trackerId) : nullptr;
    if (m_busy || !viewTracker || view->type != drift::ClipType::Video)
        return {};

    const FrameView frame = frameView(ht, hc, trackerId,
                                      drift::usToSeconds(referenceTimelineUs(*view, *viewTracker)));
    drift::Mask mask;
    if (viewTracker->kind == MotionTracker::Kind::Planar) {
        if (frame.region.size() < 3)
            return {};
        mask.shape = drift::MaskShape::Freeform;
        mask.points = frame.region;
    } else {
        if (frame.points.isEmpty())
            return {};
        QPointF centre;
        for (const FramePoint &p : frame.points)
            centre += QPointF(p.x, p.y);
        centre /= double(frame.points.size());
        mask = m_app->maskFromCatalogId(QStringLiteral("rectangle"));
        mask.x = qBound(0.0, centre.x(), 1.0);
        mask.y = qBound(0.0, centre.y(), 1.0);
        mask.w = qBound(0.06, frame.points.first().patternW * 2.0, 0.8);
        mask.h = qBound(0.06, frame.points.first().patternH * 2.0, 0.8);
    }

    const drift::Project before = m_app->m_project;
    const drift::ClipRef added = drift::addLinkedMask(m_app->m_project, ht, hc, mask);
    if (added.trackIndex < 0)
        return {};
    const QString maskId = m_app->m_project.tracks().at(added.trackIndex).clips.at(added.clipIndex).id;

    // addLinkedMask can insert a lane, so the host's index may have moved.
    if (!locateClip(clipId, &ht, &hc))
        return {};
    Clip *host = hostAt(ht, hc);
    MotionTracker *tracker = trackerById(*host, trackerId);
    if (!tracker)
        return {};
    QString error;
    addTarget(*host, *tracker, maskId, QStringLiteral("mask"), &error);

    m_app->pushProjectEdit(before, tr("Add mask"));
    m_app->finishEdit(tr("Mask added"));
    if (!error.isEmpty())
        m_app->setLastMessage(error, QStringLiteral("warning"));
    m_app->selectClipById(maskId);
    emit trackersChanged();
    return maskId;
}

QVariantMap TrackingController::clipRefById(const QString &clipId) const
{
    int t = -1;
    int c = -1;
    if (!locateClip(clipId, &t, &c))
        return {};
    return QVariantMap{{QStringLiteral("track"), t},
                       {QStringLiteral("clip"), c},
                       {QStringLiteral("name"), m_app->m_project.tracks().at(t).clips.at(c).name}};
}

QVariantList TrackingController::trackersDriving(const QString &clipId) const
{
    QVariantList out;
    if (clipId.isEmpty())
        return out;
    for (const drift::Track &track : std::as_const(m_app->m_project).tracks()) {
        for (const Clip &host : track.clips) {
            for (const MotionTracker &tracker : host.motionTrackers) {
                for (const drift::TrackerTarget &target : tracker.targets) {
                    if (target.clipId != clipId)
                        continue;
                    out.append(QVariantMap{{QStringLiteral("trackerId"), tracker.id},
                                           {QStringLiteral("trackerName"), tracker.name},
                                           {QStringLiteral("hostClipId"), host.id},
                                           {QStringLiteral("hostName"), host.name},
                                           {QStringLiteral("mode"), drift::trackerTargetModeToString(target.mode)}});
                }
            }
        }
    }
    return out;
}

QVariantMap TrackingController::maskTrackInfo(const QString &maskClipId) const
{
    QVariantMap out{{QStringLiteral("trackerId"), QString()},
                    {QStringLiteral("trackState"), QStringLiteral("none")},
                    {QStringLiteral("solve"), QStringLiteral("translate")},
                    {QStringLiteral("trackerPoints"), 0}};
    for (const drift::Track &track : std::as_const(m_app->m_project).tracks()) {
        for (const Clip &host : track.clips) {
            for (const MotionTracker &tracker : host.motionTrackers) {
                const bool drives = std::any_of(tracker.targets.begin(), tracker.targets.end(),
                    [&](const drift::TrackerTarget &target) {
                        return target.clipId == maskClipId && target.mode == drift::TrackerTarget::Mode::Mask;
                    });
                if (!drives)
                    continue;
                out[QStringLiteral("trackerId")] = tracker.id;
                out[QStringLiteral("solve")] = drift::motionTrackerSolveToString(tracker.solve);
                out[QStringLiteral("trackerPoints")] = int(tracker.points.size());
                TimeUs lo = 0;
                TimeUs hi = -1;
                trackedSpan(host, tracker, &lo, &hi);
                if (lo < hi) {
                    const TimeUs slack = 2 * drift::secondsToUs(1.0 / std::max(1, int(m_app->projectFile()->projectFps())));
                    out[QStringLiteral("trackState")] = lo <= host.timelineStart + slack && hi >= host.timelineEnd() - slack
                                                            ? QStringLiteral("full")
                                                            : QStringLiteral("partial");
                }
                return out;
            }
        }
    }
    return out;
}

bool TrackingController::locateClip(const QString &clipId, int *trackIndex, int *clipIndex) const
{
    return !clipId.isEmpty() && findClipById(m_app->m_project, clipId, trackIndex, clipIndex);
}

QVariantMap TrackingController::trackerFrame(int trackIndex, int clipIndex, const QString &id,
                                             double timelineSeconds) const
{
    const Clip *host = hostView(trackIndex, clipIndex);
    if (!host || !trackerById(*host, id))
        return {};
    const FrameView view = frameView(trackIndex, clipIndex, id, timelineSeconds);
    QVariantList points;
    for (const FramePoint &p : view.points) {
        points.append(QVariantMap{
            {QStringLiteral("x"), p.x},
            {QStringLiteral("y"), p.y},
            {QStringLiteral("confidence"), p.confidence},
            {QStringLiteral("userKey"), p.userKey},
            {QStringLiteral("patternW"), p.patternW},
            {QStringLiteral("patternH"), p.patternH},
            {QStringLiteral("searchW"), p.searchW},
            {QStringLiteral("searchH"), p.searchH},
            {QStringLiteral("has"), p.has},
            {QStringLiteral("ghost"), p.ghost},
        });
    }
    QVariantList trails;
    for (const QVector<QPointF> &trail : view.trails) {
        QVariantList list;
        for (const QPointF &p : trail)
            list.append(pointMap(p.x(), p.y()));
        trails.append(list);
    }
    QVariantList region;
    for (const QPointF &p : view.region)
        region.append(pointMap(p.x(), p.y()));
    return {{QStringLiteral("points"), points}, {QStringLiteral("region"), region}, {QStringLiteral("trail"), trails}};
}

QVector<double> TrackingController::confidenceBuckets(int trackIndex, int clipIndex, const QString &id,
                                                      int buckets) const
{
    const Clip *host = hostView(trackIndex, clipIndex);
    const MotionTracker *tracker = host ? trackerById(*host, id) : nullptr;
    if (!tracker || buckets <= 0)
        return {};
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
    return lowest;
}

QVector<double> TrackingController::confidenceBuckets(const QString &clipId, const QString &id, int buckets) const
{
    int t = -1;
    int c = -1;
    return locateClip(clipId, &t, &c) ? confidenceBuckets(t, c, id, buckets) : QVector<double>{};
}

QVariantList TrackingController::confidenceStrip(int trackIndex, int clipIndex, const QString &id, int buckets) const
{
    QVariantList out;
    for (const double v : confidenceBuckets(trackIndex, clipIndex, id, buckets))
        out.append(v);
    return out;
}

MotionTracker TrackingController::buildPointTracker(Clip &host, double x, double y) const
{
    const TimeUs playhead = qBound(host.timelineStart, m_app->m_playheadUs, host.timelineEnd() - 1);
    const QSizeF px = frameSizeAt(m_app->m_project, host, playhead);

    MotionTracker tracker;
    tracker.id = QStringLiteral("trk_") + QUuid::createUuid().toString(QUuid::WithoutBraces);
    tracker.name = tr("Tracker %1").arg(host.motionTrackers.size() + 1);
    tracker.referenceUs = host.timelineToSourceUs(playhead);
    TrackPoint point;
    const double aspect = px.width() / px.height();
    point.patternSize = QSizeF(0.05, 0.05 * aspect);
    point.searchSize = QSizeF(0.15, 0.15 * aspect);
    point.samples.insert(tracker.referenceUs, TrackSample{QPointF(x, y), 1.0f, true});
    tracker.points.append(point);
    return tracker;
}

QString TrackingController::addPointTracker(int trackIndex, int clipIndex, double x, double y)
{
    const Clip *view = hostView(trackIndex, clipIndex);
    if (m_busy || !view || view->type != drift::ClipType::Video || view->path.isEmpty())
        return {};
    const drift::Project before = m_app->m_project;
    Clip *host = hostAt(trackIndex, clipIndex);
    const MotionTracker tracker = buildPointTracker(*host, x, y);
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

// A hand-placed key restarts the track from there: the automatic samples past it, on the side away
// from the reference, were followed from the old position. They go, up to the next hand-placed key.
void dropAutoSamplesBeyond(const Clip &host, const MotionTracker &tracker, TrackPoint &point, TimeUs keySourceUs)
{
    const TimeUs keyAt = sampleTimelineUs(host, keySourceUs);
    const TimeUs referenceAt = sampleTimelineUs(host, tracker.referenceUs);
    if (keyAt == referenceAt)
        return;
    const bool forward = keyAt > referenceAt;
    QVector<QPair<TimeUs, TimeUs>> beyond;
    for (auto it = point.samples.constBegin(); it != point.samples.constEnd(); ++it) {
        const TimeUs at = sampleTimelineUs(host, it.key());
        if (forward ? at > keyAt : at < keyAt)
            beyond.append({at, it.key()});
    }
    std::sort(beyond.begin(), beyond.end(), [&](const auto &a, const auto &b) {
        return forward ? a.first < b.first : a.first > b.first;
    });
    for (const auto &entry : beyond) {
        const auto it = point.samples.constFind(entry.second);
        if (it->userKey)
            break;
        point.samples.erase(point.samples.constFind(entry.second));
    }
}

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
    dropAutoSamplesBeyond(host, tracker, point, key);
    return true;
}

} // namespace

void TrackingController::setTrackerPoint(int trackIndex, int clipIndex, const QString &id, int pointIndex,
                                         double x, double y)
{
    QString error;
    const bool ok = editTracker(trackIndex, clipIndex, id, tr("Move tracking point"), [&](Clip &host, MotionTracker &tracker) {
        if (!writePointKey(m_app->m_project, host, tracker, m_app->m_playheadUs, pointIndex, x, y))
            return false;
        if (!tracker.targets.isEmpty())
            error = applyTargets(host.id, tracker.id);
        return true;
    });
    if (ok && !error.isEmpty())
        m_app->setLastMessage(error, QStringLiteral("warning"));
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
    if (!tracker->targets.isEmpty())
        applyTargets(host->id, id);
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
        bool clipLock = false;
        if (removeKeys) {
            for (const drift::TrackerTarget &target : tracker.targets) {
                unapplyTarget(host, tracker, target);
                clipLock = clipLock || (target.mode == drift::TrackerTarget::Mode::Stabilize
                                        && target.via == drift::TrackerTarget::Via::Clip);
            }
        }
        for (int i = 0; i < host.motionTrackers.size(); ++i) {
            if (host.motionTrackers.at(i).id == id) {
                host.motionTrackers.removeAt(i);
                if (clipLock)
                    reapplyHost(host.id, {});
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
    job.fps = project.fps();
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
            m_app->setLastMessage(tr("Drag the outline's corners onto the surface at this frame, then track again"), QStringLiteral("warning"));
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
            m_app->setLastMessage(tr("Drag the marker onto the feature at this frame, then track again"), QStringLiteral("warning"));
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
        for (const Clip &clip : project.tracks().at(t).clips) {
            if (clip.type == drift::ClipType::Adjustment && clip.adjustmentKind == drift::AdjustmentKind::VideoEffects
                && isMaskTargetOwner(project, t, clip) && clip.timelineStart < host->timelineEnd()
                && clip.timelineEnd() > host->timelineStart) {
                out.append(QVariantMap{{QStringLiteral("clipId"), clip.id},
                                       {QStringLiteral("name"), tr("%1 (adjustment)").arg(clip.name)},
                                       {QStringLiteral("kind"), QStringLiteral("mask")}});
            }
            if (t == trackIndex)
                continue;
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

bool TrackingController::addTarget(Clip &host, MotionTracker &tracker, const QString &targetClipId,
                                   const QString &mode, QString *error, drift::TrackerTarget::Via via)
{
    const drift::TrackerTarget::Mode targetMode = drift::trackerTargetModeFromString(mode);
    const bool stabilize = targetMode == drift::TrackerTarget::Mode::Stabilize;
    if (targetClipId.isEmpty() || (targetClipId == host.id && !stabilize))
        return false;
    if (stabilize) {
        if (host.layer3d) {
            *error = tr("Lock to screen is not available for 3D clips");
            return false;
        }
        if (via == drift::TrackerTarget::Via::Clip && targetClipId != host.id)
            return false;
        // One stabilize per tracked clip: whatever locked it before lets go, keeping the zoom and
        // anchor the user chose on this tracker.
        double zoom = 1.0;
        bool center = false;
        bool hadClipLock = false;
        for (MotionTracker &other : host.motionTrackers) {
            for (int i = 0; i < other.targets.size(); ++i) {
                const drift::TrackerTarget old = other.targets.at(i);
                if (old.mode != drift::TrackerTarget::Mode::Stabilize)
                    continue;
                if (other.id == tracker.id && old.via == via)
                    return false;
                if (other.id == tracker.id) {
                    zoom = old.zoom;
                    center = old.centerOnTrack;
                }
                unapplyTarget(host, other, old);
                other.targets.removeAt(i--);
                hadClipLock = hadClipLock || old.via == drift::TrackerTarget::Via::Clip;
            }
        }
        if (hadClipLock)
            reapplyHost(host.id, tracker.id);
        drift::TrackerTarget target{targetClipId, targetMode};
        target.via = via;
        target.zoom = zoom;
        target.centerOnTrack = center;
        if (via == drift::TrackerTarget::Via::Clip) {
            const TimeUs rel = qMax<TimeUs>(0, referenceTimelineUs(host, tracker) - host.timelineStart);
            const auto own = [&](const drift::KeyframeTrack<double> &track, double fallback) {
                return track.isEmpty() ? fallback : track.evaluateAt(rel);
            };
            target.hasRest = true;
            target.restRect = QRectF(own(host.transformX, 0.0), own(host.transformY, 0.0),
                                     own(host.transformW, m_app->m_project.width()),
                                     own(host.transformH, m_app->m_project.height()));
            target.restRotation = own(host.rotation, 0.0);
            tracker.targets.append(target);
            *error = applyTargets(host.id, tracker.id, targetClipId);
            return true;
        }

        // Camera clips already over the tracked clip stay as they are; each uncovered stretch of
        // its span gets a camera clip of its own, on a new camera track when there is none.
        drift::Project &project = m_app->m_project;
        const TimeUs spanStart = host.timelineStart;
        const TimeUs spanEnd = host.timelineEnd();
        QList<QPair<TimeUs, TimeUs>> gaps;
        TimeUs cursor = spanStart;
        int cameraTrack = drift::cameraTrackIndex(project.tracks());
        if (cameraTrack >= 0) {
            QList<const Clip *> covering;
            for (const Clip &clip : std::as_const(project.tracks()).at(cameraTrack).clips) {
                if (drift::isCameraClip(clip) && clip.timelineStart < spanEnd && clip.timelineEnd() > spanStart)
                    covering.append(&clip);
            }
            std::sort(covering.begin(), covering.end(),
                      [](const Clip *a, const Clip *b) { return a->timelineStart < b->timelineStart; });
            for (const Clip *clip : covering) {
                if (clip->timelineStart > cursor)
                    gaps.append({cursor, clip->timelineStart});
                cursor = qMax(cursor, clip->timelineEnd());
            }
        }
        if (cursor < spanEnd)
            gaps.append({cursor, spanEnd});
        if (!gaps.isEmpty()) {
            if (cameraTrack < 0) {
                project.ensureTrackIds();
                cameraTrack = drift::insertCameraTrack(project.tracks());
                // The camera goes in above everything, so every stored track index shifts by one.
                if (m_app->m_selectedTrack >= 0)
                    ++m_app->m_selectedTrack;
                for (QPair<int, int> &pair : m_app->m_selection)
                    ++pair.first;
                if (m_app->m_selectedTransitionTrack >= 0)
                    ++m_app->m_selectedTransitionTrack;
            }
            for (const auto &[from, to] : gaps)
                project.tracks()[cameraTrack].clips.append(drift::makeCameraClip(from, to - from));
        }

        const TimeUs refTimeline = referenceTimelineUs(host, tracker);
        QStringList cameraIds;
        for (const Clip &clip : std::as_const(project.tracks()).at(cameraTrack).clips) {
            if (!drift::isCameraClip(clip) || clip.timelineStart >= spanEnd || clip.timelineEnd() <= spanStart)
                continue;
            const TimeUs at = clip.timelineStart + std::clamp<TimeUs>(refTimeline - clip.timelineStart, 0, clip.timelineDuration);
            const drift::SceneCamera3d camera = drift::sceneCameraFromClip(clip, at, 1.0);
            drift::TrackerTarget cameraTarget = target;
            cameraTarget.clipId = clip.id;
            cameraTarget.restCamera = {camera.positionX, camera.positionY, camera.positionZ,
                                       camera.rotationX, camera.rotationY, camera.rotationZ};
            tracker.targets.append(cameraTarget);
            cameraIds.append(clip.id);
        }
        for (const QString &cameraId : cameraIds) {
            const QString warning = applyTargets(host.id, tracker.id, cameraId);
            if (error->isEmpty())
                *error = warning;
        }
        return true;
    }
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
    *error = applyTargets(host.id, tracker.id, targetClipId);
    return true;
}

// Sets a stabilize target's zoom and regenerates its keys; the old keys go first because which
// channels it writes depends on the zoom.
bool TrackingController::zoomTarget(Clip &host, MotionTracker &tracker, const QString &targetClipId, double zoom,
                                    QString *error)
{
    zoom = std::clamp(zoom, 1.0, kMaxStabilizeZoom);
    const auto named = std::any_of(tracker.targets.begin(), tracker.targets.end(), [&](const drift::TrackerTarget &target) {
        return target.clipId == targetClipId && target.mode == drift::TrackerTarget::Mode::Stabilize
               && target.zoom != zoom;
    });
    if (!named)
        return false;
    // A camera lock is one target per camera clip over the tracked clip; they zoom together.
    QStringList changed;
    for (drift::TrackerTarget &target : tracker.targets) {
        if (target.mode != drift::TrackerTarget::Mode::Stabilize || target.zoom == zoom)
            continue;
        unapplyTarget(host, tracker, target);
        target.zoom = zoom;
        changed.append(target.clipId);
    }
    for (const QString &id : changed) {
        const QString warning = applyTargets(host.id, tracker.id, id);
        if (error->isEmpty())
            *error = warning;
    }
    return true;
}

// Re-applies every tracker of the host but `except`: a clip-mode stabilize moves the host, and
// follow and on-canvas mask targets read where it is.
void TrackingController::reapplyHost(const QString &hostId, const QString &except)
{
    if (m_reapplying)
        return;
    int t = -1;
    int c = -1;
    if (!findClipById(m_app->m_project, hostId, &t, &c))
        return;
    QStringList ids;
    for (const MotionTracker &tracker : m_app->m_project.tracks().at(t).clips.at(c).motionTrackers) {
        if (tracker.id != except)
            ids.append(tracker.id);
    }
    m_reapplying = true;
    for (const QString &id : ids)
        applyTargets(hostId, id);
    m_reapplying = false;
}

void TrackingController::setTarget(int trackIndex, int clipIndex, const QString &id, const QString &targetClipId,
                                   const QString &mode)
{
    QString error;
    const bool ok = editTracker(trackIndex, clipIndex, id, tr("Apply track"), [&](Clip &host, MotionTracker &tracker) {
        return addTarget(host, tracker, targetClipId, mode, &error);
    });
    if (ok && !error.isEmpty())
        m_app->setLastMessage(error, QStringLiteral("warning"));
}

void TrackingController::removeTarget(int trackIndex, int clipIndex, const QString &id, const QString &targetClipId)
{
    editTracker(trackIndex, clipIndex, id, tr("Remove tracking target"), [&](Clip &host, MotionTracker &tracker) {
        for (int i = 0; i < tracker.targets.size(); ++i) {
            if (tracker.targets.at(i).clipId == targetClipId) {
                const drift::TrackerTarget removed = tracker.targets.at(i);
                unapplyTarget(host, tracker, removed);
                tracker.targets.removeAt(i);
                if (removed.mode == drift::TrackerTarget::Mode::Stabilize && removed.via == drift::TrackerTarget::Via::Camera) {
                    // The rest of the lock's camera clips go with it.
                    for (int j = tracker.targets.size() - 1; j >= 0; --j) {
                        if (tracker.targets.at(j).mode == drift::TrackerTarget::Mode::Stabilize) {
                            unapplyTarget(host, tracker, tracker.targets.at(j));
                            tracker.targets.removeAt(j);
                        }
                    }
                }
                if (removed.mode == drift::TrackerTarget::Mode::Stabilize && removed.via == drift::TrackerTarget::Via::Clip)
                    reapplyHost(host.id, {});
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
    const bool stabilize = target.mode == drift::TrackerTarget::Mode::Stabilize;

    const TimeUs refTimeline = referenceTimelineUs(host, tracker);
    const QSizeF px = frameSizeAt(project, host, refTimeline);
    const QVector<FrameXform> frames = frameTransforms(project, host, tracker, px);
    const QVector<QVector<int>> runs = keyableRuns(frames, owner);
    if (runs.isEmpty())
        return;
    const TimeUs lo = frames[runs.first().first()].timelineUs - owner.timelineStart;
    const TimeUs hi = frames[runs.last().last()].timelineUs - owner.timelineStart;
    const TimeUs restT = qMax<TimeUs>(0, refTimeline - owner.timelineStart);

    if (stabilize && target.via == drift::TrackerTarget::Via::Camera) {
        if (target.restCamera.size() < 6)
            return;
        const CameraChannels channels = cameraChannels(tracker, target);
        const auto stripCamera = [&](drift::KeyframeTrack<double> &track, int index) {
            double rest = 0;
            if (!stripChannel(track, lo, hi, restT, &rest))
                track.setKeyframe(0, target.restCamera.at(index));
        };
        stripCamera(owner.transformX, 0);
        stripCamera(owner.transformY, 1);
        if (channels.distance)
            stripCamera(owner.positionZ, 2);
        if (channels.tilt) {
            stripCamera(owner.rotationX, 3);
            stripCamera(owner.rotationY, 4);
        }
        if (channels.roll)
            stripCamera(owner.rotation, 5);
        return;
    }

    const bool planar = tracker.kind == MotionTracker::Kind::Planar;
    const bool writesRotation = planar || tracker.solve != MotionTracker::Solve::Translate;
    const bool writesScale = planar || tracker.solve == MotionTracker::Solve::TranslateRotateScale
                             || tracker.solve == MotionTracker::Solve::Perspective;

    if (target.mode == drift::TrackerTarget::Mode::Mask) {
        if (!isMaskTargetOwner(project, t, owner))
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
    if (writesScale || (stabilize && target.zoom != 1.0)) {
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
            if (target.clipId != targetClipId || target.mode == drift::TrackerTarget::Mode::Mask
                || target.centerOnTrack == center)
                continue;
            target.centerOnTrack = center;
            if (target.mode == drift::TrackerTarget::Mode::Stabilize) {
                // The camera clips of one lock move together.
                QStringList ids;
                for (drift::TrackerTarget &other : tracker.targets) {
                    if (other.mode == drift::TrackerTarget::Mode::Stabilize) {
                        other.centerOnTrack = center;
                        ids.append(other.clipId);
                    }
                }
                for (const QString &id : ids) {
                    const QString warning = applyTargets(host.id, tracker.id, id);
                    if (error.isEmpty())
                        error = warning;
                }
                return true;
            }
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

QString TrackingController::applyTargets(const QString &clipId, const QString &trackerId, const QString &onlyTargetId)
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
    if (!anyFrame) {
        // A tracker that holds only its placement key has not been run yet; attaching to it is
        // not a failure, the keys appear when it is tracked.
        qsizetype samples = tracker->homographies.size();
        for (const drift::TrackPoint &point : tracker->points)
            samples += point.samples.size();
        return samples > 1 ? tr("Nothing is tracked yet") : QString();
    }

    const bool planar = tracker->kind == MotionTracker::Kind::Planar;
    const MotionTracker::Solve solve = tracker->solve;
    const bool writesRotation = planar || solve != MotionTracker::Solve::Translate;
    const bool writesScale = planar || solve == MotionTracker::Solve::TranslateRotateScale
                             || solve == MotionTracker::Solve::Perspective;
    QString firstError;

    // Moving the host changes what the other targets read, so a clip-mode stabilize always
    // brings the rest of the tracker's targets along.
    QString onlyTarget = onlyTargetId;
    for (const drift::TrackerTarget &target : tracker->targets) {
        if (target.mode == drift::TrackerTarget::Mode::Stabilize && target.via == drift::TrackerTarget::Via::Clip
            && target.clipId == onlyTarget)
            onlyTarget.clear();
    }

    bool movedHost = false;
    for (const drift::TrackerTarget &target : tracker->targets) {
        if (target.mode != drift::TrackerTarget::Mode::Stabilize || (!onlyTarget.isEmpty() && target.clipId != onlyTarget))
            continue;
        if (host.layer3d) {
            if (firstError.isEmpty())
                firstError = tr("Lock to screen is not available for 3D clips");
            continue;
        }
        if (target.via == drift::TrackerTarget::Via::Camera) {
            int ct = -1;
            int cc = -1;
            if (!findClipById(project, target.clipId, &ct, &cc))
                continue;
            const QString warning = writeCameraKeys(project, host, *tracker, target, project.tracks()[ct].clips[cc]);
            if (firstError.isEmpty())
                firstError = warning;
            continue;
        }
        const StabilizeSetup setup = stabilizeSetup(project, host, *tracker, target, refTimeline);
        const QVector<FrameXform> stabilizeFrames = frameTransforms(project, host, *tracker, setup.px);
        Clip &owner = project.tracks()[hostTrack].clips[hostIndex];
        const QVector<QVector<int>> runs = keyableRuns(stabilizeFrames, owner);
        if (runs.isEmpty())
            continue;
        QVector<PoseRun> poseRuns;
        for (const QVector<int> &run : runs) {
            PoseRun poses;
            for (const int i : run) {
                Pose pose = stabilizedPose(setup, stabilizeFrames[i].h, target.zoom, writesRotation);
                pose.t = stabilizeFrames[i].timelineUs - owner.timelineStart;
                poses.append(pose);
            }
            unwrapRotation(&poses);
            poseRuns.append(poses);
        }
        writeChannel(owner.transformX, poseRuns, &Pose::x, kScalarTolerancePx);
        writeChannel(owner.transformY, poseRuns, &Pose::y, kScalarTolerancePx);
        if (writesRotation) {
            // A locked shot shows rotation error as drift at the frame's edge, so bound it in pixels there.
            double radius = 1.0;
            for (const PoseRun &poses : poseRuns) {
                for (const Pose &pose : poses)
                    radius = qMax(radius, std::hypot(pose.w, pose.h) / 2.0);
            }
            writeChannel(owner.rotation, poseRuns, &Pose::rotation, qRadiansToDegrees(kScalarTolerancePx / radius));
        }
        if (writesScale || target.zoom != 1.0) {
            writeChannel(owner.transformW, poseRuns, &Pose::w, kScalarTolerancePx);
            writeChannel(owner.transformH, poseRuns, &Pose::h, kScalarTolerancePx);
        }
        movedHost = true;
    }

    for (const drift::TrackerTarget &target : tracker->targets) {
        if (target.mode == drift::TrackerTarget::Mode::Stabilize || (!onlyTarget.isEmpty() && target.clipId != onlyTarget))
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
            if (!isMaskTargetOwner(project, t, owner)) {
                if (firstError.isEmpty())
                    firstError = tr("%1 is not a mask").arg(owner.name);
                continue;
            }
            // A mask on a standalone effects layer is normalized to the canvas; one pinned to the clip,
            // to the clip's frame, which is where the tracker works.
            const bool onCanvas = owner.adjustmentKind == drift::AdjustmentKind::VideoEffects;
            const QSizeF space = onCanvas ? QSizeF(project.width(), project.height()) : px;
            const HostFrame hostRef = hostFrameAt(project, host, refTimeline);
            if (onCanvas && host.layer3d && firstError.isEmpty())
                firstError = tr("This clip is transformed; the adjustment mask may not line up");
            drift::Mask &mask = owner.mask;
            const drift::Mask rest = mask.resolvedAt(refTimeline - owner.timelineStart);
            const bool parametric = !planar && solve != MotionTracker::Solve::Perspective
                                    && rest.shape != drift::MaskShape::Freeform
                                    && rest.shape != drift::MaskShape::Bars && rest.shape != drift::MaskShape::None;
            if (parametric) {
                QVector<PoseRun> poseRuns;
                const QPointF centre(rest.x * space.width(), rest.y * space.height());
                for (const QVector<int> &run : runs) {
                    PoseRun poses;
                    for (const int i : run) {
                        const Homography &h = frames[i].h;
                        Pose pose;
                        pose.t = frames[i].timelineUs - owner.timelineStart;
                        double turn = qRadiansToDegrees(std::atan2(h[3], h[0]));
                        double scale = std::hypot(h[0], h[3]);
                        QPointF moved;
                        if (onCanvas) {
                            const HostFrame now = hostFrameAt(project, host, frames[i].timelineUs);
                            moved = carryThroughHost(hostRef, now, h, px, centre);
                            const QPointF e = carryThroughHost(hostRef, now, h, px, centre + QPointF(1.0, 0.0)) - moved;
                            turn = qRadiansToDegrees(std::atan2(e.y(), e.x()));
                            scale = std::hypot(e.x(), e.y());
                        } else {
                            moved = drift::tracking::applyHomography(h, centre);
                        }
                        pose.x = moved.x() / space.width();
                        pose.y = moved.y() / space.height();
                        pose.rotation = rest.rotation + (writesRotation ? turn : 0.0);
                        if (!writesScale)
                            scale = 1.0;
                        pose.w = rest.w * scale;
                        pose.h = rest.h * scale;
                        poses.append(pose);
                    }
                    unwrapRotation(&poses);
                    poseRuns.append(poses);
                }
                writeChannel(mask.keyframes[QStringLiteral("x")], poseRuns, &Pose::x, kScalarTolerancePx / space.width());
                writeChannel(mask.keyframes[QStringLiteral("y")], poseRuns, &Pose::y, kScalarTolerancePx / space.height());
                if (writesRotation)
                    writeChannel(mask.keyframes[QStringLiteral("rotation")], poseRuns, &Pose::rotation,
                                 kRotationToleranceDeg);
                if (writesScale) {
                    writeChannel(mask.keyframes[QStringLiteral("w")], poseRuns, &Pose::w, kScalarTolerancePx / space.width());
                    writeChannel(mask.keyframes[QStringLiteral("h")], poseRuns, &Pose::h, kScalarTolerancePx / space.height());
                }
                continue;
            }

            QString error;
            const QVector<QPointF> vertices = restVertices(rest, space, &error);
            if (vertices.isEmpty()) {
                if (firstError.isEmpty())
                    firstError = error;
                continue;
            }
            if (mask.shape != drift::MaskShape::Freeform) {
                mask.shape = drift::MaskShape::Freeform;
                mask.points.clear();
                for (const QPointF &v : vertices)
                    mask.points.append(QPointF(v.x() / space.width(), v.y() / space.height()));
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
                    const HostFrame now = onCanvas ? hostFrameAt(project, host, frames[i].timelineUs) : HostFrame();
                    for (const QPointF &v : vertices) {
                        const QPointF moved = onCanvas ? carryThroughHost(hostRef, now, frames[i].h, px, v)
                                                       : drift::tracking::applyHomography(frames[i].h, v);
                        pose.points.append(QPointF(moved.x() / space.width(), moved.y() / space.height()));
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
                        worst = qMax(worst, std::hypot((poses[k].points[v].x() - expected.x()) * space.width(),
                                                       (poses[k].points[v].y() - expected.y()) * space.height()));
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
    if (movedHost)
        reapplyHost(host.id, trackerId);
    return firstError;
}
