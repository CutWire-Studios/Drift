import QtQuick
import QtQuick.Shapes
import Drift

// The 3D transform gizmo for the selected clip when it is a 3D layer: arrows (move), rings
// (rotate) or square-tipped handles (scale), per EditorState.gizmoTool, along the camera's axes or
// the clip's own per EditorState.gizmoOrientation. Geometry, hit-testing and the drag maths all
// come from the engine (engine/ClipGizmo), so this only draws and forwards the pointer.
//
// Fills the overlay it sits in, whose coordinates are canvas px times sx. Presses anywhere off a
// handle fall through to the clip boxes underneath.
Item {
    id: gizmo

    // A previewClipsAtPlayhead entry, or null for no gizmo.
    property var box: null
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
        void EditorState.gizmoTool
        void EditorState.gizmoOrientation
        return pose ? EditorState.previewGizmoGeometry(pose, sx, handleSize) : null
    }

    function pickAt(x, y) {
        if (!pose || !geometry || !geometry.valid)
            return ""
        void EditorState.gizmoTool
        void EditorState.gizmoOrientation
        return EditorState.previewGizmoPick(pose, sx, handleSize, x, y, tolerance)
    }

    function colorFor(id) {
        return id === "x" ? Theme.gizmoX : id === "y" ? Theme.gizmoY
             : id === "z" ? Theme.gizmoZ : Theme.gizmoUniform
    }

    visible: !!box && !!geometry && geometry.valid

    Repeater {
        model: gizmo.visible ? gizmo.geometry.handles : []

        delegate: Item {
            id: handleItem
            required property var modelData
            readonly property bool hot: gizmo.dragging === modelData.id
                                        || (gizmo.dragging === "" && gizmo.hovered === modelData.id)
            readonly property color color: hot ? Theme.gizmoHot : gizmo.colorFor(modelData.id)
            readonly property real lineWidth: (hot ? 3 : 2) * (gizmo.touch ? 1.25 : 1)
            // While one handle is dragged the others step back, so the one in use reads clearly.
            opacity: gizmo.dragging === "" || hot ? 1 : 0.35

            // The far half of a ring, behind the clip.
            Repeater {
                model: handleItem.modelData.back
                delegate: Shape {
                    required property var modelData
                    preferredRendererType: Shape.CurveRenderer
                    opacity: 0.3
                    ShapePath {
                        strokeColor: handleItem.color
                        strokeWidth: handleItem.lineWidth
                        fillColor: "transparent"
                        capStyle: ShapePath.RoundCap
                        joinStyle: ShapePath.RoundJoin
                        PathPolyline { path: modelData }
                    }
                }
            }

            Repeater {
                model: handleItem.modelData.front
                delegate: Shape {
                    required property var modelData
                    preferredRendererType: Shape.CurveRenderer
                    ShapePath {
                        strokeColor: handleItem.color
                        strokeWidth: handleItem.lineWidth
                        fillColor: "transparent"
                        capStyle: ShapePath.RoundCap
                        joinStyle: ShapePath.RoundJoin
                        PathPolyline { path: modelData }
                    }
                }
            }

            Shape {
                visible: handleItem.modelData.head.length > 0
                preferredRendererType: Shape.CurveRenderer
                ShapePath {
                    strokeColor: handleItem.color
                    strokeWidth: 1
                    fillColor: handleItem.color
                    joinStyle: ShapePath.RoundJoin
                    PathPolyline { path: handleItem.modelData.head }
                }
            }
        }
    }

    // Pivot: the clip centre, which every tool turns and scales about.
    Rectangle {
        visible: gizmo.visible
        width: gizmo.touch ? 10 : 7
        height: width
        radius: width / 2
        x: gizmo.visible ? gizmo.geometry.origin.x - width / 2 : 0
        y: gizmo.visible ? gizmo.geometry.origin.y - height / 2 : 0
        color: Theme.gizmoUniform
        border.width: 1
        border.color: Theme.scrimStrong
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
        property point press: Qt.point(0, 0)

        onPositionChanged: (mouse) => {
            if (!pressed) {
                gizmo.hovered = gizmo.pickAt(mouse.x, mouse.y)
                return
            }
            if (gizmo.dragging === "")
                return
            // Ctrl passes straight through the 15° steps, as it does for the 2D snaps.
            gizmo.livePose = EditorState.previewApplyGizmoDrag(
                        startPose, gizmo.dragging, press.x, press.y, mouse.x, mouse.y,
                        !(mouse.modifiers & Qt.ControlModifier), gizmo.sx)
        }
        onExited: if (!pressed) gizmo.hovered = ""
        onPressed: (mouse) => {
            const id = gizmo.pickAt(mouse.x, mouse.y)
            if (id === "") {
                mouse.accepted = false
                return
            }
            startPose = gizmo.box
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
            gizmo.dragFinished()
        }
    }
}
