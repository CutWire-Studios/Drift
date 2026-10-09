import QtQuick
import Drift
import "../.."
import ".."

// Navigating the 3D scene: middle- or right-drag orbits, with Shift it pans, and the wheel dollies.
// A click on the camera's drawn body selects it, and the corner widget shows which way the world's
// axes point. The pad under it steps the view. Sits under the tools, so a clip's box still wins
// the pointer.
Item {
    id: mode

    // The PreviewViewport this view navigates.
    property PreviewViewport viewport

    readonly property Item canvas: viewport ? viewport.canvas : null
    readonly property real perCanvas: (canvas ? canvas.width : 0) / Math.max(1, EditorState.projectFile.projectWidth())

    // One numpad step, in degrees. Blender's default rotation angle.
    readonly property real orbitStep: 15

    // "left"/"right"/"up"/"down" orbit, "in"/"out" dolly. Shared by the corner pad and the numpad,
    // so the two cannot drift apart. Orbit walks the eye: left toward the left view, up toward the
    // top view. A mouse drag grabs the scene instead, so it turns the other way.
    function applyNav(action) {
        if (action === "left")
            EditorState.preview.orbit(orbitStep, 0)
        else if (action === "right")
            EditorState.preview.orbit(-orbitStep, 0)
        else if (action === "up")
            EditorState.preview.orbit(0, orbitStep)
        else if (action === "down")
            EditorState.preview.orbit(0, -orbitStep)
        else if (action === "in")
            EditorState.preview.dolly(1)
        else if (action === "out")
            EditorState.preview.dolly(-1)
    }

    // Blender's view keys: F frames the selection, Home resets, Numpad 0 looks through the camera,
    // Numpad 1/3/7 look from the front/right/top (Ctrl: the opposite side), Numpad 4/6/8/2 orbit
    // and Numpad +/− zoom. Shift+Numpad 4/6 rolls in Blender, which this view does not, so those
    // are left alone. The owner forwards key presses that bubble up to the viewport, so they work
    // from whatever in the preview has focus.
    function handleKey(event) {
        const keypad = (event.modifiers & Qt.KeypadModifier) !== 0
        const opposite = (event.modifiers & Qt.ControlModifier) !== 0
        const shifted = (event.modifiers & Qt.ShiftModifier) !== 0
        if (event.key === Qt.Key_F && !opposite) {
            EditorState.preview.frameSelection()
        } else if (event.key === Qt.Key_Home) {
            EditorState.preview.resetView()
        } else if (keypad && event.key === Qt.Key_0) {
            EditorState.preview.toggleLookThrough()
        } else if (keypad && event.key === Qt.Key_1) {
            EditorState.preview.setAxisView(opposite ? "back" : "front")
        } else if (keypad && event.key === Qt.Key_3) {
            EditorState.preview.setAxisView(opposite ? "left" : "right")
        } else if (keypad && event.key === Qt.Key_7) {
            EditorState.preview.setAxisView(opposite ? "bottom" : "top")
        } else if (keypad && !shifted && event.key === Qt.Key_4) {
            applyNav("left")
        } else if (keypad && !shifted && event.key === Qt.Key_6) {
            applyNav("right")
        } else if (keypad && !shifted && event.key === Qt.Key_8) {
            applyNav("up")
        } else if (keypad && !shifted && event.key === Qt.Key_2) {
            applyNav("down")
        } else if (keypad && !shifted && event.key === Qt.Key_Plus) {
            applyNav("in")
        } else if (keypad && !shifted && event.key === Qt.Key_Minus) {
            applyNav("out")
        } else {
            return
        }
        event.accepted = true
    }

    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.MiddleButton | Qt.RightButton
        cursorShape: pressed ? Qt.ClosedHandCursor : Qt.ArrowCursor

        property real lastX: 0
        property real lastY: 0

        onPressed: (mouse) => {
            lastX = mouse.x
            lastY = mouse.y
            mode.viewport.forceActiveFocus()
            EditorState.preview.setNavigating(true)
        }
        onReleased: EditorState.preview.setNavigating(false)
        onCanceled: EditorState.preview.setNavigating(false)
        onPositionChanged: (mouse) => {
            if (!pressed)
                return
            const dx = mouse.x - lastX
            const dy = mouse.y - lastY
            lastX = mouse.x
            lastY = mouse.y
            if (mouse.modifiers & Qt.ShiftModifier)
                EditorState.preview.pan(dx / mode.perCanvas, dy / mode.perCanvas)
            else
                EditorState.preview.orbit(dx * 0.4, dy * 0.4)
        }
        onWheel: (wheel) => {
            if (wheel.angleDelta.y === 0) {
                wheel.accepted = false
                return
            }
            EditorState.preview.dolly(wheel.angleDelta.y / 120)
            wheel.accepted = true
        }
    }

    TapHandler {
        enabled: !EditorState.playing
        onTapped: (eventPoint) => {
            mode.viewport.forceActiveFocus()
            const p = mode.canvas.mapFromItem(mode, eventPoint.position.x, eventPoint.position.y)
            EditorState.preview.pickCamera(p.x / mode.perCanvas, p.y / mode.perCanvas, 14 / mode.perCanvas)
        }
    }

    // On the viewport rather than in this view, so it stacks above the tools: a clip's box covering
    // the corner would otherwise swallow its clicks.
    ViewAxisWidget {
        id: axes
        parent: mode.viewport ? mode.viewport : mode
        z: 160
        x: mode.canvas ? mode.canvas.x + mode.canvas.width - width - Theme.spacingLg : 0
        y: mode.canvas ? mode.canvas.y + Theme.spacingLg : 0
    }

    ViewNavPad {
        parent: axes.parent
        z: 160
        x: axes.x + (axes.width - width) / 2
        y: axes.y + axes.height + Theme.spacingXs
        navigate: (action) => mode.applyNav(action)
    }
}
