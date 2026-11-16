.pragma library

// A plain-value view of a QML WheelEvent, for route()/delta(). touchPadType is
// PointerDevice.TouchPad: a .pragma library can't import QtQuick's enums, so the caller passes it.
// Touchpads are told apart by scroll phase on Wayland and macOS. On X11 there are no phases, so
// the device type is used; it depends on the X stack (XI 2.4 gesture class, xorg >= 21.1 with
// libinput >= 1.2), and without it the touchpad is treated as a wheel. The device type is not
// used elsewhere: Qt's cocoa plugin retypes the shared pointing device to TouchPad after the
// first trackpad scroll, so a later USB wheel would look like a touchpad.
function input(wheel, touchPadType) {
    return {
        "touchpad": wheel.phase !== Qt.NoScrollPhase
                    || (Qt.platform.pluginName === "xcb" && wheel.device !== null
                        && wheel.device.type === touchPadType),
        "ctrl": (wheel.modifiers & Qt.ControlModifier) !== 0,
        "shift": (wheel.modifiers & Qt.ShiftModifier) !== 0,
        "alt": (wheel.modifiers & Qt.AltModifier) !== 0,
        "pixelX": wheel.pixelDelta.x, "pixelY": wheel.pixelDelta.y,
        "angleX": wheel.angleDelta.x, "angleY": wheel.angleDelta.y
    }
}

// How far one event scrolls: a touchpad's pixels, else the wheel's angle x wheelScale
// (1 = 120 px per notch). Mice carry a pixel delta too on X11 and macOS, so only a touchpad's counts.
function usesPixels(e) {
    return e.touchpad && (e.pixelX !== 0 || e.pixelY !== 0)
}

function delta(e, wheelScale) {
    if (usesPixels(e))
        return { "x": e.pixelX, "y": e.pixelY }
    return { "x": e.angleX * wheelScale, "y": e.angleY * wheelScale }
}

// The one-axis amount of a wheel turn: vertical, else horizontal (Qt turns the wheel horizontal
// while Alt is held on X11 and Windows, and macOS does with Shift).
function oneAxis(angleX, angleY) {
    return angleY !== 0 ? angleY : angleX
}

// Whole notches (120 units) in accum + delta, and the remainder to keep for the next event.
// A high-resolution wheel sends one notch as several smaller events. Turning the other way
// drops the remainder, so a reversal takes one notch, not two.
function notches(accum, delta) {
    const total = (accum * delta < 0 ? 0 : accum) + delta
    const steps = Math.trunc(total / 120)
    return { "steps": steps, "rest": total - steps * 120 }
}

// How one wheel or touchpad scroll moves the timeline. Plain numbers in and out, so
// tst_timelinewheel can check it without a window.
// e: { touchpad, ctrl, shift, pixelX, pixelY, angleX, angleY }
// wheelPansTime: Settings → Timeline → Mouse wheel scrolls → Time (invertTimelineScroll); wheels only
// tracksOverflow: the tracks are taller than the viewport
// Returns { zoom, x, y }: a zoom factor (1 = none) and a pan in panTimelineBy()'s sign.
function route(e, wheelPansTime, tracksOverflow) {
    // Mice report a pixel delta too on X11 (xf86-input-libinput) and macOS, so only a
    // touchpad's pixel delta is used. Wheels pan by angle: one notch (120) = 120 px.
    const usePixels = usesPixels(e)
    const d = delta(e, 1)
    const dx = d.x
    const dy = d.y

    if (e.ctrl) {
        if (dy === 0)
            return { "zoom": 1, "x": 0, "y": 0 }
        // Proportional, so a high-resolution wheel's partial notches add up to one step.
        return { "zoom": usePixels ? Math.pow(1.0015, dy) : Math.pow(1.15, dy / 120), "x": 0, "y": 0 }
    }

    // Motion that goes onto one axis follows the larger component, so a diagonal swipe does
    // not cancel itself out. macOS already turns Shift+wheel into dx.
    const one = Math.abs(dy) >= Math.abs(dx) ? dy : dx
    // The setting is for mouse wheels; a touchpad keeps natural two-axis scrolling.
    const pansTime = wheelPansTime && !e.touchpad
    // Vertical motion scrolls the tracks by default and pans time with "Time" set; Shift swaps.
    if (pansTime !== e.shift)
        return { "zoom": 1, "x": one, "y": 0 }
    if (e.shift)
        return { "zoom": 1, "x": 0, "y": one }
    // Tracks that overflow scroll along both axes. When they fit there is nothing to scroll
    // vertically: a wheel's vertical motion pans time instead (dominant axis), while a touchpad
    // keeps natural scrolling and only its horizontal motion pans time.
    if (tracksOverflow)
        return { "zoom": 1, "x": dx, "y": dy }
    return { "zoom": 1, "x": e.touchpad ? dx : one, "y": 0 }
}
