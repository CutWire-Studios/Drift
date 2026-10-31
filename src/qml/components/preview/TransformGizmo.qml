import QtQuick
import Drift

// The 3D transform gizmo for the selected clip when it is a 3D layer: arrows (move), rings
// (rotate) or square-tipped handles (scale), per EditorState.preview.gizmoTool, along the camera's axes or
// the clip's own per EditorState.preview.gizmoOrientation. Geometry, hit-testing and the drag maths all
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
    // Touch: bigger handles and a wider hit radius.
    property bool touch: false

    // The pose while a handle is dragged, in the box's shape; the owner mirrors it onto the clip's
    // outline, since the overlay does not rebuild mid-drag.
    property var livePose: null
    property string dragging: ""
    property string hovered: ""

    signal dragStarted()
    signal dragFinished()

    readonly property real handleSize: touch ? 1.5 : 1
    readonly property real tolerance: touch ? 22 : 8
    readonly property var pose: livePose || box
    readonly property var geometry: {
        void EditorState.preview.gizmoTool
        void EditorState.preview.gizmoOrientation
        void EditorState.preview.viewRevision
        return pose ? EditorState.preview.gizmoGeometry(pose, sx, handleSize) : null
    }

    function pickAt(x, y) {
        if (!pose || !geometry || !geometry.valid)
            return ""
        void EditorState.preview.gizmoTool
        void EditorState.preview.gizmoOrientation
        return EditorState.preview.gizmoPick(pose, sx, handleSize, x, y, tolerance)
    }

    function colorFor(id) {
        return id === "x" ? Theme.c.transformGizmo.x : id === "y" ? Theme.c.transformGizmo.y
             : id === "z" ? Theme.c.transformGizmo.z : Theme.c.transformGizmo.uniform
    }

    visible: !!box && !!geometry && geometry.valid

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
        // Only the handles are "inside", so everything else reaches the clip boxes below.
        containmentMask: QtObject {
            function contains(point: point): bool {
                return gizmo.dragging !== "" || gizmo.pickAt(point.x, point.y) !== ""
            }
        }

        property var startPose: null
        property var startBoxes: null
        property point press: Qt.point(0, 0)

        onPositionChanged: (mouse) => {
            if (!pressed) {
                gizmo.hovered = gizmo.pickAt(mouse.x, mouse.y)
                return
            }
            if (gizmo.dragging === "")
                return
            // Ctrl passes straight through the 15° steps, as it does for the 2D snaps.
            const snap = !(mouse.modifiers & Qt.ControlModifier)
            gizmo.livePose = startBoxes
                ? EditorState.preview.applyGroupGizmoDrag(
                      startPose, startBoxes, gizmo.dragging, press.x, press.y, mouse.x, mouse.y,
                      snap, gizmo.sx)
                : EditorState.preview.applyGizmoDrag(
                      startPose, gizmo.dragging, press.x, press.y, mouse.x, mouse.y, snap, gizmo.sx)
        }
        onExited: if (!pressed) gizmo.hovered = ""
        onPressed: (mouse) => {
            const id = gizmo.pickAt(mouse.x, mouse.y)
            if (id === "") {
                mouse.accepted = false
                return
            }
            startPose = gizmo.box
            startBoxes = gizmo.box.kind === "group" ? gizmo.groupBoxes : null
            press = Qt.point(mouse.x, mouse.y)
            gizmo.dragging = id
            gizmo.livePose = gizmo.box
            gizmo.dragStarted()
            EditorState.beginPreviewDrag()
        }
        onReleased: finish()
        onCanceled: finish()

        function finish() {
            if (gizmo.dragging === "")
                return
            gizmo.dragging = ""
            gizmo.livePose = null
            startPose = null
            startBoxes = null
            gizmo.dragFinished()
        }
    }
}
