import QtQuick
import Drift

// The pivot the clip turns and scales about, as a crosshair. Dragging it moves the anchor and the
// position together, so the picture stays where it is; Shift lands it on the box's nine points.
// Left to the inspector for a tilted, parented or camera-framed clip, where the pivot is 3D.
Item {
    id: anchor

    required property var clipBox
    readonly property var manip: clipBox.manip

    visible: clipBox.selected && !manip.mode3d && !manip.groupMode && !clipBox.tilted
             && !clipBox.viaCamera && !clipBox.hasParent
    anchors.fill: parent

    readonly property real cx: clipBox.pivotX * clipBox.sx
    readonly property real cy: clipBox.pivotY * clipBox.sy

    Rectangle {
        x: anchor.cx - width / 2
        y: anchor.cy - height / 2
        width: 18
        height: 1
        color: Theme.c.manipHandle.border
    }

    Rectangle {
        x: anchor.cx - width / 2
        y: anchor.cy - height / 2
        width: 1
        height: 18
        color: Theme.c.manipHandle.border
    }

    HandleKnob {
        shape: "diamond"
        space: anchor.clipBox.stage
        selected: anchor.clipBox.anchoring
        visualSize: anchor.manip.touch ? 14 : 8
        cursor: "anchor"
        x: anchor.cx - width / 2
        y: anchor.cy - height / 2

        onPressed: (point, modifiers, device) => anchor.clipBox.anchorBegin(point, device)
        onMoved: (point, modifiers) => anchor.clipBox.anchorTo(point, modifiers)
        onReleased: if (anchor.clipBox.anchoring) anchor.manip.endGesture()
    }
}
