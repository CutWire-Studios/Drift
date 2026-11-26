#pragma once

#include "core/base/Time.h"
#include "core/input/InputLog.h"

#include <QList>
#include <QPointF>

namespace drift {

// Finds the places a cursor settled, so a screen recording can be zoomed even where the clicks
// themselves cannot be recorded.
//
// This exists because of a platform limit rather than a preference. A Wayland compositor will not
// let an application observe input it did not receive itself — the RemoteDesktop portal only
// *injects* input, there is no capture side — and /dev/input is root-owned. What the screen-share
// portal does hand over is the cursor's position, as per-frame metadata on the stream. So on
// Wayland the position is knowable and the button is not.
//
// A settled cursor is a good stand-in: people move the mouse to a thing, stop, and then click it.
// The stop is where attention went, and it happens slightly before the click, which is the moment
// a zoom wants to be arriving anyway.
struct CursorSample
{
    TimeUs timeUs = 0;
    QPointF position; // normalized to the recorded frame (0..1, top-left origin)
};

struct CursorDwellOptions
{
    // How long the cursor must stay put to count as settled. Below about a third of a second this
    // fires on the pauses inside one ordinary gesture.
    TimeUs minDwellUs = 400'000;
    // How far it may drift and still count as still, normalized to the frame's width. A hand
    // resting on a mouse moves a pixel or two.
    double moveTolerance = 0.012;
    // Two dwells closer than this collapse to one. Without it a slow drag emits a dwell every
    // frame it happens to pause.
    TimeUs minGapUs = 900'000;
};

// The settled points, as Press-kind events in time order, so autoZoomFromClicks consumes them
// unchanged. The time reported is when the cursor *arrived*, not when it left: that is the instant
// the zoom should be centred on.
QList<InputEvent> cursorDwellPoints(const QList<CursorSample> &samples,
                                    const CursorDwellOptions &options = {});

} // namespace drift
