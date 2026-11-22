import QtQuick
import Drift

// Zoom and pan of a preview viewport: Ctrl+wheel about the cursor, middle-drag, and two-finger
// pinch. It takes only the middle button and touch, so left-drags still reach what is above it.
// With `viewport` set it applies the moves itself; the signals let a caller that needs more (a
// 3D camera, a haptic tick) act on the same gestures instead.
Item {
    id: nav

    // Anything with zoomAt(x, y, factor), panX and panY, such as PreviewViewport.
    property var viewport
    property bool autoApply: true
    property bool pinchEnabled: true
    property bool wheelZoom: true
    property bool middlePan: true
    property int pinchDevices: PointerDevice.TouchScreen
    property real wheelStep: 1.15

    readonly property bool pinching: pinch.active

    signal zoomed(real x, real y, real factor)
    // Each pinch step pans by the centroid's move, zero or not; a middle-drag only on movement.
    signal panned(real dx, real dy)

    function zoomAt(x, y, factor) {
        if (autoApply && viewport)
            viewport.zoomAt(x, y, factor)
        zoomed(x, y, factor)
    }

    function panBy(dx, dy) {
        if (autoApply && viewport) {
            viewport.panX += dx
            viewport.panY += dy
        }
        panned(dx, dy)
    }

    // Hidden when it has nothing to take, so its arrow cursor does not cover what is beneath.
    MouseArea {
        anchors.fill: parent
        visible: nav.middlePan || nav.wheelZoom
        enabled: nav.middlePan || nav.wheelZoom
        acceptedButtons: nav.middlePan ? Qt.MiddleButton : Qt.NoButton
        cursorShape: pressed ? Qt.ClosedHandCursor : Qt.ArrowCursor

        property real lastX: 0
        property real lastY: 0

        onPressed: (mouse) => {
            lastX = mouse.x
            lastY = mouse.y
        }
        onPositionChanged: (mouse) => {
            if (!pressed)
                return
            nav.panBy(mouse.x - lastX, mouse.y - lastY)
            lastX = mouse.x
            lastY = mouse.y
        }

        // Ctrl-less scrolls are explicitly rejected so they keep propagating: a MouseArea accepts
        // wheel events even with no onWheel bound.
        onWheel: (wheel) => {
            if (!nav.wheelZoom || !Theme.primaryModifierPressed(wheel.modifiers) || wheel.angleDelta.y === 0) {
                wheel.accepted = false
                return
            }
            nav.zoomAt(wheel.x, wheel.y, wheel.angleDelta.y > 0 ? nav.wheelStep : 1 / nav.wheelStep)
            wheel.accepted = true
        }
    }

    PinchHandler {
        id: pinch
        target: null
        enabled: nav.pinchEnabled
        acceptedDevices: nav.pinchDevices

        property real lastScale: 1
        property point lastCentroid

        // Both change signals fire for the same event; consuming the deltas makes the second call
        // a no-op instead of a double application.
        function step() {
            if (!active)
                return
            const c = centroid.position
            if (activeScale !== lastScale && lastScale > 0) {
                nav.zoomAt(c.x, c.y, activeScale / lastScale)
                lastScale = activeScale
            }
            nav.panBy(c.x - lastCentroid.x, c.y - lastCentroid.y)
            lastCentroid = c
        }

        onActiveChanged: {
            if (!active)
                return
            lastScale = activeScale
            lastCentroid = centroid.position
        }
        onActiveScaleChanged: step()
        onCentroidChanged: step()
    }
}
