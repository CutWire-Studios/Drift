#pragma once

#include "core/base/Time.h"
#include "core/effects/Mask.h"
#include "engine/face/tracking/FaceTrack.h"

#include <QList>
#include <QPair>

namespace drift {

// Turns a baked FaceTrack into ordinary keyframed masks, which is what makes "blur every face"
// one action: a "requires": "mask" effect such as Privacy Blur reads the clip's folded mask
// stack, so covering the faces is a matter of producing one mask per face that follows it.
//
// Nothing here is live — the keys are baked, so the result is editable afterwards like any mask
// the user drew. That is deliberate: a detector miss is then a keyframe to drag rather than a
// black box to re-run.
struct FaceMaskOptions
{
    // Host frame height / width — the aspect of the frames the scan ran on. FaceAnchors stores
    // lengths in "width-normalized" space (uv with y scaled by this), while a Mask stores w and h
    // each normalized to its own axis, so h has to be divided back out by it.
    double frameAspect = 1.0;
    // Scale on the detected face oval. The oval traces the skin boundary, which leaves hair, ears
    // and jaw edge outside it; a blur that stops there still identifies the person.
    double padding = 1.4;
    // Feather on the mask edge, in frame pixels. A hard-edged blur patch reads as a sticker.
    double feather = 24.0;
};

// The number of face slots the track carries, i.e. the most faces baked into any one frame.
// Slots are stable across frames (the landmarker seeds each frame's ROI from the last), so this
// is the count of people to mask rather than a per-frame maximum that moves around.
int faceSlotCount(const FaceTrack &track);

// One Ellipse mask following slot `faceIndex`.
//
// `samples` pairs each mask-relative time to key with the face-track-relative source time to
// sample there. The caller owns that mapping because only it knows the host clip's trim, speed
// and reverse; this keeps the geometry in one place and the time model in the other.
//
// Frames where the slot has no face collapse the mask to zero size rather than dropping out, so
// the coverage simply closes over one frame and the effect stops applying — a mask cannot be
// disabled per frame, and holding the last position would blur an empty patch of frame.
//
// Returns a mask with shape None when the slot never resolves, which addLinkedMask rejects.
Mask faceMask(const FaceTrack &track, int faceIndex, const QList<QPair<TimeUs, TimeUs>> &samples,
              const FaceMaskOptions &options);

} // namespace drift
