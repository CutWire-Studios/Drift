import QtQuick
import Drift

// The 3D transform gizmo for the selected clip when it is a 3D layer: arrows (move), rings
// (rotate) or square-tipped handles (scale), per EditorState.preview.gizmoTool, along the world's, the clip's
// own or the screen's axes per EditorState.preview.gizmoOrientation. Geometry, hit-testing and the drag maths all
// come from the engine (engine/ClipGizmo), so this only draws and forwards the pointer.
//
// Fills the overlay it sits in, whose coordinates are canvas px times sx. Presses anywhere off a
// handle fall through to the clip boxes underneath.
Item {
    id: gizmo

    // A previewClipsAtPlayhead entry, or null for no gizmo.
    property var box: null
    // The members when box is the pivot frame of a multi-selection (groupGizmoPivot); empty otherwise.
    property var groupBoxes: []
    property real sx: 1
    // 0 measures through the scene camera, 1 through the editor viewport.
    property int viewport: 0
    // Touch: bigger handles and a wider hit radius.
    property bool touch: false

    // The pose while a handle is dragged, in the box's shape; the owner mirrors it onto the clip's
    // outline, since the overlay does not rebuild mid-drag.
    property var livePose: null
    property string dragging: ""
    property string hovered: ""

    // The phone's Snap chip. On a keyboard, Ctrl held snaps and no chip is needed.
    property bool snapOn: false
    // An axis the next drag (or the one in flight) is constrained to, from the phone's X/Y/Z chips;
    // empty for free.
    property string lockPreset: ""

    // The lock of the drag in flight: "x", "y" or "z" for an axis, lockPlane for the plane without it,
    // lockFlip for the opposite of the gizmo's world/local orientation.
    property string axisLock: ""
    property bool lockPlane: false
    property bool lockFlip: false
    readonly property var guides: dragging !== "" && axisLock !== ""
        ? EditorState.preview.gizmoLockGuides(startPose || box, sx, dragOptions(undefined), viewport) : []

    signal dragStarted(bool touch)
    signal dragFinished()
    // Where the pointer is, in this item's coordinates, for the HUD.
    signal pointerMoved(point position)

    readonly property real handleSize: touch ? 1.5 : 1
    readonly property real mouseTolerance: 8
    readonly property real touchTolerance: 22
    readonly property var pose: livePose || box
    readonly property var geometry: {
        void EditorState.preview.gizmoTool
        void EditorState.preview.gizmoOrientation
        void EditorState.preview.viewRevision
        return pose ? EditorState.preview.gizmoGeometry(pose, sx, handleSize, viewport) : null
    }

    property var startPose: null
    property var startBoxes: null
    property point press: Qt.point(0, 0)
    // Where the engine reads the pointer: the real one, except that Shift slows its travel to a
    // tenth, so engaging or releasing it never makes the pose jump.
    property point effective: Qt.point(0, 0)
    property point lastPoint: Qt.point(0, 0)
    property int heldModifiers: 0

    // Drops a drag in flight without announcing it, for when the owner has ended or cancelled it.
    function abort() {
        if (dragging === "")
            return
        dragging = ""
        livePose = null
        startPose = null
        startBoxes = null
        clearLock()
    }

    function clearLock() {
        axisLock = ""
        lockPlane = false
        lockFlip = false
    }

    function pickAt(x, y, tolerance) {
        if (!pose || !geometry || !geometry.valid)
            return ""
        void EditorState.preview.gizmoTool
        void EditorState.preview.gizmoOrientation
        return EditorState.preview.gizmoPick(pose, sx, handleSize, x, y, tolerance, viewport)
    }

    function colorFor(id) {
        return id === "x" ? Theme.c.transformGizmo.x : id === "y" ? Theme.c.transformGizmo.y
             : id === "z" ? Theme.c.transformGizmo.z : Theme.c.transformGizmo.uniform
    }

    function dragOptions(typed) {
        const base = EditorState.preview.gizmoOrientation
        const options = {
            snap: snapOn || (heldModifiers & Qt.ControlModifier) !== 0,
            axis: axisLock,
            plane: lockPlane,
            orientation: lockFlip ? (base === "local" ? "world" : "local") : ""
        }
        if (typed !== undefined)
            options.typed = typed
        return options
    }

    function signed(value, digits) {
        return (value >= 0 ? "+" : "") + value.toFixed(digits)
    }

    function readout(p) {
        if (!p || p.amount === undefined)
            return ""
        const tool = EditorState.preview.gizmoTool
        const axis = (axisLock !== "" ? axisLock : (dragging === "xy" ? "" : dragging)).toUpperCase()
        let text
        if (tool === "move") {
            if (lockPlane) {
                const planes = { "X": "YZ", "Y": "XZ", "Z": "XY" }
                text = qsTr("%1 %2 / %3").arg(planes[axis]).arg(signed(p.amount, 2)).arg(signed(p.amount2, 2))
            } else {
                text = qsTr("%1 %2").arg(axis).arg(signed(p.amount, 2))
            }
        } else if (tool === "rotate") {
            text = qsTr("Rot %1 %2°").arg(axis).arg(Number(p.amount.toFixed(1)))
        } else {
            text = axis !== "" ? qsTr("Scale %1 %2×").arg(axis).arg(p.amount.toFixed(2))
                               : qsTr("Scale %1×").arg(p.amount.toFixed(2))
        }
        if (axisLock !== "") {
            const local = (EditorState.preview.gizmoOrientation === "local") !== lockFlip
            text += " · " + (local ? qsTr("Local") : qsTr("World"))
        }
        return text
    }

    // Re-solves the pose for the pointer and the lock as they stand, with a typed value in place of
    // the pointer when `typed` is given.
    function solve(typed) {
        if (dragging === "" || !startPose)
            return
        const options = dragOptions(typed)
        livePose = startBoxes
            ? EditorState.preview.applyGroupGizmoDrag(startPose, startBoxes, dragging, press.x, press.y,
                                                      effective.x, effective.y, options, sx, viewport)
            : EditorState.preview.applyGizmoDrag(startPose, dragging, press.x, press.y,
                                                 effective.x, effective.y, options, sx, viewport)
        EditorState.drag.update(readout(livePose))
    }

    // X, Y or Z during a drag: constrains it to that axis; again, to the opposite orientation (local
    // or world); a third time, free. With `plane` (Shift), to the plane without that axis, which a
    // second press releases. Returns whether a drag took it.
    function lockKey(axis, plane) {
        if (dragging === "")
            return false
        if (plane) {
            if (axisLock === axis && lockPlane) {
                clearLock()
            } else {
                axisLock = axis
                lockPlane = true
                lockFlip = false
            }
        } else if (axisLock === axis && !lockPlane) {
            if (lockFlip)
                clearLock()
            else
                lockFlip = true
        } else {
            axisLock = axis
            lockPlane = false
            lockFlip = false
        }
        solve(undefined)
        return true
    }

    // A modifier key went down or up with the pointer still: the snap follows it.
    function setModifiers(modifiers) {
        heldModifiers = modifiers
        solve(undefined)
    }

    onLockPresetChanged: {
        if (dragging === "")
            return
        axisLock = lockPreset
        lockPlane = false
        lockFlip = false
        solve(undefined)
    }

    visible: !!box && !!geometry && geometry.valid

    // The lock's axes through the pivot, in the axis colour, running out of the view both ways.
    Repeater {
        model: gizmo.guides

        delegate: Item {
            id: guide

            required property var modelData
            readonly property real angle: Math.atan2(modelData.y2 - modelData.y1, modelData.x2 - modelData.x1) * 180 / Math.PI

            x: modelData.x1
            y: modelData.y1

            Repeater {
                model: [0, 180]

                delegate: Rectangle {
                    required property int modelData
                    x: 0
                    y: -height / 2
                    width: 6000
                    height: Theme.c.manipSnapLine.strokeWidth
                    transformOrigin: Item.Left
                    rotation: guide.angle + modelData
                    color: gizmo.colorFor(guide.modelData.axis)
                    opacity: Theme.c.transformGizmoHandle.frontOpacity
                }
            }
        }
    }

    Repeater {
        model: gizmo.visible ? gizmo.geometry.handles : []

        delegate: TransformGizmoHandle {
            host: gizmo
        }
    }

    // Pivot: the clip centre, which every tool turns and scales about.
    TransformGizmoPivot {
        shown: gizmo.visible
        touch: gizmo.touch
        originX: gizmo.visible ? gizmo.geometry.origin.x : 0
        originY: gizmo.visible ? gizmo.geometry.origin.y : 0
    }

    MouseArea {
        id: area
        anchors.fill: parent
        enabled: gizmo.visible
        hoverEnabled: true
        preventStealing: true
        cursorShape: gizmo.dragging !== "" ? Qt.ClosedHandCursor : Qt.PointingHandCursor
        // Only the handles are "inside", so everything else reaches the clip boxes below. A press can be
        // either device, so this takes the wider reach and onPressed narrows it for a mouse.
        containmentMask: QtObject {
            function contains(point: point): bool {
                return gizmo.dragging !== "" || gizmo.pickAt(point.x, point.y, gizmo.touchTolerance) !== ""
            }
        }

        onPositionChanged: (mouse) => {
            if (!pressed) {
                gizmo.hovered = gizmo.pickAt(mouse.x, mouse.y, gizmo.mouseTolerance)
                return
            }
            if (gizmo.dragging === "")
                return
            const slow = (mouse.modifiers & Qt.ShiftModifier) ? 0.1 : 1
            gizmo.effective = Qt.point(gizmo.effective.x + (mouse.x - gizmo.lastPoint.x) * slow,
                                       gizmo.effective.y + (mouse.y - gizmo.lastPoint.y) * slow)
            gizmo.lastPoint = Qt.point(mouse.x, mouse.y)
            gizmo.heldModifiers = mouse.modifiers
            gizmo.pointerMoved(gizmo.lastPoint)
            gizmo.solve(undefined)
        }
        onExited: if (!pressed) gizmo.hovered = ""
        onPressed: (mouse) => {
            const byTouch = mouse.source !== Qt.MouseEventNotSynthesized
            const id = gizmo.pickAt(mouse.x, mouse.y, byTouch ? gizmo.touchTolerance : gizmo.mouseTolerance)
            if (id === "") {
                mouse.accepted = false
                return
            }
            gizmo.startPose = gizmo.box
            gizmo.startBoxes = gizmo.box.kind === "group" ? gizmo.groupBoxes : null
            gizmo.press = Qt.point(mouse.x, mouse.y)
            gizmo.effective = gizmo.press
            gizmo.lastPoint = gizmo.press
            gizmo.heldModifiers = mouse.modifiers
            gizmo.axisLock = gizmo.lockPreset
            gizmo.lockPlane = false
            gizmo.lockFlip = false
            gizmo.dragging = id
            gizmo.livePose = gizmo.box
            gizmo.pointerMoved(gizmo.press)
            gizmo.dragStarted(byTouch)
        }
        onReleased: finish()
        onCanceled: finish()

        function finish() {
            if (gizmo.dragging === "")
                return
            gizmo.abort()
            gizmo.dragFinished()
        }
    }
}
