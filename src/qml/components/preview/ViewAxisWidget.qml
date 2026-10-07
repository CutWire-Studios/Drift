import QtQuick
import Drift

// The 3D view's orientation widget: the world axes as they point on screen, X red, Y green, Z blue.
// Clicking an axis end looks along it at the scene, the way Blender's navigation gizmo does.
Item {
    id: widget

    readonly property real radius: 34
    readonly property var axes: {
        void EditorState.preview.viewRevision
        return EditorState.preview.axes()
    }
    // Axis ends drawn back to front, so the ones toward the viewer sit on top.
    readonly property var ends: {
        const out = []
        const views = { "x": ["right", "left"], "y": ["bottom", "top"], "z": ["front", "back"] }
        for (const a of axes) {
            out.push({ axis: a.axis, sign: 1, x: a.x, y: a.y, depth: a.depth, view: views[a.axis][0] })
            out.push({ axis: a.axis, sign: -1, x: -a.x, y: -a.y, depth: -a.depth, view: views[a.axis][1] })
        }
        out.sort((p, q) => p.depth - q.depth)
        return out
    }

    function colorFor(axis) {
        return axis === "x" ? Theme.gizmoX : axis === "y" ? Theme.gizmoY : Theme.gizmoZ
    }

    width: radius * 2 + 16
    height: width

    Rectangle {
        anchors.fill: parent
        radius: width / 2
        color: hover.hovered ? Qt.rgba(1, 1, 1, 0.08) : "transparent"
        HoverHandler { id: hover }
    }

    Repeater {
        model: widget.ends
        delegate: Item {
            id: end
            required property var modelData
            readonly property real cx: widget.width / 2
            readonly property real cy: widget.height / 2
            readonly property real tipX: cx + modelData.x * widget.radius
            readonly property real tipY: cy + modelData.y * widget.radius
            readonly property bool positive: modelData.sign > 0
            anchors.fill: parent

            // The shaft, for the positive end only.
            Rectangle {
                visible: end.positive
                x: end.cx
                y: end.cy - height / 2
                width: Math.hypot(end.tipX - end.cx, end.tipY - end.cy)
                height: 2
                color: widget.colorFor(end.modelData.axis)
                transformOrigin: Item.Left
                rotation: Math.atan2(end.tipY - end.cy, end.tipX - end.cx) * 180 / Math.PI
            }

            Rectangle {
                id: knob
                width: end.positive ? 18 : 12
                height: width
                radius: width / 2
                x: end.tipX - width / 2
                y: end.tipY - height / 2
                color: end.positive ? widget.colorFor(end.modelData.axis)
                                    : Qt.darker(widget.colorFor(end.modelData.axis), 1.8)
                border.width: knobArea.containsMouse ? 2 : 0
                border.color: Theme.onMedia

                Text {
                    visible: end.positive
                    anchors.centerIn: parent
                    text: end.modelData.axis.toUpperCase()
                    color: Theme.onMedia
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeXs
                    font.bold: true
                }

                MouseArea {
                    id: knobArea
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: EditorState.preview.setAxisView(end.modelData.view)
                }
            }
        }
    }
}
