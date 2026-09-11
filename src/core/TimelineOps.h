#pragma once

#include "Project.h"
#include "Time.h"

#include <QSet>

namespace drift {

constexpr TimeUs kImageClipDurationUs = 5 * kUsPerSecond;
constexpr TimeUs kTextClipDurationUs = 5 * kUsPerSecond;
constexpr TimeUs kSubtitleClipDurationUs = 30 * kUsPerSecond;
constexpr TimeUs kMinClipDurationUs = kUsPerSecond / 10;
constexpr TimeUs kSnapThresholdUs = 150'000;

// `extraTargets` are additional snap positions supplied by the caller — currently the
// detected beat grid, which is analysis state rather than something the project stores.
// Core never learns what a beat is; it just snaps to whatever times it is handed.
TimeUs snapTime(const Project &project, TimeUs time, bool snapEnabled, TimeUs playheadUs,
                const QList<TimeUs> &extraTargets = {});

TimeUs resolveClipStart(const Project &project, const Track &track, int excludeClipIndex,
                        TimeUs desiredStart, TimeUs duration, bool snapEnabled, TimeUs playheadUs,
                        const QList<TimeUs> &extraTargets = {});

// Push `desiredStart` forward until [start, start+duration) does not intersect any clip on
// `track` whose id is not in `excludeIds`. Abutting (end == next start) is allowed.
TimeUs clampClipStartNoOverlap(const Track &track, const QSet<QString> &excludeIds,
                               TimeUs desiredStart, TimeUs duration);

// Cap a left-edge extend so it cannot cross into clips that currently end at/before
// `currentStart`. Already-overlapping clips are ignored (toggle must not un-overlap).
TimeUs clampClipStartAgainstLeftNeighbors(const Track &track, const QSet<QString> &excludeIds,
                                          TimeUs currentStart, TimeUs desiredStart);

// Cap a right-edge extend so it cannot cross into clips that currently start at/after
// `currentEnd`. Already-overlapping clips are ignored.
TimeUs clampClipEndNoOverlap(const Track &track, const QSet<QString> &excludeIds, TimeUs currentEnd,
                             TimeUs desiredEnd);

struct ClipRef
{
    int trackIndex = -1;
    int clipIndex = -1;
};

TrackType trackTypeForClipType(ClipType type);

int defaultTrackForClipType(const Project &project, ClipType type);

// Indices of the nested adjustment lanes attached to `parentIndex`, in track order (topmost
// first, which is also the order their effects apply in). Lanes are kept immediately above their
// parent, but `parentTrackId` is what actually binds them, so this scans by id rather than
// trusting the layout to be intact.
QList<int> adjustmentLaneIndexes(const Project &project, int parentIndex);

// Inverse: the track a lane is nested in, or -1 if `laneIndex` is not a lane.
int adjustmentLaneParentIndex(const Project &project, int laneIndex);

// Index of a nested lane on `parentIndex` that holds `kind` and has no clip overlapping
// [startUs, startUs + durationUs), minting one when every existing lane is occupied over that
// span. -1 when `parentIndex` is out of range or is itself an adjustment track (nesting a lane
// inside a lane would give it two scopes at once).
//
// A lane holds one kind: a row mixing video and audio adjustments would have no unambiguous
// colour or inspector. An empty lane takes anything. INSERTS TRACKS.
int ensureAdjustmentLane(Project &project, int parentIndex, AdjustmentKind kind, TimeUs startUs,
                         TimeUs durationUs);

// A mask paired with the id of the adjustment clip carrying it. The compositor needs the id: the
// reader pool keys its cursor by stream id, so a mask's media must not decode under the host
// clip's id or the two fight over one cursor.
struct LaneMask
{
    Mask mask;
    QString adjustmentId;
};

// Every Mask-kind lane adjustment on `trackIndex` covering `timelineUs`, in stack order — lane
// order first (lanes are listed parent-first, which is the order they were created in), then clip
// order within a lane.
QList<LaneMask> laneMasksAt(const Project &project, int trackIndex, TimeUs timelineUs);

// The Mask-kind adjustments pinned to clips[clipIndex], as (trackIndex, clipIndex) pairs.
QList<ClipRef> linkedMaskAdjustments(const Project &project, int trackIndex, int clipIndex);

// Pin `mask` to clips[clipIndex] as a Mask-kind adjustment on one of `trackIndex`'s lanes,
// replacing whatever mask was pinned there. A mask with shape None removes the pin instead.
//
// This is the only way a mask reaches a media clip: Clip::mask is meaningful on adjustment clips
// alone. INSERTS TRACKS, so any index held across the call goes stale.
void setLinkedMask(Project &project, int trackIndex, int clipIndex, const Mask &mask);

// Pin `mask` as an *additional* Mask-kind adjustment on clips[clipIndex], stacking on whatever is
// already pinned there instead of replacing it — which is what dropping a second mask onto a clip
// means. Returns the new adjustment's position, or a null ClipRef when the mask would not
// contribute. INSERTS TRACKS.
ClipRef addLinkedMask(Project &project, int trackIndex, int clipIndex, const Mask &mask);

// An unlinked Mask adjustment on one of `trackIndex`'s lanes, spanning [startUs, +durationUs).
// Pinned to nothing, so its edges stay draggable and it masks whatever the track shows over that
// span rather than one clip. `trackIndex` must be a track that can carry lanes. INSERTS TRACKS.
ClipRef addLaneMask(Project &project, int trackIndex, const Mask &mask, TimeUs startUs,
                    TimeUs durationUs);

// Drop the mask adjustments pinned to clips[clipIndex]. `mediaOnly` keeps parametric masks, which
// is what a multicam switch wants: a matte describes the camera it was traced from, but a
// geometric mask is treatment like the transform.
void clearLinkedMasks(Project &project, int trackIndex, int clipIndex, bool mediaOnly = false);

// Moves a mask still sitting directly on a media clip onto an adjustment linked to it. The v4->v5
// counterpart of hoistClipEffectsToAdjustmentLanes, and load-only: unlike effects, nothing writes
// Clip::mask on a media clip any more. INSERTS TRACKS.
void migrateClipMasksToAdjustmentLanes(Project &project);

// Moves any effect stack still sitting directly on a media clip onto an adjustment linked to it,
// in one of its track's nested lanes, minting lanes as needed.
//
// This is the single invariant the whole adjustment model rests on: a stack lives on an
// adjustment, never on a clip, so there are never two places holding one that can disagree about
// what the clip has. Running it centrally is what lets effect templates, the Premiere importer
// and project load all keep writing clip.effects the straightforward way.
//
// Idempotent, and a no-op once no clip holds a stack. INSERTS TRACKS, so any index held across
// the call goes stale — callers address tracks by id around it.
void hoistClipEffectsToAdjustmentLanes(Project &project);

// Moves any ClipType::Adjustment clip still sitting on a video track onto a standalone
// adjustment track at the same depth, which is the z-position it already had — so this is
// visually lossless.
//
// Shared by v3 project load and the Premiere importer, which both produce the older shape where
// an adjustment was just a clip squatting on a video track. INSERTS TRACKS.
void liftAdjustmentClipsToOwnTracks(Project &project);

int ensureTrackForClipType(Project &project, ClipType type, bool insertAtTop = false);

// Always prepends a fresh track (multiple tracks of the same type are allowed).
int insertTrackAtTopForClipType(Project &project, ClipType type);

// Inserts a fresh track directly above `trackIndex` (index 0 is the topmost track), pushing that
// track and everything below it down one. Returns the new track's index.
int insertTrackAboveForClipType(Project &project, int trackIndex, ClipType type);

TimeUs clipDurationForAsset(const MediaAsset *asset);

TimeUs sourceDurationForClip(const Project &project, const Clip &clip);

// Split `head` at `offset` from its timeline start into head + tail (same reverse/speed).
// Caller assigns `tail.id`. Returns false if the offset is too close to either end.
bool splitClipAtOffset(Clip &head, Clip &tail, TimeUs offset);

// Repoint `dst` at the media `src` carries while keeping dst's timeline placement. The source
// time is read from `src` at dst's timeline start, so the two stay in timeline sync — which is
// what makes a multicam switch land on the frame the angle was showing at that moment.
//
// dst's transform, effects, fades and opacity are deliberately left alone: switching camera
// changes which pixels arrive, not the treatment applied to them. `srcMediaDurationUs` bounds
// the new source range; dst's timeline duration shrinks if the media runs out before its slot
// does.
void retargetClipToSource(Clip &dst, const Clip &src, TimeUs srcMediaDurationUs);

// True when `left` ends where `right` begins, shares media + reverse/speed, and source ranges abut.
bool clipsCanMerge(const Clip &left, const Clip &right);

// Merge abutting clips. Keeps left transforms/effects; takes right's fade-out.
Clip mergeClips(const Clip &left, const Clip &right);

// All clips sharing `clip.linkId` (excluding `clip` itself).
QList<ClipRef> linkedPartners(const Project &project, const Clip &clip);

// Mirror timeline/source timing from a linked source clip.
void syncLinkedTiming(Clip &dst, const Clip &src);

// Copy link fields when splitting a linked clip so both halves stay paired.
// Returns the link id assigned to `tail` (empty when `head` was not linked).
QString assignSplitLinkIds(Clip &head, Clip &tail);

// Prepares every clip for a canvas resize from `oldWidth`x`oldHeight`.
//
// Clips with no transformW/transformH keyframes fall back to the project size at
// composite time, so they would silently rescale themselves to the new canvas.
// This freezes that implicit size into explicit values first, then shifts stored
// positions by (-originX, -originY) so the region the user framed stays put.
// Clips are left visually stationary; whatever falls outside the new canvas is
// simply clipped away by the compositor.
void rebaseClipLayout(Project &project, int oldWidth, int oldHeight, double originX, double originY);

// A punch on a staged multicam assignment: from `timeUs` until the next cut (or the
// session end), this camera is the one that stays. `cuts` is empty while the session is
// still unedited — every camera stacked, `initialAngle` showing as program.
struct MulticamCut
{
    TimeUs timeUs = 0;
    int angle = 0;
};

struct MulticamInterval
{
    TimeUs startUs = 0;
    TimeUs endUs = 0;
    int angle = 0;
};

enum class MulticamSwitchResult { Applied, NoOp, OutOfRange, TooCloseToEdge };

// Punch `angle` at `atUs` inside `[rangeStart, rangeEnd)`. Empty `cuts` is the unedited
// stack; `initialAngle` is the topmost camera, which is program until the first punch.
MulticamSwitchResult applyMulticamSwitch(QList<MulticamCut> &cuts, TimeUs rangeStart, TimeUs rangeEnd,
                                         int angle, TimeUs atUs, int initialAngle);

int multicamAngleAt(const QList<MulticamCut> &cuts, TimeUs rangeStart, TimeUs rangeEnd, TimeUs timeUs,
                    int uneditedAngle);

QList<MulticamInterval> multicamIntervals(const QList<MulticamCut> &cuts, TimeUs rangeStart,
                                          TimeUs rangeEnd, int uneditedAngle);

// Intersection of `src` with `[start, end)`. False when that span is empty or shorter than
// kMinClipDurationUs. `out` keeps `src`'s id; the caller mints a new one if it needs one.
bool sliceClipToTimelineRange(const Clip &src, TimeUs start, TimeUs end, Clip &out);

} // namespace drift
