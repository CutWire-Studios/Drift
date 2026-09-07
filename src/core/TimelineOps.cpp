#include "TimelineOps.h"

#include <QUuid>

#include <algorithm>

namespace drift {

TimeUs snapTime(const Project &project, TimeUs time, bool snapEnabled, TimeUs playheadUs,
                const QList<TimeUs> &extraTargets)
{
    if (!snapEnabled)
        return qMax<TimeUs>(0, time);

    QList<TimeUs> targets = {0, playheadUs};
    for (const Track &track : project.tracks()) {
        for (const Clip &clip : track.clips) {
            targets.append(clip.timelineStart);
            targets.append(clip.timelineEnd());
        }
    }
    targets.append(extraTargets);

    TimeUs best = time;
    TimeUs bestDistance = kSnapThresholdUs;
    for (TimeUs target : targets) {
        const TimeUs distance = qAbs(target - time);
        if (distance < bestDistance) {
            bestDistance = distance;
            best = target;
        }
    }

    return qMax<TimeUs>(0, best);
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
        return TrackType::Shape;
    case ClipType::Adjustment:
        return TrackType::Adjustment;
    case ClipType::Video:
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
                         TimeUs durationUs)
{
    if (parentIndex < 0 || parentIndex >= project.tracks().size())
        return -1;
    // A lane addresses its parent by id, so the parent needs one before it can be pointed at.
    project.ensureTrackIds();

    // Lanes nest in the tracks that carry clips. Nesting one inside another lane would give it
    // two scopes at once.
    if (project.tracks().at(parentIndex).isAdjustment())
        return -1;

    for (const int laneIndex : adjustmentLaneIndexes(project, parentIndex)) {
        const Track &lane = project.tracks().at(laneIndex);
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

QList<LaneMask> laneMasksAt(const Project &project, int trackIndex, TimeUs timelineUs)
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
            if (!adjustment.mask.contributes())
                continue;
            result.append(LaneMask{adjustment.mask, adjustment.id});
        }
    }
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

    Clip adjustment;
    adjustment.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    adjustment.type = ClipType::Adjustment;
    adjustment.adjustmentKind = AdjustmentKind::Mask;
    // Pinned: syncLinkedAdjustments mirrors the clip's span onto it through every
    // move/trim/split/delete, so the mask cannot drift off the shot it was made for.
    adjustment.linkedClipId = source.id;
    adjustment.timelineStart = source.timelineStart;
    adjustment.timelineDuration = source.timelineDuration;
    adjustment.srcIn = 0;
    adjustment.srcOut = source.timelineDuration;
    adjustment.mask = mask;

    const int laneIndex = ensureAdjustmentLane(project, trackIndex, AdjustmentKind::Mask,
                                               adjustment.timelineStart,
                                               adjustment.timelineDuration);
    if (laneIndex < 0)
        return;
    project.tracks()[laneIndex].clips.append(adjustment);
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
        return kImageClipDurationUs;

    if (asset->kind == MediaKind::Image)
        return kImageClipDurationUs;

    if (asset->durationUs > 0)
        return asset->durationUs;

    return kImageClipDurationUs;
}

TimeUs sourceDurationForClip(const Project &project, const Clip &clip)
{
    if (!clip.assetId.isEmpty()) {
        if (const MediaAsset *asset = project.asset(clip.assetId)) {
            if (asset->durationUs > 0)
                return asset->durationUs;
        }
    }

    if (clip.type == ClipType::Image || clip.type == ClipType::Shape || clip.type == ClipType::Adjustment)
        return kImageClipDurationUs;

    return qMax(clip.srcOut, clip.timelineDuration);
}

bool splitClipAtOffset(Clip &head, Clip &tail, TimeUs offset)
{
    if (offset < kMinClipDurationUs || head.timelineDuration - offset < kMinClipDurationUs)
        return false;

    const TimeUs sourceSpan = head.srcOut - head.srcIn;
    const TimeUs sourceOffset =
        head.hasSpeedCurve() ? head.speedCurve.sourceOffsetForTimelineOffset(offset, sourceSpan)
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

    if (head.reverse) {
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
            head.name = subtitleClipName(head.subtitleCues);
            tail.name = subtitleClipName(tail.subtitleCues);
        }
    }

    head.timelineDuration = offset;
    return true;
}

void retargetClipToSource(Clip &dst, const Clip &src, TimeUs srcMediaDurationUs)
{
    // Media identity.
    dst.assetId = src.assetId;
    dst.path = src.path;
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

    if (left.reverse)
        return left.srcIn == right.srcOut;
    return left.srcOut == right.srcIn;
}

Clip mergeClips(const Clip &left, const Clip &right)
{
    Clip out = left;
    out.timelineDuration = left.timelineDuration + right.timelineDuration;
    if (left.reverse) {
        out.srcIn = right.srcIn;
        out.srcOut = left.srcOut;
    } else {
        out.srcOut = right.srcOut;
    }
    out.fadeOutUs = right.fadeOutUs;
    return out;
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
    dst.fadeInUs = src.fadeInUs;
    dst.fadeOutUs = src.fadeOutUs;
    dst.fadeCurve = src.fadeCurve;
    dst.fadeShape = src.fadeShape;
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
                                         int angle, TimeUs atUs, int initialAngle)
{
    if (rangeEnd - rangeStart < kMinClipDurationUs)
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
        if (atUs - rangeStart < kMinClipDurationUs || rangeEnd - atUs < kMinClipDurationUs)
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

    if (atUs - intervalStart < kMinClipDurationUs || intervalEnd - atUs < kMinClipDurationUs)
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

bool sliceClipToTimelineRange(const Clip &src, TimeUs start, TimeUs end, Clip &out)
{
    const TimeUs from = qMax(start, src.timelineStart);
    const TimeUs to = qMin(end, src.timelineEnd());
    if (to - from < kMinClipDurationUs)
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

void rebaseClipLayout(Project &project, int oldWidth, int oldHeight, double originX, double originY)
{
    for (Track &track : project.tracks()) {
        if (track.type == TrackType::Audio)
            continue;
        for (Clip &clip : track.clips) {
            if (clip.type == ClipType::Audio)
                continue;
            bakeIfImplicit(clip.transformW, oldWidth);
            bakeIfImplicit(clip.transformH, oldHeight);
            shiftTrackValues(clip.transformX, originX, 0.0);
            shiftTrackValues(clip.transformY, originY, 0.0);
        }
    }
}

} // namespace drift
