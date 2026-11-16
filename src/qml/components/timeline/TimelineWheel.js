.pragma library

// How one wheel or touchpad scroll moves the timeline. Plain numbers in and out, so
// tst_timelinewheel can check it without a window.
// e: { touchpad, ctrl, shift, pixelX, pixelY, angleX, angleY }
// wheelPansTime: Settings → Timeline → Mouse wheel scrolls → Time (invertTimelineScroll); wheels only
// tracksOverflow: the tracks are taller than the viewport
// Returns { zoom, x, y }: a zoom factor (1 = none) and a pan in panTimelineBy()'s sign.
function route(e, wheelPansTime, tracksOverflow) {
    // Mice report a pixel delta too on X11 (xf86-input-libinput) and macOS, so only a
    // touchpad's pixel delta is used. Wheels pan by angle: one notch (120) = 120 px.
    const usePixels = e.touchpad && (e.pixelX !== 0 || e.pixelY !== 0)
    const dx = usePixels ? e.pixelX : e.angleX
    const dy = usePixels ? e.pixelY : e.angleY

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
