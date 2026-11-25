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

    // Blender's view keys (see the viewport3d scope of the keymap for the defaults): frame the
    // selection or everything, reset, fly, toggle orthographic, look through the camera, look from
    // the front/right/top or the opposite side, orbit and dolly. Shift+Numpad 4/6 rolls in Blender,
    // which this view does not. The panel hands it the actions its keys are bound to.
    function handleViewAction(id) {
        switch (id) {
        case "viewport3d.toggleFly": EditorState.preview.flying = !EditorState.preview.flying; return true
        case "viewport3d.frameSelected": EditorState.preview.frameSelection(); return true
        case "viewport3d.resetView": EditorState.preview.resetView(); return true
        case "viewport3d.frameAll": EditorState.preview.frameAll(); return true
        case "viewport3d.toggleOrthographic": EditorState.preview.toggleOrthographic(); return true
        case "viewport3d.lookThrough": EditorState.preview.toggleLookThrough(); return true
        case "viewport3d.viewFront": EditorState.preview.setAxisView("front"); return true
        case "viewport3d.viewBack": EditorState.preview.setAxisView("back"); return true
        case "viewport3d.viewRight": EditorState.preview.setAxisView("right"); return true
        case "viewport3d.viewLeft": EditorState.preview.setAxisView("left"); return true
        case "viewport3d.viewTop": EditorState.preview.setAxisView("top"); return true
        case "viewport3d.viewBottom": EditorState.preview.setAxisView("bottom"); return true
        case "viewport3d.orbitLeft": applyNav("left"); return true
        case "viewport3d.orbitRight": applyNav("right"); return true
        case "viewport3d.orbitUp": applyNav("up"); return true
        case "viewport3d.orbitDown": applyNav("down"); return true
        case "viewport3d.dollyIn": applyNav("in"); return true
        case "viewport3d.dollyOut": applyNav("out"); return true
        }
        return false
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

    // Blender's fly mode. With the Mouse look setting the pointer is locked and the mouse's raw
    // movement turns the view; otherwise (touchpads, or where no lock is available) the view keeps
    // turning toward the pointer, faster the farther it is from the centre. W/S move along the
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
        // Fly key (Qt.Key_*) -> the fly3d action it was pressed as; Shift is held by key alone.
        property var held: ({})
        // Steering waits until the pointer has visited the centre: starting from the tool button at
        // the edge would otherwise send the view spinning at once.
        property bool steering: false
        property point pointer: Qt.point(width / 2, height / 2)
        // True while the pointer lock was granted for this flight; the look is then the mouse's.
        property bool mouseLook: false
        // Degrees per mouse count.
        readonly property real lookSensitivity: 0.12

        // Shift speeds the flight up, so a fly chord is looked up without it.
        function flyAction(event) {
            if (event.key === Qt.Key_Shift)
                return "shift"
            const keymap = EditorState.keymap
            return keymap.actionFor("fly3d", keymap.chordFromKey(event.key, event.modifiers & ~Qt.ShiftModifier,
                                                                 event.nativeScanCode, event.text))
        }
        function setHeld(key, action) {
            const next = Object.assign({}, held)
            if (action !== "")
                next[key] = action
            else
                delete next[key]
            held = next
        }
        function has(action) {
            return Object.values(held).indexOf(action) >= 0
        }
        function axis(plus, minus) {
            return (has(plus) ? 1 : 0) - (has(minus) ? 1 : 0)
        }
        // Squared past the still zone, so small offsets steer finely.
        function steer(offset) {
            const t = Math.max(0, Math.abs(offset) - deadZone) / (1 - deadZone)
            return Math.sign(offset) * Math.min(1, t) * Math.min(1, t)
        }
        // Whether a key of a held fly action is a controller button that is down now.
        function padHeld(action) {
            return EditorState.keymap.bindings(action).some((chord) => Gamepad.chordHeld(chord))
        }
        function step(dt) {
            let right = 0
            let up = 0
            if (!mouseLook && steering && width > 0 && height > 0) {
                right = steer((pointer.x - width / 2) / (width / 2)) * turnRate * dt
                up = -steer((pointer.y - height / 2) / (height / 2)) * turnRate * dt
            }
            const move = speed * dt * (has("shift") ? 4 : 1)
            let forward = axis("fly3d.forward", "fly3d.back") * move
            let strafe = axis("fly3d.right", "fly3d.left") * move
            let rise = axis("fly3d.up", "fly3d.down") * move
            if (Gamepad.connected) {
                // Sticks move and turn, the triggers speed up and slow down, the shoulders sink and rise.
                const padMove = speed * dt * Math.pow(4, Gamepad.rightTrigger - Gamepad.leftTrigger)
                forward += Gamepad.leftY * padMove
                strafe += Gamepad.leftX * padMove
                rise += ((padHeld("fly3d.up") ? 1 : 0) - (padHeld("fly3d.down") ? 1 : 0)) * padMove
                right += Gamepad.rightX * Gamepad.lookSpeed * dt
                up += Gamepad.rightY * Gamepad.lookSpeed * dt
                if (Gamepad.gyroEnabled) {
                    const degrees = 57.29578 * Gamepad.gyroSensitivity * dt
                    right -= Gamepad.gyroY * degrees
                    up += Gamepad.gyroX * degrees
                }
            }
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
            PointerLock.unlock()
            mouseLook = false
            if (on) {
                forceActiveFocus()
                mouseLook = EditorState.preview.flyLook === "mouse" && PointerLock.lock(fly)
            }
        }
        onActiveFocusChanged: {
            if (!activeFocus) {
                held = {}
                PointerLock.unlock()
            }
        }

        Connections {
            target: Gamepad
            enabled: fly.on

            function onButtonPressed(chord) {
                if (EditorState.keymap.actionFor("fly3d", chord) === "fly3d.land")
                    EditorState.preview.flying = false
            }
        }

        Connections {
            target: PointerLock

            function onMoved(dx, dy) {
                if (fly.on && fly.mouseLook)
                    EditorState.preview.fly(dx * fly.lookSensitivity, -dy * fly.lookSensitivity, 0, 0, 0)
            }
            // Only a lock that was held can drop: the compositor or system took it back.
            function onLockedChanged() {
                if (!PointerLock.locked && fly.mouseLook && fly.on)
                    EditorState.preview.flying = false
            }
        }

        Keys.onShortcutOverride: (event) => {
            if (event.key === Qt.Key_Escape || fly.flyAction(event) !== "")
                event.accepted = true
        }
        Keys.onPressed: (event) => {
            const action = fly.flyAction(event)
            if (event.key === Qt.Key_Escape) {
                EditorState.preview.flying = false
            } else if (action !== "") {
                if (!event.isAutoRepeat)
                    fly.setHeld(event.key, action)
            } else {
                return
            }
            event.accepted = true
        }
        // By key, not chord: a modifier pressed or let go mid-flight must not strand a held key.
        Keys.onReleased: (event) => {
            if (event.isAutoRepeat || !(event.key in fly.held))
                return
            fly.setHeld(event.key, "")
            event.accepted = true
        }

        MouseArea {
            anchors.fill: parent
            hoverEnabled: true
            acceptedButtons: Qt.LeftButton | Qt.RightButton
            cursorShape: Qt.CrossCursor

            onPositionChanged: (mouse) => {
                if (fly.mouseLook)
                    return
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
            visible: !fly.mouseLook
            steering: fly.steering
            fraction: fly.deadZone
        }

        Preview3DFlyHint {
            message: fly.mouseLook
                     ? qsTr("Mouse looks · W/A/S/D move · Q/E down/up · Shift faster · Wheel speed (%1×) · Esc or click to finish")
                           .arg(fly.speed.toFixed(2))
                     : fly.steering
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
