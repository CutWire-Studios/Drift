#pragma once

#include "engine/face/tracking/FaceLandmarker.h"
#include "core/base/Time.h"

#include <QList>
#include <QMap>
#include <QString>
#include <QVariant>

#include <memory>

namespace drift {

// One baked frame: a slot per tracked face, holding a gap where that face was not found.
struct FaceTrackFrame
{
    QList<FaceAnchors> faces;
};

// A clip's face detection, baked once and replayed at composite time. This is the face equivalent
// of a segmentation matte: MatteWriter stores per-frame coverage as a video, this stores per-frame
// anchors as a small JSON sidecar.
struct FaceTrack
{
    int fps = 0;
    TimeUs startSrcUs = 0; // source time of frame 0, for diagnostics
    // Height / width of the frames the scan ran on, which is the space FaceAnchors normalizes
    // lengths into (see applyFaceUniforms). Anything converting an anchor back to a per-axis
    // measure needs it, and re-deriving it from the asset means reconstructing the display-matrix
    // rotation and whichever file the clip actually decoded from. 0 in sidecars written before
    // this was recorded; callers fall back to the host clip's own aspect there.
    double frameAspect = 0.0;
    QList<FaceTrackFrame> frames;

    bool isEmpty() const { return frames.isEmpty(); }

    // `relativeUs` is source time measured from the start of the track, i.e. the caller has
    // already subtracted Clip::faceTrackSrcOffsetUs.
    //
    // Interpolates between the two baked frames bracketing the request. Source time rarely lands
    // exactly on the bake grid once speed or reverse is involved, and snapping to the nearest
    // frame makes the warp visibly stutter against smooth motion. A slot is only valid when both
    // neighbours have it, so a face blinking in or out never produces a half-interpolated ghost.
    FaceAnchors sample(TimeUs relativeUs, int faceIndex) const;

    // Every slot at one instant. Compositing samples once per clip per frame and hands the result
    // to whichever effect path runs, so the CPU and GPU compositors cannot drift apart.
    QList<FaceAnchors> sampleAll(TimeUs relativeUs) const;
};

// Raw per-frame landmarks jitter by a pixel or two even on a still head, and a bulge anchored to a
// jittering point visibly boils. A short symmetric average over neighbouring frames costs nothing
// at bake time and is what makes the warps sit still.
//
// Only runs across frames where the slot is continuously valid, so smoothing never drags an anchor
// across the gap where a face left and came back. Contours and pose are gated the same way on
// their own presence flags, so a partially re-scanned track cannot average a present contour with
// an absent one.
//
// Lives beside FaceTrack::sample rather than in the app layer because the two have to agree on
// every convention these fields carry — the quaternion sign, the angle seam, the contour length.
void smoothFaceTrack(FaceTrack *track, int radius = 2);

// Merges one frame's anchors into an effect's parameter map as u_face* uniforms, consuming the
// package's own "faceIndex" parameter to pick a slot.
//
// Positions stay in uv. Lengths and the angle are in "width-normalized" space (uv with y scaled by
// the frame's aspect), because a radius has to mean the same thing along both axes; shaders rebuild
// that space from u_resolution.
//
// With no anchors for the chosen slot this sets u_faceValid to 0, which every face shader treats as
// pass-through — that is what makes an un-scanned clip render untouched rather than broken.
void applyFaceUniforms(QMap<QString, QVariant> *parameters,
                       const QList<FaceAnchors> &faceSlots);

// <AppDataLocation>/facetracks, created on demand. Mirrors matteCacheDir().
QString faceTrackCacheDir();
QString newFaceTrackPath();

// Where a face-swap source photo's landmarks live: <AppDataLocation>/faceswapsources/<key>.json.
//
// A still photo's landmarks are just a one-frame FaceTrack, so the swap reuses this whole file
// rather than inventing a second sidecar format — same quantized mesh blob, same writer, same
// cached reader. `fps` is 1 and there is exactly one frame; callers read frames[0] directly
// instead of sampling.
//
// Keyed on the photo's path, modification time and size, matching every other derived-asset
// cache here. Replacing the photo therefore re-ingests rather than serving stale landmarks, and
// moving it re-ingests too — which is cheap, and what the project-open sweep already handles.
QString faceSwapSourcePath(const QString &photoPath);

// Writes via a ".part" file and renames, so a cancelled or crashed bake never leaves a file that
// looks complete.
bool writeFaceTrack(const QString &path, const FaceTrack &track, QString *errorOut);
bool readFaceTrack(const QString &path, FaceTrack *out, QString *errorOut);

// Parsed once and kept, keyed by path and modification time. This is what the compositor calls on
// every frame, so it must not touch the disk in the steady state. Null when the file is missing or
// malformed. Thread-safe.
std::shared_ptr<const FaceTrack> loadFaceTrackCached(const QString &path);

} // namespace drift
