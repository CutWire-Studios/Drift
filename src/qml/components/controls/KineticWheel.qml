import QtQuick
import Drift

// Touchpad momentum for a Flickable. Qt Quick moves a Flickable 1:1 with a two-finger scroll and
// stops dead when the fingers lift; this takes over the touchpad gesture, tracks its speed and
// glides on after the lift.
//
// The glide is Firefox's desktop fling (gfx/layers/apz DesktopFlingPhysics + the apz.fling_*
// prefs): velocity decays exponentially, v *= (1 - friction)^ms, rather than Flickable.flick()'s
// constant deceleration, which is what gives it the long soft tail instead of a sudden stop.
//
// Only scroll-phased events are touched (touchpads on Wayland/X11). Mouse wheel notches carry no
// phase and are passed through to the Flickable as before. macOS adds its own momentum events,
// so it is left alone.
//
// Lives in the flickable's contentItem, behind the content and pinned over the viewport, so
// wheel-aware items in the content get the event first and this gets it before the Flickable.
// As a negative-z child of the Flickable itself it would come after: an item is offered an
// event before its negative-z children, so the Flickable scrolled the gesture natively,
// accepted it, and only the ends of the list ever reached this.
//
// The Flickable sees every event this rejects. ScrollBegin puts it into a drag anchored at the
// current position and only ScrollEnd takes it out, so both are always passed on, and once a
// gesture has scrolled here nothing else of it is: the Flickable would drag from its stale anchor
// and snap the content back. So a gesture hands off to an outer list only if it starts at the
// bound, not when it reaches it part-way.
MouseArea {
    id: root

    property Flickable flickable: null
    property bool vertical: true

    // Firefox defaults; velocities in px/ms.
    property real friction: 0.002               // apz.fling_friction
    property real stoppedThreshold: 0.01        // apz.fling_stopped_threshold
    property real minVelocity: 0.5              // apz.fling_min_velocity_threshold
    property int velocityWindow: 100            // apz.velocity_relevance_time_ms
    property real accelMinFlingVelocity: 1.5    // apz.fling_accel_min_fling_velocity

    property var _samples: []
    property real _velocity: 0
    // Velocity left over from a glide the new gesture interrupted, for fling acceleration.
    property real _carry: 0

    readonly property bool isKineticWheel: true
    // This wheel has moved the content since the last ScrollBegin.
    property bool _scrolled: false

    parent: flickable ? flickable.contentItem : null
    x: flickable ? flickable.contentX : 0
    y: flickable ? flickable.contentY : 0
    width: flickable ? flickable.width : 0
    height: flickable ? flickable.height : 0
    z: -1
    acceptedButtons: Qt.NoButton
    enabled: flickable !== null && Qt.platform.os !== "osx" && Qt.platform.os !== "ios"

    function _bounds() {
        const f = root.flickable
        const min = root.vertical ? f.originY - f.topMargin : f.originX - f.leftMargin
        const max = root.vertical
                  ? Math.max(min, f.originY + f.contentHeight + f.bottomMargin - f.height)
                  : Math.max(min, f.originX + f.contentWidth + f.rightMargin - f.width)
        return { min: min, max: max }
    }

    // Moves the content by delta (positive = towards the start, as pixelDelta). Returns false
    // when already pinned at that end.
    function _scrollBy(delta) {
        const f = root.flickable
        const b = _bounds()
        const pos = root.vertical ? f.contentY : f.contentX
        const next = Math.max(b.min, Math.min(b.max, pos - delta))
        if (next === pos)
            return false
        if (root.vertical)
            f.contentY = next
        else
            f.contentX = next
        _scrolled = true
        return true
    }

    // Whether an event this wheel can't use may be rejected. A two-axis flickable has one wheel
    // per scrollbar; the one above passes down to the other, the last one swallows.
    function _mayReject() {
        const c = root.flickable.contentItem.children
        let stale = false
        let below = false
        let seenSelf = false
        for (let i = 0; i < c.length; ++i) {
            if (c[i].isKineticWheel !== true)
                continue
            if (c[i] === root)
                seenSelf = true
            else if (!seenSelf)
                below = true
            if (c[i]._scrolled)
                stale = true
        }
        return !stale || below
    }

    function _stopGlide() {
        glide.stop()
        _velocity = 0
    }

    // Measured up to the last motion event, not the ScrollEnd: the end can arrive tens of ms after
    // the fingers' last movement, and counting that gap as travel time shrank the fling to nothing.
    function _trackedVelocity() {
        if (_samples.length < 2)
            return 0
        const last = _samples[_samples.length - 1].t
        if (Date.now() - last > root.velocityWindow)
            return 0
        const recent = _samples.filter(function(s) { return last - s.t <= root.velocityWindow })
        if (recent.length < 2)
            return 0
        // Each delta is travel since the previous event, so the first one predates the span.
        let sum = 0
        for (let i = 1; i < recent.length; ++i)
            sum += recent[i].d
        return sum / Math.max(8, last - recent[0].t)
    }

    FrameAnimation {
        id: glide
        onTriggered: {
            const ms = frameTime * 1000
            root._velocity *= Math.pow(1 - root.friction, ms)
            if (Math.abs(root._velocity) <= root.stoppedThreshold
                    || !root._scrollBy(root._velocity * ms))
                root._stopGlide()
        }
    }

    Connections {
        target: root.flickable
        // A touch drag or a scrollbar grab takes over.
        function onMovementStarted() { root._stopGlide() }
    }

    onWheel: (wheel) => {
        if (wheel.phase === Qt.NoScrollPhase || wheel.phase === Qt.ScrollMomentum
                || (wheel.modifiers & Qt.ControlModifier)) {
            root._stopGlide()
            wheel.accepted = false
            return
        }
        if (wheel.phase === Qt.ScrollBegin) {
            root._carry = glide.running ? root._velocity : 0
            root._stopGlide()
            root.flickable.cancelFlick()
            root._samples = []
            root._scrolled = false
            wheel.accepted = false
            return
        }
        if (wheel.phase === Qt.ScrollEnd) {
            let v = root._trackedVelocity()
            root._samples = []
            // Fling acceleration: a fast swipe in the same direction as a glide still running
            // adds to it, so repeated swipes cover a long list quickly.
            if (Math.abs(v) >= root.accelMinFlingVelocity && v * root._carry > 0)
                v += root._carry
            root._carry = 0
            if (Math.abs(v) > root.minVelocity) {
                root._velocity = v
                glide.start()
            }
            wheel.accepted = false
            return
        }
        const d = root.vertical ? wheel.pixelDelta.y : wheel.pixelDelta.x
        // A new gesture doesn't always open with ScrollBegin, and a glide left running would
        // fight fingers going the other way.
        // Zero-delta updates can trail the ScrollEnd, so only real movement stops a glide.
        if (d !== 0 && glide.running) {
            root._carry = root._velocity
            root._stopGlide()
        }
        if (d === 0 || !root._scrollBy(d)) {
            wheel.accepted = !root._mayReject()
            return
        }
        root._samples.push({ t: Date.now(), d: d })
        if (root._samples.length > 32)
            root._samples.shift()
    }

    Component.onDestruction: glide.stop()
}
