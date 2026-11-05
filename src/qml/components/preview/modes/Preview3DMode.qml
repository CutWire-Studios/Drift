import QtQuick
import Drift
// Navigating the 3D scene. Blender scheme: middle- or right-drag orbits, with Shift it pans, Ctrl+middle dollies. Maya scheme:
// Alt+left/middle/right orbits/pans/dollies. The wheel zooms toward the pointer.
// A click on the camera's drawn body selects it, and the corner widget shows which way the world's
// axes point. Sits under the tools, so a clip's box still wins the pointer.
Item {
    id: mode

    // The PreviewViewport this view navigates.
    property var viewport

    readonly property Item canvas: viewport ? viewport.canvas : null
    readonly property real perCanvas: (canvas ? canvas.width : 0) / Math.max(1, EditorState.projectFile.projectWidth())

    // One numpad step, in degrees. Blender's default rotation angle.
    readonly property real orbitStep: 15

    // "left"/"right"/"up"/"down" orbit, "in"/"out" dolly, from the numpad. Orbit walks the eye: left toward the left view, up toward the
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

    // Blender's view keys: F frames the selection, Home frames everything, Shift+Home resets, Shift+F flies, Numpad 5 toggles orthographic, Numpad 0 looks through the camera,
    // Numpad 1/3/7 look from the front/right/top (Ctrl: the opposite side), Numpad 4/6/8/2 orbit
    // and Numpad +/− zoom. Shift+Numpad 4/6 rolls in Blender, which this view does not, so those
    // are left alone. The owner forwards key presses that bubble up to the viewport, so they work
    // from whatever in the preview has focus.
    function handleKey(event) {
        const keypad = (event.modifiers & Qt.KeypadModifier) !== 0
        const opposite = (event.modifiers & Qt.ControlModifier) !== 0
        const shifted = (event.modifiers & Qt.ShiftModifier) !== 0
        if (event.key === Qt.Key_F && shifted && !opposite) {
            EditorState.preview.flying = !EditorState.preview.flying
        } else if (event.key === Qt.Key_F && !opposite) {
            EditorState.preview.frameSelection()
        } else if (event.key === Qt.Key_Home && shifted) {
            EditorState.preview.resetView()
        } else if (event.key === Qt.Key_Home) {
            EditorState.preview.frameAll()
        } else if (keypad && event.key === Qt.Key_5) {
            EditorState.preview.toggleOrthographic()
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

    // Above the tools, so Alt-drag in the Maya scheme wins over a clip under the pointer. Presses that are
    // not a navigation gesture are refused and fall through to them.
    MouseArea {
        id: navArea

        // "orbit", "pan", "dolly" or "" for a press that is not navigation.
        function gestureFor(button, modifiers) {
            const shift = (modifiers & Qt.ShiftModifier) !== 0
            const ctrl = (modifiers & Qt.ControlModifier) !== 0
            const alt = (modifiers & Qt.AltModifier) !== 0
            if (EditorState.preview.navigationScheme === "maya") {
                if (!alt)
                    return ""
                return button === Qt.LeftButton ? "orbit" : button === Qt.MiddleButton ? "pan" : "dolly"
            }
            if (button === Qt.LeftButton)
                return ""
            if (shift)
                return "pan"
            return ctrl && button === Qt.MiddleButton ? "dolly" : "orbit"
        }

        parent: mode.viewport ? mode.viewport : mode
        anchors.fill: parent
        z: 150
        acceptedButtons: Qt.LeftButton | Qt.MiddleButton | Qt.RightButton
        cursorShape: gesture !== "" ? Qt.ClosedHandCursor : Qt.ArrowCursor

        property string gesture: ""
        property real lastX: 0
        property real lastY: 0

        onPressed: (mouse) => {
            gesture = gestureFor(mouse.button, mouse.modifiers)
            if (gesture === "") {
                mouse.accepted = false
                return
            }
            lastX = mouse.x
            lastY = mouse.y
            mode.viewport.forceActiveFocus()
            EditorState.preview.setNavigating(true)
        }
        onReleased: finish()
        onCanceled: finish()
        onPositionChanged: (mouse) => {
            if (gesture === "")
                return
            const dx = mouse.x - lastX
            const dy = mouse.y - lastY
            lastX = mouse.x
            lastY = mouse.y
            if (gesture === "pan")
                EditorState.preview.pan(dx / mode.perCanvas, dy / mode.perCanvas)
            else if (gesture === "dolly")
                EditorState.preview.dolly(EditorState.preview.navigationScheme === "maya" ? (dx - dy) / 30 : -dy / 30)
            else
                EditorState.preview.orbit(dx * 0.4, dy * 0.4)
        }
        onWheel: (wheel) => {
            if (wheel.angleDelta.y === 0 || !mode.canvas || mode.canvas.width <= 0) {
                wheel.accepted = false
                return
            }
            const p = mode.canvas.mapFromItem(navArea, wheel.x, wheel.y)
            EditorState.preview.zoomAt(wheel.angleDelta.y / 120, p.x / mode.canvas.width * 2 - 1,
                                       1 - p.y / mode.canvas.height * 2, mode.canvas.width / mode.canvas.height)
            wheel.accepted = true
        }

        function finish() {
            if (gesture !== "")
                EditorState.preview.setNavigating(false)
            gesture = ""
        }
    }

    // Touch has no middle or right button: one finger orbits, two dolly and pan.
    DragHandler {
        target: null
        acceptedDevices: PointerDevice.TouchScreen
        maximumPointCount: 1

        property point last

        onActiveChanged: {
            if (active) {
                last = Qt.point(0, 0)
                mode.viewport.forceActiveFocus()
            }
            EditorState.preview.setNavigating(active)
        }
        onTranslationChanged: {
            if (!active)
                return
            EditorState.preview.orbit((translation.x - last.x) * 0.4, (translation.y - last.y) * 0.4)
            last = Qt.point(translation.x, translation.y)
        }
    }

    PinchHandler {
        target: null
        acceptedDevices: PointerDevice.TouchScreen

        property real lastScale: 1
        property point lastCentroid

        function step() {
            if (!active)
                return
            const c = centroid.position
            if (activeScale !== lastScale && lastScale > 0) {
                // dolly() scales the distance by 0.85 per step.
                EditorState.preview.dolly(Math.log(activeScale / lastScale) / 0.1625)
                lastScale = activeScale
            }
            EditorState.preview.pan((c.x - lastCentroid.x) / mode.perCanvas,
                                    (c.y - lastCentroid.y) / mode.perCanvas)
            lastCentroid = c
        }

        onActiveChanged: {
            if (!active)
                return
            lastScale = activeScale
            lastCentroid = centroid.position
            mode.viewport.forceActiveFocus()
        }
        onActiveScaleChanged: step()
        onCentroidChanged: step()
    }

    TapHandler {
        enabled: !EditorState.playing
        onTapped: (eventPoint) => {
            mode.viewport.forceActiveFocus()
            const p = mode.canvas.mapFromItem(mode, eventPoint.position.x, eventPoint.position.y)
            EditorState.preview.pickCamera(p.x / mode.perCanvas, p.y / mode.perCanvas, 14 / mode.perCanvas)
        }
    }

    // Blender's fly mode. The view keeps turning toward the pointer, faster the farther it is from
    // the centre, so steering needs no pointer lock (Wayland has none to give). W/S move along the
    // view, A/D strafe, Q/E sink and rise, Shift hurries and the wheel sets the speed. Esc, a click
    // or the tool button lands, keeping the view where it is.
    Item {
        id: fly

        readonly property bool on: EditorState.preview.flying
        // Movement in view distances per second; turning in degrees per second at the edge.
        property real speed: 0.5
        readonly property real turnRate: 120
        // The still zone around the centre, as a fraction of the half size.
        readonly property real deadZone: 0.08
        property var held: ({})
        // Steering waits until the pointer has visited the centre: starting from the tool button at
        // the edge would otherwise send the view spinning at once.
        property bool steering: false
        property point pointer: Qt.point(width / 2, height / 2)

        function isFlyKey(key) {
            return key === Qt.Key_W || key === Qt.Key_A || key === Qt.Key_S || key === Qt.Key_D
                    || key === Qt.Key_Q || key === Qt.Key_E || key === Qt.Key_Shift
                    || key === Qt.Key_Escape
        }
        function setHeld(key, down) {
            const next = Object.assign({}, held)
            next[key] = down
            held = next
        }
        function axis(plus, minus) {
            return (held[plus] ? 1 : 0) - (held[minus] ? 1 : 0)
        }
        // Squared past the still zone, so small offsets steer finely.
        function steer(offset) {
            const t = Math.max(0, Math.abs(offset) - deadZone) / (1 - deadZone)
            return Math.sign(offset) * Math.min(1, t) * Math.min(1, t)
        }
        function step(dt) {
            let right = 0
            let up = 0
            if (steering && width > 0 && height > 0) {
                right = steer((pointer.x - width / 2) / (width / 2)) * turnRate * dt
                up = -steer((pointer.y - height / 2) / (height / 2)) * turnRate * dt
            }
            const move = speed * dt * (held[Qt.Key_Shift] ? 4 : 1)
            const forward = axis(Qt.Key_W, Qt.Key_S) * move
            const strafe = axis(Qt.Key_D, Qt.Key_A) * move
            const rise = axis(Qt.Key_E, Qt.Key_Q) * move
            if (right !== 0 || up !== 0 || forward !== 0 || strafe !== 0 || rise !== 0)
                EditorState.preview.fly(right, up, forward, strafe, rise)
        }

        anchors.fill: parent
        z: 10
        visible: on
        enabled: on

        onOnChanged: {
            held = {}
            steering = false
            if (on)
                forceActiveFocus()
        }
        onActiveFocusChanged: {
            if (!activeFocus)
                held = {}
        }

        Keys.onShortcutOverride: (event) => {
            if (fly.isFlyKey(event.key))
                event.accepted = true
        }
        Keys.onPressed: (event) => {
            if (event.key === Qt.Key_Escape) {
                EditorState.preview.flying = false
            } else if (fly.isFlyKey(event.key)) {
                if (!event.isAutoRepeat)
                    fly.setHeld(event.key, true)
            } else {
                return
            }
            event.accepted = true
        }
        Keys.onReleased: (event) => {
            if (!fly.isFlyKey(event.key) || event.isAutoRepeat)
                return
            fly.setHeld(event.key, false)
            event.accepted = true
        }

        MouseArea {
            anchors.fill: parent
            hoverEnabled: true
            acceptedButtons: Qt.LeftButton | Qt.RightButton
            cursorShape: Qt.CrossCursor

            onPositionChanged: (mouse) => {
                fly.pointer = Qt.point(mouse.x, mouse.y)
                if (!fly.steering
                        && Math.abs(mouse.x - width / 2) <= fly.deadZone * width / 2
                        && Math.abs(mouse.y - height / 2) <= fly.deadZone * height / 2)
                    fly.steering = true
            }
            onExited: fly.steering = false
            onClicked: EditorState.preview.flying = false
            onWheel: (wheel) => {
                fly.speed = Math.max(0.02, Math.min(20, fly.speed * Math.pow(1.25, wheel.angleDelta.y / 120)))
                wheel.accepted = true
            }
        }

        FrameAnimation {
            running: fly.on
            onTriggered: fly.step(Math.min(frameTime, 0.1))
        }

        // The still zone, so there is something to aim the pointer at.
        Preview3DFlyDeadZone {
            steering: fly.steering
            fraction: fly.deadZone
        }

        Preview3DFlyHint {
            message: fly.steering
                     ? qsTr("W/A/S/D move · Q/E down/up · Shift faster · Wheel speed (%1×) · Esc or click to finish")
                           .arg(fly.speed.toFixed(2))
                     : qsTr("Move the pointer to the centre ring to start steering")
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
}
