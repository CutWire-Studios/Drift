#include "core/edit/TimelineOps.h"

#include <limits>

#include "core/edit/MaskSpace.h"

#include <QUuid>
#include <QtMath>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <optional>

namespace drift {

namespace {
std::atomic<TimeUs> g_imageClipDurationUs{5 * kUsPerSecond};
} // namespace

TimeUs imageClipDurationUs()
{
    return g_imageClipDurationUs.load(std::memory_order_relaxed);
}

void setImageClipDurationUs(TimeUs duration)
{
    g_imageClipDurationUs.store(duration, std::memory_order_relaxed);
}

void SnapTargets::build(const Project &project, TimeUs playheadUs,
                        const QList<TimeUs> &extraTargets, const QString &excludeClipId)
{
    sorted.clear();
    sorted.reserve(2 + extraTargets.size() + 2 * project.tracks().size());
    sorted.push_back(0);
    sorted.push_back(playheadUs);
    for (const Track &track : project.tracks()) {
        for (const Clip &clip : track.clips) {
            if (!excludeClipId.isEmpty() && clip.id == excludeClipId)
                continue;
            sorted.push_back(clip.timelineStart);
            sorted.push_back(clip.timelineEnd());
        }
    }
    for (TimeUs extra : extraTargets)
        sorted.push_back(extra);

    std::sort(sorted.begin(), sorted.end());
    sorted.erase(std::unique(sorted.begin(), sorted.end()), sorted.end());
}

TimeUs snapTimeTo(const SnapTargets &targets, TimeUs time, bool snapEnabled, TimeUs thresholdUs)
{
    if (!snapEnabled || targets.sorted.empty())
        return qMax<TimeUs>(0, time);

    // Only the two targets bracketing `time` can be the nearest one. Ties go to the lower
    // target; the old linear scan gave them to whichever clip came first in track order, which
    // was no more meaningful and only reachable at exact microsecond equidistance.
    const auto it = std::lower_bound(targets.sorted.begin(), targets.sorted.end(), time);
    TimeUs best = time;
    TimeUs bestDistance = thresholdUs;
    if (it != targets.sorted.begin()) {
        const TimeUs candidate = *(it - 1);
        const TimeUs distance = qAbs(candidate - time);
        if (distance < bestDistance) {
            bestDistance = distance;
            best = candidate;
        }
    }
    if (it != targets.sorted.end()) {
        const TimeUs candidate = *it;
        const TimeUs distance = qAbs(candidate - time);
        if (distance < bestDistance) {
            bestDistance = distance;
            best = candidate;
        }
    }

    return qMax<TimeUs>(0, best);
}

TimeUs snapTime(const Project &project, TimeUs time, bool snapEnabled, TimeUs playheadUs,
                const QList<TimeUs> &extraTargets, const QString &excludeClipId, TimeUs thresholdUs)
{
    if (!snapEnabled)
        return qMax<TimeUs>(0, time);

    SnapTargets targets;
    targets.build(project, playheadUs, extraTargets, excludeClipId);
    return snapTimeTo(targets, time, snapEnabled, thresholdUs);
}

TimeUs resolveClipStart(const Project &project, const Track &track, int excludeClipIndex,
                        TimeUs desiredStart, TimeUs duration, bool snapEnabled, TimeUs playheadUs,
                        const QList<TimeUs> &extraTargets)
{
    TimeUs start = snapTime(project, desiredStart, snapEnabled, playheadUs, extraTargets);

    struct Interval {
        TimeUs begin;
        TimeUs end;
    };
    QList<Interval> intervals;
    intervals.reserve(track.clips.size());

    for (int i = 0; i < track.clips.size(); ++i) {
        if (i == excludeClipIndex)
            continue;
        const Clip &clip = track.clips.at(i);
        intervals.append({clip.timelineStart, clip.timelineEnd()});
    }

    std::sort(intervals.begin(), intervals.end(),
              [](const Interval &a, const Interval &b) { return a.begin < b.begin; });

    bool adjusted = true;
    while (adjusted) {
        adjusted = false;
        for (const Interval &interval : intervals) {
            if (start < interval.end && start + duration > interval.begin) {
                start = interval.end;
                adjusted = true;
            }
        }
    }

    return qMax<TimeUs>(0, start);
}

TimeUs clampClipStartNoOverlap(const Track &track, const QSet<QString> &excludeIds,
                               TimeUs desiredStart, TimeUs duration)
{
    TimeUs start = qMax<TimeUs>(0, desiredStart);

    struct Interval {
        TimeUs begin;
        TimeUs end;
    };
    QList<Interval> intervals;
    intervals.reserve(track.clips.size());
    for (const Clip &clip : track.clips) {
        if (excludeIds.contains(clip.id))
            continue;
        intervals.append({clip.timelineStart, clip.timelineEnd()});
    }
    std::sort(intervals.begin(), intervals.end(),
              [](const Interval &a, const Interval &b) { return a.begin < b.begin; });

    bool adjusted = true;
    while (adjusted) {
        adjusted = false;
        for (const Interval &interval : intervals) {
            if (start < interval.end && start + duration > interval.begin) {
                start = interval.end;
                adjusted = true;
            }
        }
    }

    return start;
}

TimeUs placeClipStartNoOverlap(const Track &track, const QSet<QString> &excludeIds,
                               TimeUs desiredStart, TimeUs duration)
{
    const TimeUs start = qMax<TimeUs>(0, desiredStart);
    TimeUs firstBlocker = -1;
    for (const Clip &clip : track.clips) {
        if (excludeIds.contains(clip.id))
            continue;
        if (start < clip.timelineEnd() && start + duration > clip.timelineStart
            && (firstBlocker < 0 || clip.timelineStart < firstBlocker))
            firstBlocker = clip.timelineStart;
    }
    if (firstBlocker < 0)
        return start;
    if (start >= firstBlocker)
        return clampClipStartNoOverlap(track, excludeIds, start, duration);

    const TimeUs left = firstBlocker - duration;
    if (left < 0 || clampClipStartNoOverlap(track, excludeIds, left, duration) != left)
        return -1;
    return left;
}

std::optional<TimeUs> groupShiftNoOverlap(const QList<PlacedSpan> &spans,
                                          const QSet<QString> &excludeIds)
{
    TimeUs mostLeft = 0;
    TimeUs mostRight = 0;
    for (const PlacedSpan &span : spans) {
        const TimeUs placed = placeClipStartNoOverlap(*span.track, excludeIds, span.start, span.duration);
        if (placed < 0)
            return std::nullopt;
        const TimeUs shift = placed - span.start;
        mostLeft = qMin(mostLeft, shift);
        mostRight = qMax(mostRight, shift);
    }
    if (mostLeft < 0 && mostRight > 0)
        return std::nullopt;
    if (mostLeft == 0)
        return mostRight;

    for (const PlacedSpan &span : spans) {
        const TimeUs start = span.start + mostLeft;
        if (start < 0 || clampClipStartNoOverlap(*span.track, excludeIds, start, span.duration) != start)
            return std::nullopt;
    }
    return mostLeft;
}

TimeUs clampClipStartAgainstLeftNeighbors(const Track &track, const QSet<QString> &excludeIds,
                                          TimeUs currentStart, TimeUs desiredStart)
{
    TimeUs start = qMax<TimeUs>(0, desiredStart);
    TimeUs minStart = 0;
    for (const Clip &clip : track.clips) {
        if (excludeIds.contains(clip.id))
            continue;
        // Only blockers that sit fully to the left of the current edge (gap or abut).
        if (clip.timelineEnd() <= currentStart)
            minStart = qMax(minStart, clip.timelineEnd());
    }
    return qMax(start, minStart);
}

TimeUs clampClipEndNoOverlap(const Track &track, const QSet<QString> &excludeIds, TimeUs currentEnd,
                             TimeUs desiredEnd)
{
    TimeUs end = qMax(currentEnd, desiredEnd);
    for (const Clip &clip : track.clips) {
        if (excludeIds.contains(clip.id))
            continue;
        // Only blockers that sit fully to the right of the current edge (gap or abut).
        if (clip.timelineStart < currentEnd)
            continue;
        if (end > clip.timelineStart)
            end = clip.timelineStart;
    }
    return end;
}

TrackType trackTypeForClipType(ClipType type)
{
    switch (type) {
    case ClipType::Audio:
        return TrackType::Audio;
    case ClipType::Text:
        return TrackType::Text;
    case ClipType::Subtitle:
        return TrackType::Subtitle;
    case ClipType::Image:
    case ClipType::Shape:
    case ClipType::Vector:
    case ClipType::Model3d:
    case ClipType::Pdf:
    case ClipType::Procedural:
        return TrackType::Shape;
    case ClipType::Adjustment:
        return TrackType::Adjustment;
    case ClipType::Video:
    case ClipType::Composite:
        break;
    }
    return TrackType::Video;
}

int defaultTrackForClipType(const Project &project, ClipType type)
{
    const TrackType trackType = trackTypeForClipType(type);
    const QList<Track> &tracks = project.tracks();
    for (int i = 0; i < tracks.size(); ++i) {
        // A nested lane is scoped to somebody else's track; dropping a free-standing adjustment
        // into one would silently change what it applies to.
        if (tracks[i].isAdjustmentLane())
            continue;
        if (tracks[i].type == trackType && tracks[i].allowsClipType(type))
            return i;
    }
    return -1;
}

QList<int> adjustmentLaneIndexes(const Project &project, int parentIndex)
{
    const QList<Track> &tracks = project.tracks();
    if (parentIndex < 0 || parentIndex >= tracks.size())
        return {};
    const QString parentId = tracks.at(parentIndex).id;
    if (parentId.isEmpty())
        return {};

    QList<int> result;
    for (int i = 0; i < tracks.size(); ++i) {
        if (tracks.at(i).isAdjustmentLane() && tracks.at(i).parentTrackId == parentId)
            result.append(i);
    }
    return result;
}

int adjustmentLaneParentIndex(const Project &project, int laneIndex)
{
    const QList<Track> &tracks = project.tracks();
    if (laneIndex < 0 || laneIndex >= tracks.size() || !tracks.at(laneIndex).isAdjustmentLane())
        return -1;
    return project.trackIndexById(tracks.at(laneIndex).parentTrackId);
}

int ensureAdjustmentLane(Project &project, int parentIndex, AdjustmentKind kind, TimeUs startUs,
                         TimeUs durationUs, bool visibleOnly)
{
    if (parentIndex < 0 || parentIndex >= project.tracks().size())
        return -1;
    // A lane addresses its parent by id, so the parent needs one before it can be pointed at.
    project.ensureTrackIds();

    // Lanes nest in the tracks that carry clips. Nesting one inside another lane would give it
    // two scopes at once.
    if (project.tracks().at(parentIndex).isAdjustment())
        return -1;

    project.tracks()[parentIndex].lanesCollapsed = false;

    for (const int laneIndex : adjustmentLaneIndexes(project, parentIndex)) {
        const Track &lane = project.tracks().at(laneIndex);
        if (visibleOnly && lane.hidden)
            continue;
        if (!lane.clips.isEmpty() && lane.clips.first().adjustmentKind != kind)
            continue;
        bool collides = false;
        for (const Clip &existing : lane.clips) {
            if (startUs < existing.timelineEnd() && existing.timelineStart < startUs + durationUs) {
                collides = true;
                break;
            }
        }
        if (!collides)
            return laneIndex;
    }

    // No room anywhere: a fresh lane just after the parent's existing ones, so those keep applying
    // in the order they did.
    //
    // Stored *below* the parent, not above. A lane has no z-position of its own — it is drawn
    // inside the parent's row either way — so the only thing array position decides is whose
    // indices shift when one is created. Below leaves the parent and everything above it alone,
    // which matters because adding an effect creates a lane, and the caller is usually holding
    // the index of the very track it is editing.
    Track lane;
    lane.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    lane.type = TrackType::Adjustment;
    lane.adjustmentScope = AdjustmentScope::ParentTrack;
    lane.parentTrackId = project.tracks().at(parentIndex).id;

    const QList<int> existing = adjustmentLaneIndexes(project, parentIndex);
    const int insertAt = existing.isEmpty() ? parentIndex + 1 : existing.constLast() + 1;
    project.tracks().insert(insertAt, lane);
    return insertAt;
}

namespace {

// One Mask-kind adjustment clip. An empty `linkedClipId` leaves it free-standing on its lane with
// draggable edges; set, it is pinned and syncLinkedAdjustments mirrors the host clip's span onto
// it through every move/trim/split/delete, so the mask cannot drift off the shot it was made for.
Clip makeMaskAdjustment(const Mask &mask, const QString &linkedClipId, TimeUs startUs,
                        TimeUs durationUs)
{
    Clip adjustment;
    adjustment.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    adjustment.type = ClipType::Adjustment;
    adjustment.adjustmentKind = AdjustmentKind::Mask;
    adjustment.linkedClipId = linkedClipId;
    adjustment.timelineStart = startUs;
    adjustment.timelineDuration = durationUs;
    adjustment.srcIn = 0;
    adjustment.srcOut = durationUs;
    adjustment.mask = mask;
    return adjustment;
}

// Drop `adjustment` on a lane of `parentIndex` with room for it, minting one if need be. A null
// ClipRef when the parent cannot take a lane.
ClipRef appendToMaskLane(Project &project, int parentIndex, const Clip &adjustment)
{
    const int laneIndex = ensureAdjustmentLane(project, parentIndex, AdjustmentKind::Mask,
                                               adjustment.timelineStart,
                                               adjustment.timelineDuration);
    if (laneIndex < 0)
        return {};
    project.tracks()[laneIndex].clips.append(adjustment);
    return ClipRef{laneIndex, static_cast<int>(project.tracks().at(laneIndex).clips.size()) - 1};
}

} // namespace

QList<LaneMask> laneMasksAt(const Project &project, int trackIndex, TimeUs timelineUs,
                            const QString &hostClipId)
{
    QList<LaneMask> result;
    for (const int laneIndex : adjustmentLaneIndexes(project, trackIndex)) {
        const Track &lane = project.tracks().at(laneIndex);
        if (lane.hidden)
            continue;
        for (const Clip &adjustment : lane.clips) {
            if (adjustment.adjustmentKind != AdjustmentKind::Mask)
                continue;
            if (!adjustment.containsTime(timelineUs))
                continue;
            if (!adjustment.linkedClipId.isEmpty() && adjustment.linkedClipId != hostClipId)
                continue;
            if (!adjustment.mask.contributes())
                continue;
            // Bake animated properties down to this frame the way resolvedClipEffects does for
            // effects, so the rasterizer and the GPU fold only ever see plain numbers and preview
            // cannot diverge from export. Keys are relative to the adjustment's own start.
            const Mask &mask = adjustment.mask;
            result.append(LaneMask{
                mask.isAnimated() ? mask.resolvedAt(timelineUs - adjustment.timelineStart) : mask,
                adjustment.id, adjustment.timelineStart, adjustment.srcIn});
        }
    }
    std::stable_partition(result.begin(), result.end(),
                          [](const LaneMask &m) { return m.mask.cropShape.isEmpty(); });
    return result;
}

QList<ClipRef> linkedMaskAdjustments(const Project &project, int trackIndex, int clipIndex)
{
    if (trackIndex < 0 || trackIndex >= project.tracks().size())
        return {};
    const Track &track = project.tracks().at(trackIndex);
    if (clipIndex < 0 || clipIndex >= track.clips.size())
        return {};
    const QString clipId = track.clips.at(clipIndex).id;

    QList<ClipRef> result;
    for (const int laneIndex : adjustmentLaneIndexes(project, trackIndex)) {
        const Track &lane = project.tracks().at(laneIndex);
        for (int c = 0; c < lane.clips.size(); ++c) {
            const Clip &adjustment = lane.clips.at(c);
            if (adjustment.adjustmentKind == AdjustmentKind::Mask
                && adjustment.linkedClipId == clipId) {
                result.append(ClipRef{laneIndex, c});
            }
        }
    }
    return result;
}

void setLinkedMask(Project &project, int trackIndex, int clipIndex, const Mask &mask)
{
    if (trackIndex < 0 || trackIndex >= project.tracks().size())
        return;
    if (clipIndex < 0 || clipIndex >= project.tracks().at(trackIndex).clips.size())
        return;

    const QList<ClipRef> existing = linkedMaskAdjustments(project, trackIndex, clipIndex);
    if (!existing.isEmpty()) {
        // Write into the first one and drop the rest, so repeated edits do not stack up rows.
        // Removals go back-to-front: each takeAt shifts the indices after it.
        for (int i = existing.size() - 1; i >= 1; --i)
            project.tracks()[existing.at(i).trackIndex].clips.removeAt(existing.at(i).clipIndex);

        const ClipRef &first = existing.constFirst();
        if (mask.contributes()) {
            project.tracks()[first.trackIndex].clips[first.clipIndex].mask = mask;
            return;
        }
        project.tracks()[first.trackIndex].clips.removeAt(first.clipIndex);
        return;
    }
    if (!mask.contributes())
        return;

    const Clip source = project.tracks().at(trackIndex).clips.at(clipIndex);
    appendToMaskLane(project, trackIndex,
                     makeMaskAdjustment(mask, source.id, source.timelineStart,
                                        source.timelineDuration));
}

QStringList clipShapeIds()
{
    return {QStringLiteral("original"), QStringLiteral("circle"),   QStringLiteral("ellipse"),
            QStringLiteral("triangle"), QStringLiteral("square"),   QStringLiteral("diamond"),
            QStringLiteral("pentagon"), QStringLiteral("hexagon"),  QStringLiteral("octagon"),
            QStringLiteral("star"),     QStringLiteral("heart")};
}

Mask clipShapeMask(const QString &id, double hostW, double hostH)
{
    Mask mask;
    mask.op = MaskOp::Intersect;
    mask.cropShape = id;
    hostW = qMax(1.0, hostW);
    hostH = qMax(1.0, hostH);
    const double side = qMin(hostW, hostH);
    const double w = side / hostW;
    const double h = side / hostH;

    const auto polygon = [&](int sides, double rotationDeg) {
        mask.shape = MaskShape::Freeform;
        const double start = qDegreesToRadians(rotationDeg - 90.0);
        for (int i = 0; i < sides; ++i) {
            const double a = start + i * 2.0 * M_PI / sides;
            mask.points.append(QPointF(0.5 + 0.5 * w * std::cos(a), 0.5 + 0.5 * h * std::sin(a)));
        }
    };

    if (id == QLatin1String("ellipse")) {
        mask.shape = MaskShape::Ellipse;
        mask.w = mask.h = 1.0;
    } else if (id == QLatin1String("circle")) {
        mask.shape = MaskShape::Ellipse;
        mask.w = w;
        mask.h = h;
    } else if (id == QLatin1String("square")) {
        mask.shape = MaskShape::Rectangle;
        mask.w = w;
        mask.h = h;
    } else if (id == QLatin1String("heart")) {
        mask.shape = MaskShape::Heart;
        mask.w = w;
        mask.h = h;
    } else if (id == QLatin1String("triangle")) {
        polygon(3, 0.0);
    } else if (id == QLatin1String("diamond")) {
        polygon(4, 0.0);
    } else if (id == QLatin1String("pentagon")) {
        polygon(5, 0.0);
    } else if (id == QLatin1String("hexagon")) {
        polygon(6, 0.0);
    } else if (id == QLatin1String("octagon")) {
        polygon(8, 22.5);
    } else if (id == QLatin1String("star")) {
        mask.shape = MaskShape::Freeform;
        for (int i = 0; i < 10; ++i) {
            const double a = qDegreesToRadians(-90.0 + i * 36.0);
            const double r = i % 2 == 0 ? 1.0 : 0.4;
            mask.points.append(QPointF(0.5 + 0.5 * w * r * std::cos(a), 0.5 + 0.5 * h * r * std::sin(a)));
        }
    }
    return mask;
}

QString linkedClipShape(const Project &project, int trackIndex, int clipIndex)
{
    for (const ClipRef &ref : linkedMaskAdjustments(project, trackIndex, clipIndex)) {
        const QString &id = project.tracks().at(ref.trackIndex).clips.at(ref.clipIndex).mask.cropShape;
        if (!id.isEmpty())
            return id;
    }
    return QStringLiteral("original");
}

void setLinkedClipShape(Project &project, int trackIndex, int clipIndex, const QString &id)
{
    if (trackIndex < 0 || trackIndex >= project.tracks().size())
        return;
    if (clipIndex < 0 || clipIndex >= project.tracks().at(trackIndex).clips.size())
        return;

    project.ensureTrackIds();
    const Clip host = project.tracks().at(trackIndex).clips.at(clipIndex);
    const QString hostTrackId = project.tracks().at(trackIndex).id;

    QStringList emptiedLaneIds;
    const QList<ClipRef> existing = linkedMaskAdjustments(project, trackIndex, clipIndex);
    for (int i = existing.size() - 1; i >= 0; --i) {
        const ClipRef &ref = existing.at(i);
        if (project.tracks().at(ref.trackIndex).clips.at(ref.clipIndex).mask.cropShape.isEmpty())
            continue;
        project.tracks()[ref.trackIndex].clips.removeAt(ref.clipIndex);
        if (project.tracks().at(ref.trackIndex).clips.isEmpty())
            emptiedLaneIds.append(project.tracks().at(ref.trackIndex).id);
    }
    for (const QString &laneId : std::as_const(emptiedLaneIds)) {
        const int laneIndex = project.trackIndexById(laneId);
        if (laneIndex >= 0)
            project.tracks().removeAt(laneIndex);
    }

    const HostFrame frame = hostFrameAt(project, host, host.timelineStart);
    const Mask mask = clipShapeMask(id, frame.w, frame.h);
    if (!mask.contributes())
        return;

    const int hostIndex = project.trackIndexById(hostTrackId);
    const int laneIndex = ensureAdjustmentLane(project, hostIndex, AdjustmentKind::Mask, host.timelineStart,
                                               host.timelineDuration, true);
    if (laneIndex < 0)
        return;
    project.tracks()[laneIndex].clips.append(
        makeMaskAdjustment(mask, host.id, host.timelineStart, host.timelineDuration));
}

void moveLinkedAdjustmentsToHostTrack(Project &project, const QString &hostClipId)
{
    project.ensureTrackIds();
    int hostTrack = -1;
    int hostClip = -1;
    for (int t = 0; t < project.tracks().size() && hostTrack < 0; ++t) {
        const Track &track = project.tracks().at(t);
        if (track.isAdjustment())
            continue;
        for (int c = 0; c < track.clips.size(); ++c) {
            if (track.clips.at(c).id == hostClipId) {
                hostTrack = t;
                hostClip = c;
                break;
            }
        }
    }
    if (hostTrack < 0)
        return;

    const Clip host = project.tracks().at(hostTrack).clips.at(hostClip);
    const QString hostTrackId = project.tracks().at(hostTrack).id;

    QList<Clip> moving;
    QStringList touchedLaneIds;
    for (Track &lane : project.tracks()) {
        if (!lane.isAdjustmentLane() || lane.parentTrackId == hostTrackId)
            continue;
        for (int i = lane.clips.size() - 1; i >= 0; --i) {
            if (lane.clips.at(i).linkedClipId != hostClipId
                || lane.clips.at(i).adjustmentKind == AdjustmentKind::Transform)
                continue;
            Clip adjustment = lane.clips.at(i);
            adjustment.timelineStart = host.timelineStart;
            adjustment.timelineDuration = host.timelineDuration;
            lane.clips.removeAt(i);
            moving.prepend(adjustment);
            if (!touchedLaneIds.contains(lane.id))
                touchedLaneIds.append(lane.id);
        }
    }

    for (const Clip &adjustment : std::as_const(moving)) {
        const int laneIndex = ensureAdjustmentLane(project, project.trackIndexById(hostTrackId),
                                                   adjustment.adjustmentKind,
                                                   adjustment.timelineStart,
                                                   adjustment.timelineDuration);
        if (laneIndex >= 0)
            project.tracks()[laneIndex].clips.append(adjustment);
    }

    for (const QString &laneId : std::as_const(touchedLaneIds)) {
        const int laneIndex = project.trackIndexById(laneId);
        if (laneIndex >= 0 && project.tracks().at(laneIndex).clips.isEmpty())
            project.tracks().removeAt(laneIndex);
    }
}

ClipRef addLinkedMask(Project &project, int trackIndex, int clipIndex, const Mask &mask)
{
    if (trackIndex < 0 || trackIndex >= project.tracks().size())
        return {};
    if (clipIndex < 0 || clipIndex >= project.tracks().at(trackIndex).clips.size())
        return {};
    if (!mask.contributes())
        return {};

    const Clip source = project.tracks().at(trackIndex).clips.at(clipIndex);
    return appendToMaskLane(project, trackIndex,
                            makeMaskAdjustment(mask, source.id, source.timelineStart,
                                               source.timelineDuration));
}

ClipRef addLaneMask(Project &project, int trackIndex, const Mask &mask, TimeUs startUs,
                    TimeUs durationUs)
{
    if (trackIndex < 0 || trackIndex >= project.tracks().size())
        return {};
    if (!mask.contributes() || durationUs <= 0)
        return {};

    return appendToMaskLane(project, trackIndex,
                            makeMaskAdjustment(mask, {}, qMax<TimeUs>(0, startUs), durationUs));
}

void clearLinkedMasks(Project &project, int trackIndex, int clipIndex, bool mediaOnly)
{
    const QList<ClipRef> existing = linkedMaskAdjustments(project, trackIndex, clipIndex);
    for (int i = existing.size() - 1; i >= 0; --i) {
        const ClipRef &ref = existing.at(i);
        const Clip &adjustment = project.tracks().at(ref.trackIndex).clips.at(ref.clipIndex);
        if (mediaOnly && adjustment.mask.shape != MaskShape::Media)
            continue;
        project.tracks()[ref.trackIndex].clips.removeAt(ref.clipIndex);
    }
}

void migrateClipMasksToAdjustmentLanes(Project &project)
{
    bool anyMaskOnAClip = false;
    for (const Track &track : project.tracks()) {
        if (track.isAdjustment())
            continue;
        for (const Clip &clip : track.clips) {
            if (clip.type != ClipType::Adjustment && clip.mask.shape != MaskShape::None) {
                anyMaskOnAClip = true;
                break;
            }
        }
        if (anyMaskOnAClip)
            break;
    }
    if (!anyMaskOnAClip)
        return;

    project.ensureTrackIds();

    QList<Track> &tracks = project.tracks();
    // Bottom-to-top: setLinkedMask only ever inserts a lane below `i`, so every index the loop
    // still has to visit stays valid.
    for (int i = tracks.size() - 1; i >= 0; --i) {
        if (tracks.at(i).isAdjustment())
            continue;
        for (int c = 0; c < tracks.at(i).clips.size(); ++c) {
            if (tracks.at(i).clips.at(c).type == ClipType::Adjustment)
                continue;
            const Mask mask = tracks.at(i).clips.at(c).mask;
            if (mask.shape == MaskShape::None)
                continue;
            tracks[i].clips[c].mask = Mask();
            setLinkedMask(project, i, c, mask);
        }
    }
}

namespace {

int trackIndexIn(const QList<Track> &tracks, const QString &id)
{
    if (id.isEmpty())
        return -1;
    for (int i = 0; i < tracks.size(); ++i) {
        if (tracks.at(i).id == id)
            return i;
    }
    return -1;
}

bool isTransformClip(const Clip &clip)
{
    return clip.type == ClipType::Adjustment && clip.adjustmentKind == AdjustmentKind::Transform;
}

// Lanes sit with their parent, so a lane is covered exactly when its parent is.
int coverageIndex(const QList<Track> &tracks, int trackIndex)
{
    const Track &track = tracks.at(trackIndex);
    if (!track.isAdjustmentLane())
        return trackIndex;
    return trackIndexIn(tracks, track.parentTrackId);
}

} // namespace

int transformSpanEndIndex(const QList<Track> &tracks, int layerIndex)
{
    if (layerIndex < 0 || layerIndex >= tracks.size() || !tracks.at(layerIndex).isTransformLayer())
        return -1;
    const int end = trackIndexIn(tracks, tracks.at(layerIndex).spanEndTrackId);
    if (end < 0)
        return -1;
    const int resolved = coverageIndex(tracks, end);
    return resolved > layerIndex ? resolved : -1;
}

bool isTransformableTrack(const Track &track)
{
    return track.type == TrackType::Video || track.type == TrackType::Text
           || track.type == TrackType::Subtitle || track.type == TrackType::Shape;
}

QList<int> transformSpanTrackIndexes(const QList<Track> &tracks, int layerIndex)
{
    QList<int> result;
    const int end = transformSpanEndIndex(tracks, layerIndex);
    for (int i = layerIndex + 1; i <= end; ++i) {
        if (isTransformableTrack(tracks.at(i)))
            result.append(i);
    }
    return result;
}

QList<int> transformLayersCovering(const QList<Track> &tracks, int trackIndex)
{
    if (trackIndex < 0 || trackIndex >= tracks.size())
        return {};
    const int index = coverageIndex(tracks, trackIndex);
    QList<int> result;
    for (int layer = 0; layer < index; ++layer) {
        if (tracks.at(layer).isTransformLayer() && transformSpanEndIndex(tracks, layer) >= index)
            result.append(layer);
    }
    return result;
}

int insertTransformTrack(QList<Track> &tracks, int index, const QString &spanEndTrackId)
{
    Track track;
    track.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    track.type = TrackType::Adjustment;
    track.adjustmentScope = AdjustmentScope::Range;
    track.spanEndTrackId = spanEndTrackId;
    index = qBound(0, index, int(tracks.size()));
    tracks.insert(index, track);
    return index;
}

Clip makeTransformClip(TimeUs startUs, TimeUs durationUs)
{
    Clip clip;
    clip.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    clip.type = ClipType::Adjustment;
    clip.adjustmentKind = AdjustmentKind::Transform;
    clip.name = QStringLiteral("Transform");
    clip.timelineStart = startUs;
    clip.timelineDuration = durationUs;
    clip.srcIn = 0;
    clip.srcOut = durationUs;
    return clip;
}

void normalizeTransformLayers(QList<Track> &tracks, const QList<Track> *before)
{
    // The settled case — no layer and no stray transform clip — must cost one scan.
    bool any = false;
    for (const Track &track : tracks) {
        if (track.isTransformLayer()) {
            any = true;
            break;
        }
        if (track.isAdjustment()) {
            for (const Clip &clip : track.clips)
                any = any || isTransformClip(clip);
        }
        if (any)
            break;
    }
    if (!any)
        return;

    // Pairing. Walk bottom-up so inserting above a track never shifts one still to visit.
    for (int i = tracks.size() - 1; i >= 0; --i) {
        if (tracks.at(i).isTransformLayer()) {
            QList<Clip> strays;
            for (int c = tracks[i].clips.size() - 1; c >= 0; --c) {
                if (!isTransformClip(tracks.at(i).clips.at(c)))
                    strays.prepend(tracks[i].clips.takeAt(c));
            }
            // Directly below the layer, which is inside its span, so a lifted clip keeps moving
            // with the group it sat in.
            for (int c = strays.size() - 1; c >= 0; --c) {
                Track lifted;
                lifted.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
                lifted.type = trackTypeForClipType(strays.at(c).type);
                lifted.clips.append(strays.at(c));
                tracks.insert(i + 1, lifted);
            }
        } else if (tracks.at(i).isAdjustment()) {
            Track lifted;
            for (int c = tracks[i].clips.size() - 1; c >= 0; --c) {
                if (isTransformClip(tracks.at(i).clips.at(c)))
                    lifted.clips.prepend(tracks[i].clips.takeAt(c));
            }
            if (lifted.clips.isEmpty())
                continue;
            // A stray from a lane moves its parent; one from a standalone track moves the next
            // track down, the nearest thing it could have been meant for.
            QString end;
            if (tracks.at(i).isAdjustmentLane()) {
                end = tracks.at(i).parentTrackId;
            } else {
                for (int j = i + 1; j < tracks.size() && end.isEmpty(); ++j) {
                    if (isTransformableTrack(tracks.at(j)))
                        end = tracks.at(j).id;
                }
            }
            const int layer = tracks.at(i).isAdjustmentLane()
                                  ? qMax(0, trackIndexIn(tracks, tracks.at(i).parentTrackId))
                                  : i;
            insertTransformTrack(tracks, layer, end);
            tracks[layer].clips = lifted.clips;
            // Everything from `layer` down moved one; revisit the slot the next track now holds.
            ++i;
        }
    }

    for (Track &track : tracks) {
        if (!track.isTransformLayer())
            continue;
        for (Clip &clip : track.clips)
            clip.linkedClipId.clear();
    }

    for (int i = 0; i < tracks.size(); ++i) {
        Track &layer = tracks[i];
        if (!layer.isTransformLayer() || layer.spanEndTrackId.isEmpty())
            continue;
        const int end = trackIndexIn(tracks, layer.spanEndTrackId);
        if (end >= 0) {
            if (tracks.at(end).isAdjustmentLane())
                layer.spanEndTrackId = tracks.at(end).parentTrackId;
            continue;
        }
        QString survivor;
        if (before) {
            const int oldLayer = trackIndexIn(*before, layer.id);
            const int oldEnd = trackIndexIn(*before, layer.spanEndTrackId);
            for (int j = oldEnd - 1; oldLayer >= 0 && j > oldLayer && survivor.isEmpty(); --j) {
                const Track &candidate = before->at(j);
                if (!candidate.isAdjustmentLane() && trackIndexIn(tracks, candidate.id) >= 0)
                    survivor = candidate.id;
            }
        }
        layer.spanEndTrackId = survivor;
    }
}

int cameraTrackIndex(const QList<Track> &tracks)
{
    for (int i = 0; i < tracks.size(); ++i) {
        if (tracks.at(i).isCameraLayer())
            return i;
    }
    return -1;
}

bool isCameraClip(const Clip &clip)
{
    return clip.type == ClipType::Adjustment && clip.adjustmentKind == AdjustmentKind::Camera;
}

int insertCameraTrack(QList<Track> &tracks)
{
    Track track;
    track.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    track.type = TrackType::Adjustment;
    track.adjustmentScope = AdjustmentScope::Camera;
    tracks.prepend(track);
    return 0;
}

Clip makeCameraClip(TimeUs startUs, TimeUs durationUs)
{
    Clip clip;
    clip.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    clip.type = ClipType::Adjustment;
    clip.adjustmentKind = AdjustmentKind::Camera;
    clip.name = QStringLiteral("Camera");
    clip.timelineStart = startUs;
    clip.timelineDuration = durationUs;
    clip.srcIn = 0;
    clip.srcOut = durationUs;
    // Every transform track is left empty, which evaluates to the camera at rest. A camera clip
    // that has never been touched must therefore render exactly as no camera at all.
    return clip;
}

const Clip *cameraClipAt(const QList<Track> &tracks, TimeUs timelineUs)
{
    const int index = cameraTrackIndex(tracks);
    if (index < 0 || tracks.at(index).hidden)
        return nullptr;
    for (const Clip &clip : tracks.at(index).clips) {
        if (isCameraClip(clip) && clip.containsTime(timelineUs))
            return &clip;
    }
    return nullptr;
}

void normalizeCameraLayers(QList<Track> &tracks)
{
    // Runs after every edit, so the settled case — no camera track and no stray camera clip —
    // must cost one scan and no allocation.
    bool any = false;
    for (const Track &track : tracks) {
        if (track.isCameraLayer()) {
            any = true;
            break;
        }
        for (const Clip &clip : track.clips) {
            if (isCameraClip(clip)) {
                any = true;
                break;
            }
        }
        if (any)
            break;
    }
    if (!any)
        return;

    // One camera per sequence. Extras hand their clips to the first and go; walking downwards
    // keeps the topmost — the one insertCameraTrack made — as the survivor.
    const int keep = cameraTrackIndex(tracks);
    if (keep >= 0) {
        for (int i = tracks.size() - 1; i > keep; --i) {
            if (!tracks.at(i).isCameraLayer())
                continue;
            for (const Clip &clip : tracks.at(i).clips) {
                if (isCameraClip(clip))
                    tracks[keep].clips.append(clip);
            }
            tracks.removeAt(i);
        }
        // A camera covers the whole sequence, so a span or a parent would be meaningless.
        tracks[keep].spanEndTrackId.clear();
        tracks[keep].parentTrackId.clear();
    }

    // Camera clips belong on the camera track and nowhere else; anything else belongs off it.
    // Collect both sets first, because fixing either one moves tracks about.
    QList<Clip> homeless; // camera clips found elsewhere
    QList<Clip> evicted;  // non-camera clips found on the camera track
    for (int i = 0; i < tracks.size(); ++i) {
        const bool isCamera = tracks.at(i).isCameraLayer();
        for (int c = tracks[i].clips.size() - 1; c >= 0; --c) {
            if (isCameraClip(tracks.at(i).clips.at(c)) != isCamera)
                (isCamera ? evicted : homeless).prepend(tracks[i].clips.takeAt(c));
        }
    }

    if (!homeless.isEmpty()) {
        int index = cameraTrackIndex(tracks);
        if (index < 0)
            index = insertCameraTrack(tracks);
        tracks[index].clips.append(homeless);
    }
    // Lifted onto a track of their own rather than dropped, the way a stray on a transform layer
    // is: losing a clip to a normalisation pass is never the right answer.
    for (const Clip &clip : evicted) {
        Track lifted;
        lifted.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        lifted.type = trackTypeForClipType(clip.type);
        lifted.clips.append(clip);
        tracks.append(lifted);
    }

    const int index = cameraTrackIndex(tracks);
    if (index < 0)
        return;
    // A camera is not pinned to anything, and overlapping clips would make "the camera now"
    // ambiguous. Sort by start and trim any overlap back, so one cut ends where the next begins.
    for (Clip &clip : tracks[index].clips)
        clip.linkedClipId.clear();
    std::sort(tracks[index].clips.begin(), tracks[index].clips.end(),
              [](const Clip &a, const Clip &b) { return a.timelineStart < b.timelineStart; });
    QList<Clip> &clips = tracks[index].clips;
    for (int c = 0; c + 1 < clips.size();) {
        const TimeUs nextStart = clips.at(c + 1).timelineStart;
        if (clips.at(c).timelineEnd() <= nextStart) {
            ++c;
            continue;
        }
        if (nextStart <= clips.at(c).timelineStart) {
            // Fully swallowed by the one that follows; the later edit is the one to keep.
            clips.removeAt(c);
            continue;
        }
        clips[c].timelineDuration = nextStart - clips.at(c).timelineStart;
        clips[c].srcOut = clips.at(c).srcIn + clips.at(c).timelineDuration;
        ++c;
    }
}

void liftAdjustmentClipsToOwnTracks(Project &project)
{
    QList<Track> &tracks = project.tracks();

    // Runs after every edit as well as on load, so the settled case must cost one scan and no
    // allocation.
    bool anyOnAVideoTrack = false;
    for (const Track &track : tracks) {
        if (track.type != TrackType::Video)
            continue;
        for (const Clip &clip : track.clips) {
            if (clip.type == ClipType::Adjustment) {
                anyOnAVideoTrack = true;
                break;
            }
        }
        if (anyOnAVideoTrack)
            break;
    }
    if (!anyOnAVideoTrack)
        return;

    for (int i = tracks.size() - 1; i >= 0; --i) {
        Track &track = tracks[i];
        if (track.type != TrackType::Video)
            continue;

        int adjustmentCount = 0;
        for (const Clip &clip : track.clips) {
            if (clip.type == ClipType::Adjustment)
                ++adjustmentCount;
        }
        if (adjustmentCount == 0)
            continue;

        // The overwhelmingly common shape: a track insertTrackAtTopForClipType() created to hold
        // nothing but adjustments. Converting in place keeps its index, and therefore its z-order.
        if (adjustmentCount == track.clips.size()) {
            track.type = TrackType::Adjustment;
            track.adjustmentScope = AdjustmentScope::AllBelow;
            track.parentTrackId.clear();
            track.number = 0; // numbered again among the adjustment tracks
            continue;
        }

        // Mixed track: lift the adjustments onto their own track directly above this one.
        // addAdjustmentClipAt only ever placed an adjustment in a gap, so no frame ever held an
        // adjustment and a neighbour from this track at once — the split cannot reorder anything.
        Track lifted;
        lifted.type = TrackType::Adjustment;
        lifted.adjustmentScope = AdjustmentScope::AllBelow;
        lifted.hidden = track.hidden;
        lifted.locked = track.locked;
        lifted.heightScale = track.heightScale;
        for (int c = track.clips.size() - 1; c >= 0; --c) {
            if (track.clips.at(c).type == ClipType::Adjustment)
                lifted.clips.prepend(track.clips.takeAt(c));
        }
        tracks.insert(i, lifted);
    }
}


void hoistClipEffectsToAdjustmentLanes(Project &project)
{
    // Runs after every edit, so the "nothing to do" case has to be cheap: one scan, no
    // allocation, no id minting, no track-list churn.
    bool anyStackOnAClip = false;
    for (const Track &track : project.tracks()) {
        if (track.isAdjustment())
            continue;
        for (const Clip &clip : track.clips) {
            if (clip.type != ClipType::Adjustment
                && (!clip.effects.isEmpty() || !clip.audioEffects.isEmpty())) {
                anyStackOnAClip = true;
                break;
            }
        }
        if (anyStackOnAClip)
            break;
    }
    if (!anyStackOnAClip)
        return;

    project.ensureTrackIds();

    const auto makeLinkedAdjustment = [](const Clip &clip, AdjustmentKind kind) {
        Clip adjustment;
        adjustment.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        adjustment.type = ClipType::Adjustment;
        adjustment.adjustmentKind = kind;
        adjustment.linkedClipId = clip.id;
        adjustment.timelineStart = clip.timelineStart;
        adjustment.timelineDuration = clip.timelineDuration;
        adjustment.srcIn = 0;
        adjustment.srcOut = clip.timelineDuration;
        return adjustment;
    };

    // Drop into the first lane with room, adding one only when every existing lane is occupied
    // over that span. Clips on a track rarely overlap, so this usually yields a single lane.
    const auto place = [](QList<Track> &lanes, const Clip &adjustment) {
        for (Track &lane : lanes) {
            bool collides = false;
            for (const Clip &existing : lane.clips) {
                if (adjustment.timelineStart < existing.timelineEnd()
                    && existing.timelineStart < adjustment.timelineEnd()) {
                    collides = true;
                    break;
                }
            }
            if (!collides) {
                lane.clips.append(adjustment);
                return;
            }
        }
        Track lane;
        lane.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        lane.type = TrackType::Adjustment;
        lane.adjustmentScope = AdjustmentScope::ParentTrack;
        lane.clips.append(adjustment);
        lanes.append(lane);
    };

    QList<Track> &tracks = project.tracks();
    // Bottom-to-top, inserting at `i`: every index below the cursor stays valid, so one pass
    // suffices even though the list grows underneath it.
    for (int i = tracks.size() - 1; i >= 0; --i) {
        Track &track = tracks[i];
        if (track.isAdjustment())
            continue;

        // Existing lanes are candidates too, so a second effect on a clip that already has an
        // adjustment reuses it instead of stacking up empty rows.
        QList<Track> lanes;
        QList<int> existingLaneIndexes;
        for (const int laneIndex : adjustmentLaneIndexes(project, i)) {
            lanes.append(tracks.at(laneIndex));
            existingLaneIndexes.append(laneIndex);
        }
        const int existingLaneCount = lanes.size();

        bool moved = false;
        for (Clip &clip : track.clips) {
            if (clip.type == ClipType::Adjustment)
                continue;

            for (const AdjustmentKind kind :
                 {AdjustmentKind::VideoEffects, AdjustmentKind::AudioEffects}) {
                QList<Effect> &source =
                    kind == AdjustmentKind::VideoEffects ? clip.effects : clip.audioEffects;
                if (source.isEmpty())
                    continue;

                // Reuse the adjustment already linked to this clip rather than minting a second
                // one, or the stack would split across two rows and the inspector's indices
                // would stop matching what renders.
                Clip *host = nullptr;
                for (Track &lane : lanes) {
                    for (Clip &candidate : lane.clips) {
                        if (candidate.adjustmentKind == kind && candidate.linkedClipId == clip.id) {
                            host = &candidate;
                            break;
                        }
                    }
                    if (host)
                        break;
                }

                if (host) {
                    (kind == AdjustmentKind::VideoEffects ? host->effects : host->audioEffects)
                        .append(source);
                } else {
                    Clip adjustment = makeLinkedAdjustment(clip, kind);
                    (kind == AdjustmentKind::VideoEffects ? adjustment.effects
                                                          : adjustment.audioEffects) = source;
                    place(lanes, adjustment);
                }
                source.clear();
                moved = true;
            }
        }

        if (!moved)
            continue;

        // Read the parent's id before any insert invalidates the reference above.
        const QString parentId = track.id;
        for (Track &lane : lanes)
            lane.parentTrackId = parentId;

        // Write the reused lanes back in place first, while their indices still hold, then splice
        // in only the new ones — after the existing lanes, so the order effects apply in is the
        // order they were added. Everything lands below `i`, leaving the outer loop's remaining
        // indices untouched.
        for (int l = 0; l < existingLaneCount; ++l)
            tracks[existingLaneIndexes.at(l)] = lanes.at(l);
        const int insertAt =
            existingLaneIndexes.isEmpty() ? i + 1 : existingLaneIndexes.constLast() + 1;
        for (int l = lanes.size() - 1; l >= existingLaneCount; --l)
            tracks.insert(insertAt, lanes.at(l));
    }
}

int ensureTrackForClipType(Project &project, ClipType type, bool insertAtTop)
{
    const int existing = defaultTrackForClipType(project, type);
    if (existing >= 0)
        return existing;

    const Track track{.type = trackTypeForClipType(type)};
    if (insertAtTop)
        project.tracks().prepend(track);
    else
        project.tracks().append(track);
    return insertAtTop ? 0 : project.tracks().size() - 1;
}

// Like ensureTrackForClipType, but it will not hand back a lane whose span is already taken.
//
// Image, Shape, Vector and Model3d all map onto one track type, so emoji, stickers, shapes, Lottie
// and 3D models share a single lane by default. Adding a second one at the same time then pushed it
// down the timeline to the next free gap, which silently moved a graphic away from the moment it
// was meant to appear. Stacking them on their own lanes is what the caller meant.
int ensureFreeTrackForClipType(Project &project, ClipType type, TimeUs startUs, TimeUs durationUs,
                               bool insertAtTop)
{
    const TrackType trackType = trackTypeForClipType(type);
    int firstMatch = -1;
    const QList<Track> &tracks = project.tracks();
    for (int i = 0; i < tracks.size(); ++i) {
        if (tracks[i].isAdjustmentLane())
            continue;
        if (tracks[i].type != trackType || !tracks[i].allowsClipType(type))
            continue;
        if (firstMatch < 0)
            firstMatch = i;
        bool collides = false;
        for (const Clip &existing : tracks[i].clips) {
            if (startUs < existing.timelineEnd() && existing.timelineStart < startUs + durationUs) {
                collides = true;
                break;
            }
        }
        if (!collides)
            return i;
    }

    if (firstMatch < 0)
        return ensureTrackForClipType(project, type, insertAtTop);
    // Every existing lane is busy at this moment, so give the clip one of its own directly above
    // the first, which keeps later graphics drawing over earlier ones.
    return insertTrackAboveForClipType(project, firstMatch, type);
}

int insertTrackAtTopForClipType(Project &project, ClipType type)
{
    project.tracks().prepend(Track{.type = trackTypeForClipType(type)});
    return 0;
}

int insertTrackAboveForClipType(Project &project, int trackIndex, ClipType type)
{
    const int at = qBound(0, trackIndex, project.tracks().size());
    project.tracks().insert(at, Track{.type = trackTypeForClipType(type)});
    return at;
}

TimeUs clipDurationForAsset(const MediaAsset *asset)
{
    if (!asset)
        return imageClipDurationUs();

    if (asset->kind == MediaKind::Image)
        return imageClipDurationUs();

    if (asset->durationUs > 0)
        return asset->durationUs;

    return imageClipDurationUs();
}

TimeUs sourceDurationForClip(const Project &project, const Clip &clip)
{
    // A composite's source is its nested timeline, which grows as content is added inside it.
    if (!clip.sequenceId.isEmpty())
        return project.sequenceDurationUs(clip.sequenceId);

    if (!clip.assetId.isEmpty()) {
        if (const MediaAsset *asset = project.asset(clip.assetId)) {
            if (asset->durationUs > 0)
                return asset->durationUs;
        }
    }

    if (clip.type == ClipType::Image || clip.type == ClipType::Shape || clip.type == ClipType::Vector
        || clip.type == ClipType::Model3d || clip.type == ClipType::Pdf
        || clip.type == ClipType::Procedural || clip.type == ClipType::Adjustment)
        return imageClipDurationUs();

    return qMax(clip.srcOut, clip.timelineDuration);
}

TimeUs minClipDurationUs(double fps)
{
    if (fps <= 0.0)
        return kUsPerSecond / 120;
    return static_cast<TimeUs>(std::ceil(static_cast<double>(kUsPerSecond) / fps));
}

namespace {

// Splits "Base (N)" into its base and N, when N is a plausible piece number.
bool splitPieceNumber(const QString &name, QString *base, int *number)
{
    if (!name.endsWith(QLatin1Char(')')))
        return false;
    const int open = name.lastIndexOf(QStringLiteral(" ("));
    if (open <= 0)
        return false;
    bool ok = false;
    const int n = QStringView(name).mid(open + 2, name.size() - open - 3).toInt(&ok);
    if (!ok || n < 1 || n >= 1000)
        return false;
    *base = name.left(open);
    *number = n;
    return true;
}

} // namespace

QString nextSplitPieceName(const Track &track, const QString &name)
{
    if (name.isEmpty())
        return name;
    QString base = name;
    int ignored = 0;
    splitPieceNumber(name, &base, &ignored);

    int highest = 1;
    QSet<QString> taken;
    for (const Clip &clip : track.clips) {
        taken.insert(clip.name);
        QString otherBase;
        int n = 0;
        if (splitPieceNumber(clip.name, &otherBase, &n) && otherBase == base)
            highest = qMax(highest, n);
    }
    // Past 999 the parser no longer reads the number as a piece number, so check the whole name.
    int next = highest + 1;
    while (taken.contains(QStringLiteral("%1 (%2)").arg(base).arg(next)))
        ++next;
    return QStringLiteral("%1 (%2)").arg(base).arg(next);
}

QSet<int> rippleTrackGroup(const Project &project, int trackIndex)
{
    const QList<Track> &tracks = project.tracks();
    QSet<int> group;
    if (trackIndex < 0 || trackIndex >= tracks.size() || tracks.at(trackIndex).isAdjustment())
        return group;
    // Which tracks hold each link, then a walk over tracks sharing one.
    QHash<QString, QSet<int>> linkTracks;
    for (int t = 0; t < tracks.size(); ++t) {
        if (tracks.at(t).isAdjustment())
            continue;
        for (const Clip &clip : tracks.at(t).clips) {
            if (!clip.linkId.isEmpty())
                linkTracks[clip.linkId].insert(t);
        }
    }
    QList<int> pending{trackIndex};
    while (!pending.isEmpty()) {
        const int t = pending.takeLast();
        if (group.contains(t))
            continue;
        group.insert(t);
        for (const Clip &clip : tracks.at(t).clips) {
            if (clip.linkId.isEmpty())
                continue;
            for (const int other : linkTracks.value(clip.linkId)) {
                if (!group.contains(other))
                    pending.append(other);
            }
        }
    }
    return group;
}

QStringList rippleTracks(Project &project, const QSet<int> &tracks, TimeUs fromUs, TimeUs deltaUs,
                         const QSet<QString> &exclude, bool *clamped, QString *clampedId)
{
    QStringList moved;
    if (deltaUs == 0)
        return moved;
    QList<Track> &all = project.tracks();
    QSet<QString> movedLinks;
    QSet<QString> movedIds;
    for (const int t : tracks) {
        if (t < 0 || t >= all.size())
            continue;
        for (Clip &clip : all[t].clips) {
            if (exclude.contains(clip.id) || clip.timelineStart < fromUs)
                continue;
            if (clip.timelineStart + deltaUs < 0) {
                if (clamped)
                    *clamped = true;
                if (clampedId && clampedId->isEmpty())
                    *clampedId = clip.id;
            }
            clip.timelineStart = qMax<TimeUs>(0, clip.timelineStart + deltaUs);
            moved.append(clip.id);
            movedIds.insert(clip.id);
            if (!clip.linkId.isEmpty())
                movedLinks.insert(clip.linkId);
        }
    }
    // Earlier clips linked to a moved one keep their offset to it.
    if (!movedLinks.isEmpty()) {
        for (const int t : tracks) {
            if (t < 0 || t >= all.size())
                continue;
            for (Clip &clip : all[t].clips) {
                if (movedIds.contains(clip.id) || exclude.contains(clip.id) || clip.linkId.isEmpty()
                    || !movedLinks.contains(clip.linkId))
                    continue;
                if (clip.timelineStart + deltaUs < 0) {
                    if (clamped)
                        *clamped = true;
                    if (clampedId && clampedId->isEmpty())
                        *clampedId = clip.id;
                }
                clip.timelineStart = qMax<TimeUs>(0, clip.timelineStart + deltaUs);
                moved.append(clip.id);
                movedIds.insert(clip.id);
            }
        }
    }
    return moved;
}

QStringList shiftWindow(Project &project, const QSet<int> &tracks, TimeUs fromUs, TimeUs toUs, TimeUs deltaUs,
                        const QSet<QString> &exclude, bool *clamped, QString *clampedId)
{
    QStringList moved;
    if (deltaUs == 0 || toUs <= fromUs)
        return moved;
    QList<Track> &all = project.tracks();
    QSet<QString> movedIds;
    QSet<QString> movedLinks;
    const auto move = [&](Clip &clip) {
        if (clip.timelineStart + deltaUs < 0) {
            if (clamped)
                *clamped = true;
            if (clampedId && clampedId->isEmpty())
                *clampedId = clip.id;
        }
        clip.timelineStart = qMax<TimeUs>(0, clip.timelineStart + deltaUs);
        moved.append(clip.id);
        movedIds.insert(clip.id);
    };
    for (const int t : tracks) {
        if (t < 0 || t >= all.size())
            continue;
        for (Clip &clip : all[t].clips) {
            if (exclude.contains(clip.id) || clip.timelineStart < fromUs || clip.timelineStart >= toUs)
                continue;
            move(clip);
            if (!clip.linkId.isEmpty())
                movedLinks.insert(clip.linkId);
        }
    }
    if (!movedLinks.isEmpty()) {
        for (const int t : tracks) {
            if (t < 0 || t >= all.size())
                continue;
            for (Clip &clip : all[t].clips) {
                if (!movedIds.contains(clip.id) && !exclude.contains(clip.id) && !clip.linkId.isEmpty()
                    && movedLinks.contains(clip.linkId))
                    move(clip);
            }
        }
    }
    return moved;
}

QStringList shiftClipWithLinks(Project &project, const QString &clipId, TimeUs deltaUs, bool *clamped,
                               QString *clampedId)
{
    QStringList moved;
    QString link;
    for (const Track &track : project.tracks())
        for (const Clip &clip : track.clips)
            if (clip.id == clipId)
                link = clip.linkId;
    for (Track &track : project.tracks()) {
        if (track.isAdjustment())
            continue;
        for (Clip &clip : track.clips) {
            if (clip.id != clipId && (link.isEmpty() || clip.linkId != link))
                continue;
            if (clip.timelineStart + deltaUs < 0) {
                if (clamped)
                    *clamped = true;
                if (clampedId && clampedId->isEmpty())
                    *clampedId = clip.id;
            }
            clip.timelineStart = qMax<TimeUs>(0, clip.timelineStart + deltaUs);
            moved.append(clip.id);
        }
    }
    return moved;
}

QString rippleCollision(const Project &before, const Project &after, const QSet<int> &tracks)
{
    QSet<QString> overlappedBefore;
    const auto pairKey = [](const QString &a, const QString &b) {
        return a < b ? a + QLatin1Char('|') + b : b + QLatin1Char('|') + a;
    };
    for (const int t : tracks) {
        if (t < 0 || t >= before.tracks().size())
            continue;
        const QList<Clip> &clips = before.tracks().at(t).clips;
        for (int i = 0; i < clips.size(); ++i)
            for (int j = i + 1; j < clips.size(); ++j)
                if (clips.at(i).timelineStart < clips.at(j).timelineEnd()
                    && clips.at(j).timelineStart < clips.at(i).timelineEnd())
                    overlappedBefore.insert(pairKey(clips.at(i).id, clips.at(j).id));
    }
    for (const int t : tracks) {
        if (t < 0 || t >= after.tracks().size() || after.tracks().at(t).isAdjustment())
            continue;
        const QList<Clip> &clips = after.tracks().at(t).clips;
        for (int i = 0; i < clips.size(); ++i) {
            for (int j = i + 1; j < clips.size(); ++j) {
                const Clip &a = clips.at(i);
                const Clip &b = clips.at(j);
                if (a.timelineStart < b.timelineEnd() && b.timelineStart < a.timelineEnd()
                    && !overlappedBefore.contains(pairKey(a.id, b.id)))
                    return a.timelineStart <= b.timelineStart ? b.id : a.id;
            }
        }
    }
    return {};
}

bool closeRippleRanges(Project &project, const QList<RippleRange> &ranges, QString *blocker, QStringList *moved,
                       const Project *groupSource)
{
    // Group the ranges, then merge each group's ranges into disjoint ones.
    QList<QSet<int>> groups;
    QList<QList<QPair<TimeUs, TimeUs>>> groupRanges;
    for (const RippleRange &range : ranges) {
        if (range.end <= range.start || range.track < 0 || range.track >= project.tracks().size())
            continue;
        int g = 0;
        while (g < groups.size() && !groups.at(g).contains(range.track))
            ++g;
        if (g == groups.size()) {
            groups.append(rippleTrackGroup(groupSource ? *groupSource : project, range.track));
            groupRanges.append(QList<QPair<TimeUs, TimeUs>>());
        }
        groupRanges[g].append(qMakePair(range.start, range.end));
    }
    for (QList<QPair<TimeUs, TimeUs>> &list : groupRanges) {
        std::sort(list.begin(), list.end());
        QList<QPair<TimeUs, TimeUs>> merged;
        for (const auto &r : std::as_const(list)) {
            if (!merged.isEmpty() && r.first <= merged.last().second)
                merged.last().second = qMax(merged.last().second, r.second);
            else
                merged.append(r);
        }
        list = merged;
    }
    // Nothing may sit inside a range that closes, on any track of its group.
    for (int g = 0; g < groups.size(); ++g) {
        for (const int t : groups.at(g)) {
            for (const Clip &clip : project.tracks().at(t).clips) {
                for (const auto &r : groupRanges.at(g)) {
                    if (clip.timelineStart < r.second && clip.timelineEnd() > r.first) {
                        if (blocker)
                            *blocker = clip.id;
                        return false;
                    }
                }
            }
        }
    }
    const Project unchanged = project;
    QSet<QString> movedIds;
    QSet<int> touched;
    bool clamped = false;
    QString clampedId;
    for (int g = 0; g < groups.size(); ++g) {
        touched.unite(groups.at(g));
        const QList<QPair<TimeUs, TimeUs>> &list = groupRanges.at(g);
        for (int i = int(list.size()) - 1; i >= 0; --i) {
            for (const QString &id : rippleTracks(project, groups.at(g), list.at(i).second,
                                                  -(list.at(i).second - list.at(i).first), {}, &clamped, &clampedId))
                movedIds.insert(id);
        }
    }
    // An earlier linked clip shifted with its partner can still land on a clip that stayed, or run
    // out of room before 0: refused as well, with nothing changed.
    const QString collision = rippleCollision(unchanged, project, touched);
    if (!collision.isEmpty() || clamped) {
        if (blocker) {
            *blocker = collision;
            if (collision.isEmpty())
                *blocker = clampedId; // the clip stopped at 0 short of its full move
        }
        project = unchanged;
        return false;
    }
    if (moved)
        *moved = movedIds.values();
    return true;
}

QPair<QString, int> splitLabel(const QString &label)
{
    const int dot = label.lastIndexOf(QLatin1Char('.'));
    if (dot > 0 && dot + 1 < label.size()) {
        bool ok = false;
        const int n = label.mid(dot + 1).toInt(&ok);
        if (ok && n >= 2 && label.mid(dot + 1) == QString::number(n))
            return {label.left(dot), n};
    }
    return {label, 1};
}

bool labelMatches(const QString &label, const QString &query)
{
    if (label.isEmpty() || query.isEmpty())
        return false;
    if (label == query)
        return true;
    // An unnumbered query also finds its numbered copies.
    return splitLabel(query).second == 1 && splitLabel(label).first == query;
}

int numberCopiedLabels(const Project &before, Project &after)
{
    QSet<QString> oldIds;
    for (const Track &track : before.tracks())
        for (const Clip &clip : track.clips)
            oldIds.insert(clip.id);

    // The labels clips that were already there still hold, and every number in use per base.
    QSet<QString> taken;
    QHash<QString, QSet<int>> used;
    for (const Track &track : after.tracks()) {
        for (const Clip &clip : track.clips) {
            if (clip.label.isEmpty())
                continue;
            const QPair<QString, int> parts = splitLabel(clip.label);
            used[parts.first].insert(parts.second);
            if (oldIds.contains(clip.id))
                taken.insert(clip.label);
        }
    }

    // The new labelled clips, in timeline order so the numbers read left to right.
    struct Copy
    {
        int track;
        int clip;
        TimeUs start;
    };
    QList<Copy> copies;
    for (int t = 0; t < after.tracks().size(); ++t) {
        const QList<Clip> &clips = after.tracks().at(t).clips;
        for (int c = 0; c < clips.size(); ++c)
            if (!oldIds.contains(clips.at(c).id) && !clips.at(c).label.isEmpty())
                copies.append({t, c, clips.at(c).timelineStart});
    }
    if (copies.isEmpty())
        return 0;
    std::stable_sort(copies.begin(), copies.end(), [](const Copy &a, const Copy &b) { return a.start < b.start; });

    const auto nextFree = [&used](const QString &base) {
        QSet<int> &numbers = used[base];
        int highest = 1;
        for (const int n : std::as_const(numbers))
            highest = qMax(highest, n);
        int n = highest < std::numeric_limits<int>::max() ? highest + 1 : 2;
        while (numbers.contains(n)) // only after the top number ran out
            ++n;
        numbers.insert(n);
        return base + QLatin1Char('.') + QString::number(n);
    };
    // A copy keeps its label when nothing else holds it any more (a clip replaced by a new one: a
    // speed ramp, a subtitle conversion, a cut and paste); otherwise it takes the next number.
    // Linked new clips (a split A/V pair) take the same outcome.
    QHash<QString, QString> decided; // linkId|label -> label
    int changed = 0;
    for (const Copy &copy : std::as_const(copies)) {
        Clip &clip = after.tracks()[copy.track].clips[copy.clip];
        const QString key = clip.linkId.isEmpty() ? QString() : clip.linkId + QLatin1Char('|') + clip.label;
        QString label = key.isEmpty() ? QString() : decided.value(key);
        if (label.isEmpty()) {
            label = taken.contains(clip.label) ? nextFree(splitLabel(clip.label).first) : clip.label;
            taken.insert(label);
            if (!key.isEmpty())
                decided.insert(key, label);
        }
        if (clip.label != label) {
            clip.label = label;
            ++changed;
        }
    }
    return changed;
}

bool splitClipAtOffset(Clip &head, Clip &tail, TimeUs offset, double fps)
{
    return splitClipAtOffsetMin(head, tail, offset, minClipDurationUs(fps));
}

bool splitClipAtOffsetMin(Clip &head, Clip &tail, TimeUs offset, TimeUs minEdgeUs)
{
    minEdgeUs = qMax<TimeUs>(1, minEdgeUs);
    if (offset < minEdgeUs || head.timelineDuration - offset < minEdgeUs)
        return false;

    const TimeUs sourceSpan = head.srcOut - head.srcIn;
    // A looped clip keeps its range on both sides; only where the tail's first pass begins moves.
    const bool looped = head.loops();
    const TimeUs sourceOffset =
        looped ? 1
        : head.hasSpeedCurve() ? head.speedCurve.sourceOffsetForTimelineOffset(offset, sourceSpan)
                             : head.sourceDeltaForTimelineDelta(offset);
    if (sourceOffset <= 0 || sourceOffset >= sourceSpan)
        return false;

    tail = head;
    tail.timelineStart = head.timelineStart + offset;
    tail.timelineDuration = head.timelineDuration - offset;

    // Curve positions are normalised over the clip's own source range, so each half needs the
    // parent's ramp resampled onto its shorter range — copying it verbatim would stretch both
    // halves back over the full shape and change how they play.
    if (head.hasSpeedCurve()) {
        const double cut = static_cast<double>(sourceOffset) / sourceSpan;
        const SpeedCurve parent = head.speedCurve;
        head.speedCurve = parent.subRange(0.0, cut);
        tail.speedCurve = parent.subRange(cut, 1.0);
    }

    if (looped) {
        tail.loopOffsetUs = head.loopedSourceOffset(offset);
    } else if (head.reverse) {
        const TimeUs sourceAtSplit = head.srcOut - sourceOffset;
        tail.srcIn = head.srcIn;
        tail.srcOut = sourceAtSplit;
        head.srcIn = sourceAtSplit;
    } else {
        tail.srcIn = head.srcIn + sourceOffset;
        head.srcOut = head.srcIn + sourceOffset;
    }

    // Cue times are relative to the parent clip's timeline start, so the tail's copies have to be
    // rebased onto its new start; a cue straddling the cut is truncated on the left and resumes on
    // the right.
    if (!head.subtitleCues.isEmpty()) {
        QList<SubtitleCue> headCues;
        QList<SubtitleCue> tailCues;
        for (const SubtitleCue &cue : head.subtitleCues) {
            if (cue.startUs < offset) {
                SubtitleCue left = cue;
                left.endUs = qMin(cue.endUs, offset);
                if (left.endUs > left.startUs)
                    headCues.append(left);
            }
            if (cue.endUs > offset) {
                SubtitleCue right = cue;
                right.startUs = qMax<TimeUs>(cue.startUs - offset, 0);
                right.endUs = cue.endUs - offset;
                if (right.endUs > right.startUs)
                    tailCues.append(right);
            }
        }
        head.subtitleCues = headCues;
        tail.subtitleCues = tailCues;
        if (head.type == ClipType::Subtitle) {
            refreshSubtitleClipName(head.name, head.subtitleCues);
            refreshSubtitleClipName(tail.name, tail.subtitleCues);
        }
    }

    head.timelineDuration = offset;
    // A cut is invisible: neither half fades or animates at it. The outer fades stay where they
    // were (clamped to the shorter halves), and mergeClips puts the tail's fade-out back.
    head.fadeOutUs = 0;
    head.animOut = ClipAnimation{};
    head.audioFadeOutUs = 0;
    tail.fadeInUs = 0;
    tail.animIn = ClipAnimation{};
    tail.audioFadeInUs = 0;
    head.fadeInUs = qMin(head.fadeInUs, head.timelineDuration);
    tail.fadeOutUs = qMin(tail.fadeOutUs, tail.timelineDuration);
    // Key times are relative to the clip's own start, so the tail — which now starts `offset`
    // later — has to carry its curves back by the same amount. Without this a cut, which should be
    // invisible, replays the whole animation `offset` later on the second half. The head keeps its
    // keys as they are, including any past the cut: they still shape the curve inside its range.
    rebaseKeyframesForSplitTail(tail, offset);
    return true;
}

void retargetClipToSource(Clip &dst, const Clip &src, TimeUs srcMediaDurationUs)
{
    // Media identity.
    dst.assetId = src.assetId;
    dst.path = src.path;
    dst.sourceFrame = src.sourceFrame;
    dst.sourceFrameAngle = src.sourceFrameAngle;
    dst.type = src.type;
    dst.name = src.name;
    dst.thumbnailPath = src.thumbnailPath;
    dst.filmstripPath = src.filmstripPath;
    dst.emoji = src.emoji;

    // Fields that decide how timeline time maps onto source time. They have to come from the
    // angle: reading its frames through the outgoing clip's mapping would show the wrong ones.
    dst.speed = src.speed;
    dst.reverse = src.reverse;
    dst.flipH = src.flipH;
    dst.flipV = src.flipV;
    // Belongs to the angle's media, not the slot: it is what makes that file decode upright.
    dst.rotationCorrection = src.rotationCorrection;

    // A ramp is normalised over the clip's own source range and decides its timeline duration.
    // dst's duration is fixed by the slot it occupies, so there is no range for a ramp to
    // describe — carrying one over would contradict the placement being preserved.
    dst.speedCurve = SpeedCurve();
    // The program clip is no longer the video half of whatever pair it was in; leaving the id
    // would have syncLinkedTiming drag the old companion around after it.
    dst.linkId.clear();
    // Landmarks are baked against the outgoing media, indexed by its source time.
    dst.faceTrackPath.clear();
    dst.faceTrackSrcOffsetUs = 0;
    dst.depthPath.clear();
    // Masks are not reachable from here: they live on the adjustments pinned to the clip, not on
    // the clip. The caller must follow this with clearLinkedMasks(..., mediaOnly = true) — media
    // coverage is rendered pixels describing only the camera it was traced from, and kept it
    // would cut the new angle to the old one's silhouette. Geometric masks are treatment like the
    // transform and the effects, and stay.

    // The frame `src` is showing where dst begins — this is the whole point of the operation.
    const TimeUs srcIn = qBound(TimeUs{0}, src.timelineToSourceUs(dst.timelineStart),
                                qMax(TimeUs{0}, srcMediaDurationUs));
    dst.srcIn = srcIn;

    const TimeUs wanted = dst.sourceSpanUs();
    const TimeUs available = qMax(TimeUs{0}, srcMediaDurationUs - srcIn);
    const TimeUs span = qMin(wanted, available);
    dst.srcOut = srcIn + span;

    // The media ran out before the slot did; pull the timeline duration back to what is
    // actually there rather than looping or freezing on the last frame.
    if (span < wanted && dst.effectiveSpeed() > 0.0) {
        dst.timelineDuration =
            qMax(TimeUs{1}, static_cast<TimeUs>(llround(static_cast<double>(span) / dst.effectiveSpeed())));
    }
}

bool clipsCanMerge(const Clip &left, const Clip &right)
{
    if (left.type != right.type)
        return false;
    if (left.assetId.isEmpty() || left.assetId != right.assetId)
        return false;
    if (left.path != right.path)
        return false;
    if (left.reverse != right.reverse)
        return false;
    if (!qFuzzyCompare(left.speed, right.speed))
        return false;
    // Two ramps do not concatenate into one: the merged clip would have to carry both shapes
    // over a single normalised range.
    if (left.hasSpeedCurve() || right.hasSpeedCurve())
        return false;
    if (left.timelineEnd() != right.timelineStart)
        return false;

    // Two halves of a split loop: same range, and the right one picks up where the left stopped.
    if (left.loops() || right.loops()) {
        return left.loops() && right.loops() && left.srcIn == right.srcIn && left.srcOut == right.srcOut
               && left.loopedSourceOffset(left.timelineDuration) == right.loopOffsetUs;
    }

    if (left.reverse)
        return left.srcIn == right.srcOut;
    return left.srcOut == right.srcIn;
}

Clip mergeClips(const Clip &left, const Clip &right)
{
    Clip out = left;
    out.timelineDuration = left.timelineDuration + right.timelineDuration;
    if (left.loops()) {
        // Same range on both sides; the left one's offset already starts the merged clip right.
    } else if (left.reverse) {
        out.srcIn = right.srcIn;
        out.srcOut = left.srcOut;
    } else {
        out.srcOut = right.srcOut;
    }
    out.fadeOutUs = right.fadeOutUs;
    out.animOut = right.animOut;
    out.audioFadeOutUs = right.audioFadeOutUs;
    return out;
}

bool subtitleClipsCanMerge(const QList<Clip> &clips)
{
    if (clips.size() < 2)
        return false;
    for (const Clip &clip : clips) {
        if (clip.type != ClipType::Subtitle)
            return false;
    }
    return true;
}

Clip mergeSubtitleClips(QList<Clip> clips)
{
    std::stable_sort(clips.begin(), clips.end(),
                     [](const Clip &a, const Clip &b) { return a.timelineStart < b.timelineStart; });

    Clip out = clips.first();
    TimeUs end = out.timelineEnd();
    for (const Clip &clip : clips)
        end = qMax(end, clip.timelineEnd());
    out.timelineDuration = end - out.timelineStart;
    out.srcIn = 0;
    out.srcOut = out.timelineDuration;
    out.fadeOutUs = clips.last().fadeOutUs;

    QList<SubtitleCue> cues;
    for (const Clip &clip : clips) {
        const TimeUs offset = clip.timelineStart - out.timelineStart;
        for (SubtitleCue cue : clip.subtitleCues) {
            cue.startUs = qBound(TimeUs{0}, cue.startUs + offset, out.timelineDuration);
            cue.endUs = qBound(TimeUs{0}, cue.endUs + offset, out.timelineDuration);
            cues.append(cue);
        }
    }
    sortSubtitleCues(cues);
    out.subtitleCues = cues;
    // A name someone chose survives the merge whichever clip it was on: the earliest name that is
    // not recognisably generated wins; with none, the merged clip gets the generated count. A
    // generated name from another UI language isn't recognisable (see
    // isGeneratedSubtitleClipName), so it is kept like a chosen one and can win over a later one.
    for (const Clip &clip : std::as_const(clips)) {
        if (!isGeneratedSubtitleClipName(clip.name)) {
            out.name = clip.name;
            break;
        }
    }
    refreshSubtitleClipName(out.name, out.subtitleCues);
    return out;
}

QString subtitleSourceKey(const Clip &clip)
{
    return clip.assetId.isEmpty() ? clip.path : clip.assetId;
}

namespace {

bool sameTiming(const Clip &a, const Clip &b)
{
    return a.timelineStart == b.timelineStart && a.timelineDuration == b.timelineDuration
           && a.srcIn == b.srcIn && a.srcOut == b.srcOut && a.speed == b.speed && a.reverse == b.reverse
           && a.speedCurve.toJson() == b.speedCurve.toJson();
}

struct RetimedClip
{
    Clip from;
    Clip to;
    QString key;
};

// Where timeline time `t` lands after the retimed clip of media `key` whose old span holds it.
// Spans are half-open towards the side `t` belongs to, so the start of a span butting against a
// retimed clip of the same media doesn't get dragged along with that clip's end.
std::optional<TimeUs> retimedTime(const QList<RetimedClip> &changes, const QString &key, TimeUs t,
                                  bool isEnd)
{
    for (const RetimedClip &c : changes) {
        const bool inside = isEnd ? t > c.from.timelineStart && t <= c.from.timelineEnd()
                                  : t >= c.from.timelineStart && t < c.from.timelineEnd();
        if (c.key == key && inside)
            return c.to.timelineStart + c.to.sourceUsToClipLocalUs(c.from.timelineToSourceUs(t));
    }
    return std::nullopt;
}

} // namespace

void retimeLinkedSubtitles(const Project &before, Project &project, const QHash<QString, QString> &replaced)
{
    QHash<QString, Clip> after;
    for (const Track &track : project.tracks()) {
        for (const Clip &clip : track.clips) {
            if (clip.type == ClipType::Video || clip.type == ClipType::Audio)
                after.insert(clip.id, clip);
        }
    }
    QList<RetimedClip> changes;
    for (const Track &track : before.tracks()) {
        for (const Clip &old : track.clips) {
            if (old.type != ClipType::Video && old.type != ClipType::Audio)
                continue;
            const auto now = after.constFind(replaced.value(old.id, old.id));
            if (now != after.constEnd() && !sameTiming(old, *now))
                changes.append({old, *now, subtitleSourceKey(old)});
        }
    }
    if (changes.isEmpty())
        return;

    for (Track &track : project.tracks()) {
        for (int i = 0; i < track.clips.size(); ++i) {
            const Clip &sub = track.clips.at(i);
            if (sub.type != ClipType::Subtitle)
                continue;

            bool touched = false;
            QSet<QString> keys;
            QList<SubtitleCue> cues = sub.subtitleCues;
            for (SubtitleCue &cue : cues) {
                cue.startUs += sub.timelineStart;
                cue.endUs += sub.timelineStart;
                if (cue.source.isEmpty())
                    continue;
                keys.insert(cue.source);
                const auto a = retimedTime(changes, cue.source, cue.startUs, false);
                const auto b = retimedTime(changes, cue.source, cue.endUs, true);
                if (!a && !b)
                    continue;
                touched = true;
                // Reverse swaps which end comes first.
                const TimeUs s = a.value_or(cue.startUs);
                const TimeUs e = b.value_or(cue.endUs);
                cue.startUs = qMin(s, e);
                cue.endUs = qMax(s, e);
            }
            if (!touched)
                continue;

            auto retimeEdge = [&](TimeUs t, bool isEnd) {
                for (const QString &key : keys) {
                    if (const auto moved = retimedTime(changes, key, t, isEnd))
                        return *moved;
                }
                return t;
            };
            const TimeUs a = retimeEdge(sub.timelineStart, false);
            const TimeUs b = retimeEdge(sub.timelineEnd(), true);
            TimeUs start = qMin(a, b);
            TimeUs end = qMax(a, b);
            // A slowed-down clip pushes its captions later; don't let them run over a neighbour.
            for (int j = 0; j < track.clips.size(); ++j) {
                const Clip &other = track.clips.at(j);
                if (j == i)
                    continue;
                if (other.timelineStart >= sub.timelineEnd())
                    end = qMin(end, other.timelineStart);
                else if (other.timelineEnd() <= sub.timelineStart)
                    start = qMax(start, other.timelineEnd());
            }
            if (end <= start)
                continue;

            QList<SubtitleCue> rebased;
            for (SubtitleCue cue : cues) {
                cue.startUs -= start;
                cue.endUs -= start;
                if (cue.endUs > cue.startUs)
                    rebased.append(cue);
            }
            sortSubtitleCues(rebased);

            Clip &out = track.clips[i];
            out.timelineStart = start;
            out.timelineDuration = end - start;
            out.srcIn = 0;
            out.srcOut = out.timelineDuration;
            out.subtitleCues = rebased;
            refreshSubtitleClipName(out.name, rebased);
        }
    }
}

void muteAllButClip(Project &snap, const QString &clipId)
{
    for (Track &track : snap.tracks()) {
        bool has = false;
        for (Clip &clip : track.clips) {
            if (clip.id == clipId) {
                has = true;
                continue;
            }
            clip.volume.setKeyframe(0, 0.0);
            clip.suppressEmbeddedAudio = true;
        }
        if (!has)
            track.muted = true;
    }
}

QList<ClipRef> linkedPartners(const Project &project, const Clip &clip)
{
    QList<ClipRef> out;
    if (clip.linkId.isEmpty())
        return out;

    for (int trackIndex = 0; trackIndex < project.tracks().size(); ++trackIndex) {
        const Track &track = project.tracks().at(trackIndex);
        for (int clipIndex = 0; clipIndex < track.clips.size(); ++clipIndex) {
            const Clip &candidate = track.clips.at(clipIndex);
            if (candidate.id == clip.id)
                continue;
            if (candidate.linkId == clip.linkId)
                out.append(ClipRef{trackIndex, clipIndex});
        }
    }
    return out;
}

void syncLinkedTiming(Clip &dst, const Clip &src)
{
    dst.timelineStart = src.timelineStart;
    dst.timelineDuration = src.timelineDuration;
    dst.srcIn = src.srcIn;
    dst.srcOut = src.srcOut;
    dst.speed = src.speed;
    dst.speedCurve = src.speedCurve;
    dst.reverse = src.reverse;
    dst.loop = src.loop;
    dst.loopOffsetUs = src.loopOffsetUs;
    dst.fadeInUs = src.fadeInUs;
    dst.fadeOutUs = src.fadeOutUs;
    dst.fadeCurve = src.fadeCurve;
    dst.fadeShape = src.fadeShape;
    dst.audioFadeInUs = src.audioFadeInUs;
    dst.audioFadeOutUs = src.audioFadeOutUs;
}

QString assignSplitLinkIds(Clip &head, Clip &tail)
{
    if (head.linkId.isEmpty())
        return {};

    const QString tailLink = QUuid::createUuid().toString(QUuid::WithoutBraces);
    tail.linkId = tailLink;
    if (head.suppressEmbeddedAudio)
        tail.suppressEmbeddedAudio = true;
    return tailLink;
}

namespace {

// Writes `value` as the sole keyframe of an empty track, leaving tracks that
// already carry explicit values (including animation) untouched.
void bakeIfImplicit(KeyframeTrack<double> &track, double value)
{
    if (track.isEmpty())
        track.setKeyframe(0, value);
}

void shiftTrackValues(KeyframeTrack<double> &track, double delta, double implicitValue)
{
    if (qFuzzyIsNull(delta))
        return;
    if (track.isEmpty()) {
        track.setKeyframe(0, implicitValue - delta);
        return;
    }
    KeyframeTrack<double> shifted;
    // Tangents ride along with each key now, so the whole shape survives the shift; only the
    // values move. dy is a delta in value units and is therefore unaffected by the offset.
    const QMap<TimeUs, Keyframe<double>> &values = track.keyframes();
    for (auto it = values.constBegin(); it != values.constEnd(); ++it) {
        Keyframe<double> key = it.value();
        key.value -= delta;
        shifted.setKeyframe(it.key(), key);
    }
    shifted.setEnabled(track.enabled());
    track = shifted;
}

} // namespace

namespace {

void mergeAdjacentMulticamCuts(QList<MulticamCut> &cuts)
{
    int i = 1;
    while (i < cuts.size()) {
        if (cuts.at(i).angle == cuts.at(i - 1).angle)
            cuts.removeAt(i);
        else
            ++i;
    }
}

int multicamCutIndexAtOrBefore(const QList<MulticamCut> &cuts, TimeUs timeUs)
{
    int index = 0;
    while (index + 1 < cuts.size() && cuts.at(index + 1).timeUs <= timeUs)
        ++index;
    return index;
}

} // namespace

MulticamSwitchResult applyMulticamSwitch(QList<MulticamCut> &cuts, TimeUs rangeStart, TimeUs rangeEnd,
                                         int angle, TimeUs atUs, int initialAngle, double fps)
{
    const TimeUs minUs = minClipDurationUs(fps);
    if (rangeEnd - rangeStart < minUs)
        return MulticamSwitchResult::OutOfRange;
    if (atUs < rangeStart || atUs >= rangeEnd)
        return MulticamSwitchResult::OutOfRange;

    if (cuts.isEmpty()) {
        if (angle == initialAngle)
            return MulticamSwitchResult::NoOp;
        if (atUs == rangeStart) {
            cuts.append(MulticamCut{rangeStart, angle});
            return MulticamSwitchResult::Applied;
        }
        if (atUs - rangeStart < minUs || rangeEnd - atUs < minUs)
            return MulticamSwitchResult::TooCloseToEdge;
        cuts.append(MulticamCut{rangeStart, initialAngle});
        cuts.append(MulticamCut{atUs, angle});
        return MulticamSwitchResult::Applied;
    }

    const int index = multicamCutIndexAtOrBefore(cuts, atUs);
    const TimeUs intervalStart = cuts.at(index).timeUs;
    const TimeUs intervalEnd = index + 1 < cuts.size() ? cuts.at(index + 1).timeUs : rangeEnd;
    if (cuts.at(index).angle == angle)
        return MulticamSwitchResult::NoOp;

    if (atUs == intervalStart) {
        cuts[index].angle = angle;
        mergeAdjacentMulticamCuts(cuts);
        return MulticamSwitchResult::Applied;
    }

    if (atUs - intervalStart < minUs || intervalEnd - atUs < minUs)
        return MulticamSwitchResult::TooCloseToEdge;

    cuts.insert(index + 1, MulticamCut{atUs, angle});
    mergeAdjacentMulticamCuts(cuts);
    return MulticamSwitchResult::Applied;
}

int multicamAngleAt(const QList<MulticamCut> &cuts, TimeUs rangeStart, TimeUs rangeEnd, TimeUs timeUs,
                    int uneditedAngle)
{
    if (timeUs < rangeStart || timeUs >= rangeEnd)
        return -1;
    if (cuts.isEmpty())
        return uneditedAngle;
    return cuts.at(multicamCutIndexAtOrBefore(cuts, timeUs)).angle;
}

QList<MulticamInterval> multicamIntervals(const QList<MulticamCut> &cuts, TimeUs rangeStart,
                                          TimeUs rangeEnd, int uneditedAngle)
{
    QList<MulticamInterval> out;
    if (rangeEnd <= rangeStart)
        return out;
    if (cuts.isEmpty()) {
        out.append(MulticamInterval{rangeStart, rangeEnd, uneditedAngle});
        return out;
    }
    for (int i = 0; i < cuts.size(); ++i) {
        const TimeUs end = i + 1 < cuts.size() ? cuts.at(i + 1).timeUs : rangeEnd;
        out.append(MulticamInterval{cuts.at(i).timeUs, end, cuts.at(i).angle});
    }
    return out;
}

namespace {

// Every clip-relative curve a clip can carry, in one place so the operations below cannot forget
// one. TextAnimator curves are deliberately absent: their key "times" are animation progress, not
// clip time, so moving them would distort the animation rather than move it.
template<typename Fn>
void forEachKeyframeTrack(Clip &clip, Fn &&fn)
{
    fn(clip.opacity);
    fn(clip.transformX);
    fn(clip.transformY);
    fn(clip.transformW);
    fn(clip.transformH);
    fn(clip.rotation);
    fn(clip.rotationX);
    fn(clip.rotationY);
    fn(clip.positionZ);
    fn(clip.perspective);
    fn(clip.anchorX);
    fn(clip.anchorY);
    fn(clip.anchorZ);
    fn(clip.volume);
    const auto visitMap = [&fn](QMap<QString, KeyframeTrack<double>> &tracks) {
        for (auto it = tracks.begin(); it != tracks.end(); ++it)
            fn(it.value());
    };
    for (Effect &effect : clip.effects)
        visitMap(effect.paramKeyframes);
    for (Effect &effect : clip.audioEffects)
        visitMap(effect.paramKeyframes);
    visitMap(clip.mask.keyframes);
    visitMap(clip.shapeStyle.keyframes);
    visitMap(clip.textStyle.keyframes);
    visitMap(clip.vector.keyframes);
    visitMap(clip.model3d.keyframes);
    visitMap(clip.pdf.keyframes);
    visitMap(clip.procedural.paramKeyframes);
}

} // namespace

void shiftClipKeyframes(Clip &clip, TimeUs delta)
{
    if (delta == 0)
        return;
    forEachKeyframeTrack(clip, [delta](KeyframeTrack<double> &track) { track.shiftBy(delta); });
}

void rebaseKeyframesForSplitTail(Clip &tail, TimeUs offset)
{
    if (offset <= 0)
        return;
    // Pin the value the curve holds at the cut before moving it, or the tail would start from
    // whichever key survived the shift and hold it flat — the animation would visibly change shape
    // at a cut that is supposed to be invisible.
    forEachKeyframeTrack(tail, [offset](KeyframeTrack<double> &track) {
        track.pinValueAt(offset);
        track.shiftBy(-offset);
    });
}

bool sliceClipToTimelineRange(const Clip &src, TimeUs start, TimeUs end, Clip &out, double fps)
{
    const TimeUs from = qMax(start, src.timelineStart);
    const TimeUs to = qMin(end, src.timelineEnd());
    if (to - from < minClipDurationUs(fps))
        return false;

    Clip work = src;
    if (from > work.timelineStart) {
        Clip tail;
        if (!splitClipAtOffset(work, tail, from - work.timelineStart))
            return false;
        work = tail;
    }
    if (work.timelineEnd() > to) {
        Clip discarded;
        if (!splitClipAtOffset(work, discarded, to - work.timelineStart))
            return false;
    }

    out = work;
    return true;
}

void scaleTrackValues(KeyframeTrack<double> &track, double factor)
{
    if (track.isEmpty() || qFuzzyCompare(factor, 1.0))
        return;
    KeyframeTrack<double> scaled;
    const QMap<TimeUs, Keyframe<double>> &values = track.keyframes();
    for (auto it = values.constBegin(); it != values.constEnd(); ++it) {
        Keyframe<double> key = it.value();
        key.value *= factor;
        key.inDy *= factor;
        key.outDy *= factor;
        scaled.setKeyframe(it.key(), key);
    }
    scaled.setEnabled(track.enabled());
    track = scaled;
}

namespace {

// A composite's box shows a whole canvas, so resizing the canvas resizes what it shows. Holding
// its content still means scaling the box with the canvas and moving its centre by the
// nested canvas's own centre shift `d`, carried through the box's scale, flip and rotation.
void rebaseCanvasBox(Clip &clip, int oldWidth, int oldHeight, int newWidth, int newHeight,
                     double originX, double originY)
{
    const double sx = double(newWidth) / oldWidth;
    const double sy = double(newHeight) / oldHeight;
    const double dx = originX + newWidth / 2.0 - oldWidth / 2.0;
    const double dy = originY + newHeight / 2.0 - oldHeight / 2.0;
    const auto delta = [&](TimeUs t) {
        const double w = clip.transformW.isEmpty() ? oldWidth : clip.transformW.evaluateAt(t);
        const double h = clip.transformH.isEmpty() ? oldHeight : clip.transformH.evaluateAt(t);
        const double a = qDegreesToRadians(clip.rotation.isEmpty() ? 0.0 : clip.rotation.evaluateAt(t));
        const double lx = (clip.flipH ? -1.0 : 1.0) * w / oldWidth * dx;
        const double ly = (clip.flipV ? -1.0 : 1.0) * h / oldHeight * dy;
        const double cx = std::cos(a) * lx - std::sin(a) * ly - originX;
        const double cy = std::sin(a) * lx + std::cos(a) * ly - originY;
        return QPointF(w / 2.0 * (1.0 - sx) + cx, h / 2.0 * (1.0 - sy) + cy);
    };

    QList<TimeUs> times;
    for (const KeyframeTrack<double> *track : {&clip.transformW, &clip.transformH, &clip.rotation}) {
        if (track->keyframes().size() > 1)
            times += track->keyframes().keys();
    }
    const auto rebase = [&](KeyframeTrack<double> &track, bool isX) {
        const auto pick = [isX](QPointF p) { return isX ? p.x() : p.y(); };
        KeyframeTrack<double> out;
        out.setEnabled(track.enabled());
        const QMap<TimeUs, Keyframe<double>> &values = track.keyframes();
        for (auto it = values.constBegin(); it != values.constEnd(); ++it) {
            Keyframe<double> key = it.value();
            key.value += pick(delta(it.key()));
            out.setKeyframe(it.key(), key);
        }
        for (TimeUs t : times) {
            if (!values.contains(t))
                out.setKeyframe(t, (values.isEmpty() ? 0.0 : track.evaluateAt(t)) + pick(delta(t)));
        }
        if (values.isEmpty() && times.isEmpty()) {
            const double v = pick(delta(0));
            if (!qFuzzyIsNull(v))
                out.setKeyframe(0, v);
        }
        track = out;
    };
    rebase(clip.transformX, true);
    rebase(clip.transformY, false);
    scaleTrackValues(clip.transformW, sx);
    scaleTrackValues(clip.transformH, sy);
}

} // namespace

bool clipBoxIsCanvasReferenced(const Clip &clip)
{
    return clip.type == ClipType::Composite
           || (clip.type == ClipType::Adjustment && clip.adjustmentKind == AdjustmentKind::Transform);
}

void rebaseClipLayout(Project &project, int oldWidth, int oldHeight, int newWidth, int newHeight,
                      double originX, double originY)
{
    project.forEachTrackList([&](QList<Track> &tracks) {
        for (Track &track : tracks) {
            if (track.type == TrackType::Audio)
                continue;
            for (Clip &clip : track.clips) {
                if (clip.type == ClipType::Audio)
                    continue;
                if (clipBoxIsCanvasReferenced(clip)) {
                    rebaseCanvasBox(clip, oldWidth, oldHeight, newWidth, newHeight, originX, originY);
                    continue;
                }
                bakeIfImplicit(clip.transformW, oldWidth);
                bakeIfImplicit(clip.transformH, oldHeight);
                shiftTrackValues(clip.transformX, originX, 0.0);
                shiftTrackValues(clip.transformY, originY, 0.0);
            }
        }
    });
}

} // namespace drift
