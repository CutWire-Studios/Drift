import QtQuick
import Drift

// The 3D view's orientation widget: the world axes as they point on screen, X red, Y green, Z blue.
// Clicking an axis end looks from it at the scene, the way Blender's navigation gizmo does. The world is
// y down and z away from the viewer: the knob pointing up is -Y and opens Top, the lower one is +Y
// (Bottom), and +Z is the far side (Back).
Item {
    id: widget

    readonly property real radius: Theme.c.viewAxisWidget.radius
    readonly property var axes: {
        void EditorState.preview.viewRevision
        return EditorState.preview.axes()
    }
    // Axis ends drawn back to front, so the ones toward the viewer sit on top.
    readonly property var ends: {
        const out = []
        const views = { "x": ["right", "left"], "y": ["bottom", "top"], "z": ["back", "front"] }
        for (const a of axes) {
            out.push({ axis: a.axis, sign: 1, x: a.x, y: a.y, depth: a.depth, view: views[a.axis][0] })
            out.push({ axis: a.axis, sign: -1, x: -a.x, y: -a.y, depth: -a.depth, view: views[a.axis][1] })
        }
        out.sort((p, q) => p.depth - q.depth)
        return out
    }

    function colorFor(axis) {
        return axis === "x" ? Theme.c.viewAxisWidget.x : axis === "y" ? Theme.c.viewAxisWidget.y : Theme.c.viewAxisWidget.z
    }

    width: radius * 2 + 16
    height: width

    ViewAxisDisc {}

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
            ViewAxisShaft {
                shown: end.positive
                originX: end.cx
                originY: end.cy
                tipX: end.tipX
                tipY: end.tipY
                ink: widget.colorFor(end.modelData.axis)
            }

            ViewAxisKnob {
                positive: end.positive
                tipX: end.tipX
                tipY: end.tipY
                ink: widget.colorFor(end.modelData.axis)
                letter: end.modelData.axis.toUpperCase()
                viewName: end.modelData.view
            }
        }
    }
}
