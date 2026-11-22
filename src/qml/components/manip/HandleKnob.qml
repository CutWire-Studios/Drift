import QtQuick
import Drift

// One grip on a canvas: 8 px to look at, 14 px to hit with a mouse and 48 dp with a finger. The
// area is always the larger one; which reach applies is decided per press from the event's own
// source, so a touchscreen laptop gets both. Modifiers come from the event, never from polling.
// `cursor` is a Qt.CursorShape, or a ToolCursor shape string such as "resize:45" or "rotate:90"
// (set it once per knob: a ToolCursor that goes back to empty drops the cursor of the area too).
Item {
    id: knob

    // "dot", "square", "diamond" or "bracket" (an L opening toward the middle of what it frames).
    property string shape: "dot"
    property bool selected: false
    property var cursor: Qt.PointingHandCursor
    property real visualSize: 8
    property real mouseHitSize: 14
    property real touchHitSize: Theme.androidMinTouchTarget
    // Rotates the bracket's corner, in degrees; 0 puts the corner at the top-left.
    property real bracketAngle: 0
    // Where the point arguments of the signals are expressed.
    property Item space: parent

    readonly property bool active: area.dragging
    readonly property bool hovered: area.hot
    readonly property bool pressedByTouch: area.pressedByTouch

    // point is in `space`; modifiers is the event's Qt.KeyboardModifiers; device is a DragSession.Device.
    signal pressed(point point, int modifiers, int device)
    signal moved(point point, int modifiers, int device)
    signal released(point point, int modifiers, int device)
    signal clicked(point point, int modifiers, int device)

    width: touchHitSize
    height: touchHitSize
    // Callers place the knob by its centre.
    transformOrigin: Item.Center

    readonly property real grow: knob.active ? 1.5 : (knob.hovered ? 1.3 : 1.0)
    readonly property real drawn: visualSize * grow
    readonly property color fill: knob.active ? Theme.c.manipHandle.backgroundActive
                                : knob.selected ? Theme.c.manipHandle.backgroundSelected
                                : knob.hovered ? Theme.c.manipHandle.backgroundHover
                                : Theme.c.manipHandle.background

    Item {
        id: glyph
        anchors.centerIn: parent
        width: knob.drawn
        height: knob.drawn


        Rectangle {
            visible: knob.shape === "dot" || knob.shape === "square"
            anchors.fill: parent
            radius: knob.shape === "dot" ? width / 2 : Theme.c.cropOverlayGrip.radius
            color: knob.fill
            border.width: Theme.c.manipHandle.borderWidth
            border.color: knob.hovered || knob.active ? Theme.c.manipHandle.borderHover
                                                       : Theme.c.manipHandle.border
        }

        Rectangle {
            visible: knob.shape === "diamond"
            anchors.centerIn: parent
            width: parent.width * 0.8
            height: width
            rotation: 45
            color: knob.fill
            border.width: Theme.c.manipHandle.borderWidth
            border.color: knob.hovered || knob.active ? Theme.c.manipHandle.borderHover
                                                       : Theme.c.manipHandle.border
        }

        Item {
            visible: knob.shape === "bracket"
            anchors.fill: parent
            rotation: knob.bracketAngle

            Rectangle {
                x: 0
                y: 0
                width: parent.width
                height: Theme.c.manipHandle.borderWidth * 2
                color: knob.fill
                border.width: 1
                border.color: Theme.c.manipHandle.outline
            }
            Rectangle {
                x: 0
                y: 0
                width: Theme.c.manipHandle.borderWidth * 2
                height: parent.height
                color: knob.fill
                border.width: 1
                border.color: Theme.c.manipHandle.outline
            }
        }
    }

    Item {
        id: cursorItem
        anchors.fill: parent
    }

    ToolCursor {
        target: cursorItem
        shape: typeof knob.cursor === "string" ? knob.cursor : ""
    }

    MouseArea {
        id: area
        anchors.fill: parent
        enabled: knob.enabled
        hoverEnabled: true
        preventStealing: true
        acceptedButtons: Qt.LeftButton
        cursorShape: typeof knob.cursor === "number" ? knob.cursor : Qt.ArrowCursor

        property bool pressedByTouch: false
        property bool dragging: false
        property bool near: false
        property bool moved: false
        readonly property bool hot: near || dragging

        function reach(mouse) {
            const half = (mouse.source !== Qt.MouseEventNotSynthesized ? knob.touchHitSize : knob.mouseHitSize) / 2
            return Math.abs(mouse.x - width / 2) <= half && Math.abs(mouse.y - height / 2) <= half
        }

        function device() {
            return pressedByTouch ? DragSession.Touch : DragSession.Mouse
        }

        function spot(mouse) {
            return knob.space ? mapToItem(knob.space, mouse.x, mouse.y) : Qt.point(mouse.x, mouse.y)
        }

        onPressed: (mouse) => {
            if (!reach(mouse)) {
                mouse.accepted = false
                return
            }
            pressedByTouch = mouse.source !== Qt.MouseEventNotSynthesized
            dragging = true
            moved = false
            knob.pressed(spot(mouse), mouse.modifiers, device())
        }

        onPositionChanged: (mouse) => {
            if (!dragging) {
                near = mouse.source === Qt.MouseEventNotSynthesized && reach(mouse)
                return
            }
            moved = true
            knob.moved(spot(mouse), mouse.modifiers, device())
        }

        function finish(mouse) {
            if (!dragging)
                return
            dragging = false
            const m = mouse ? mouse.modifiers : Qt.NoModifier
            const p = mouse ? spot(mouse) : Qt.point(0, 0)
            knob.released(p, m, device())
            if (!moved && mouse)
                knob.clicked(p, m, device())
        }

        onReleased: (mouse) => finish(mouse)
        onCanceled: finish(null)
        onExited: near = false

        // A MouseArea accepts wheel events even with no handler, which would stop a zoom over a grip.
        onWheel: (wheel) => { wheel.accepted = false }
    }
}
