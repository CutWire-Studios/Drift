import QtQuick
import Drift

// Turning a clip: a handle on a stalk beside the box (above it, unless that leaves the viewport),
// and on a mouse four invisible zones just outside the corners that show the rotate cursor, as the
// design tools do. A 3D layer turns with the gizmo's rings instead.
Item {
    id: rotate

    required property var clipBox
    readonly property var manip: clipBox.manip

    visible: clipBox.selected && !manip.mode3d && !manip.groupMode
    anchors.fill: parent

    readonly property real stalk: manip.touch ? 44 : 28
    readonly property real zoneOffset: 20

    // Above the box, unless that would leave the viewport (the handle would be cut off and
    // ungrabbable): then below it, and inside it when neither side has room.
    readonly property int placement: {
        void clipBox.x
        void clipBox.y
        void clipBox.width
        void clipBox.height
        void clipBox.rotation
        void manip.stage.x
        void manip.stage.y
        const half = knob.drawn / 2
        const above = clipBox.mapToItem(manip, clipBox.width / 2, -stalk).y
        if (above - half >= 0)
            return 0
        const below = clipBox.mapToItem(manip, clipBox.width / 2, clipBox.height + stalk).y
        return below + half <= manip.height ? 1 : 2
    }

    Rectangle {
        visible: rotate.placement !== 2
        x: rotate.clipBox.width / 2 - width / 2
        y: rotate.placement === 0 ? -rotate.stalk : rotate.clipBox.height
        width: 1
        height: rotate.stalk
        color: Theme.c.manipHandle.border
    }

    HandleKnob {
        id: knob
        shape: "dot"
        space: rotate.clipBox.stage
        visualSize: rotate.manip.touch ? 16 : 9
        cursor: "rotate:" + ((rotate.placement === 1 ? 90 : -90) + rotate.clipBox.layoutRotation)
        x: rotate.clipBox.width / 2 - width / 2
        y: (rotate.placement === 0 ? -rotate.stalk
            : rotate.placement === 1 ? rotate.clipBox.height + rotate.stalk
            : rotate.stalk) - height / 2

        onPressed: (point, modifiers, device) => rotate.clipBox.rotateBegin(point, device)
        onMoved: (point, modifiers) => rotate.clipBox.rotateTo(point, modifiers)
        onReleased: if (rotate.clipBox.rotating) rotate.manip.endGesture()
    }

    Repeater {
        model: rotate.manip.touch ? [] : [
            { dx: -1, dy: -1 }, { dx: 1, dy: -1 }, { dx: 1, dy: 1 }, { dx: -1, dy: 1 }
        ]

        delegate: HandleKnob {
            id: zone
            required property var modelData
            space: rotate.clipBox.stage
            visualSize: 0
            mouseHitSize: 22
            cursor: "rotate:" + (Math.atan2(modelData.dy, modelData.dx) * 180 / Math.PI
                                 + rotate.clipBox.layoutRotation)
            x: (modelData.dx < 0 ? 0 : rotate.clipBox.width) + modelData.dx * rotate.zoneOffset - width / 2
            y: (modelData.dy < 0 ? 0 : rotate.clipBox.height) + modelData.dy * rotate.zoneOffset - height / 2

            onPressed: (point, modifiers, device) => rotate.clipBox.rotateBegin(point, device)
            onMoved: (point, modifiers) => rotate.clipBox.rotateTo(point, modifiers)
            onReleased: if (rotate.clipBox.rotating) rotate.manip.endGesture()
        }
    }
}
