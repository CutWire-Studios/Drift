#pragma once

#include "core/animation/Keyframe.h"
#include "core/base/Time.h"
#include "core/input/InputLog.h"

#include <QPointF>
#include <QSizeF>

namespace drift {

// Turns a screen recording's clicks into a keyframed push-in that follows them.
//
// This is what makes a raw screen capture watchable: a full desktop at 1080p shows a menu item as
// about twelve pixels of text, and the viewer cannot see what was clicked. Zooming to each click
// is the edit everyone makes by hand afterwards, keyframe by keyframe.
//
// The result is ordinary keyframes on the clip's own transform, not a live effect. So every zoom
// can be retimed, softened or deleted in the graph editor like anything else — which matters,
// because the one click the user wants held longer is not knowable from the log.
//
// Clicks in quick succession become one sustained zoom that pans between them rather than a zoom
// per click: zooming out and back in over a half-second burst of clicks is the "drunk camera" that
// makes auto-zoom unwatchable, and is why it is usually switched off.
struct AutoZoomOptions
{
    // Scale at the peak. 1.6 reads a menu comfortably at 1080p without the image going soft.
    double zoom = 1.6;
    // How long the push in and out take. Fast enough to keep up, slow enough not to lurch.
    TimeUs rampUs = 400'000;
    // How long to stay in after the last click of a group, so the result of the click is seen.
    TimeUs holdUs = 900'000;
    // Clicks closer together than this belong to one zoom, panning between them.
    TimeUs groupWithinUs = 1'500'000;

    // The clip's frame at 100% (Clip::frameW/frameH), in canvas pixels. Positions are computed in
    // this space because the anchor is.
    QSizeF frame;
    // Where the clip's anchor rests at scale 1 — the canvas centre for a full-frame recording.
    QPointF restPosition;
};

// The tracks to write onto the clip. Empty when there was nothing to zoom to.
//
// Only position and scale: the anchor is deliberately left alone. Its default is the frame centre,
// and keying it instead would move the pivot of every rotation the user later adds.
struct AutoZoomTracks
{
    KeyframeTrack<double> positionX;
    KeyframeTrack<double> positionY;
    KeyframeTrack<double> scaleX;
    KeyframeTrack<double> scaleY;

    bool isEmpty() const { return scaleX.isEmpty(); }
};

// `clicks` are press events with normalized positions, in time order; times are clip-relative.
// Anything outside [0, clipDurationUs) is ignored, which is what makes this safe to run on a clip
// whose head has already been trimmed.
AutoZoomTracks autoZoomFromClicks(const QList<InputEvent> &clicks, TimeUs clipDurationUs,
                                  const AutoZoomOptions &options);

} // namespace drift
