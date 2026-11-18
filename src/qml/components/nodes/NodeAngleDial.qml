import QtQuick
import Drift

// Small rotary control for an angle in degrees; 0 points up and the value grows clockwise.
Item {
    id: root

    property real value: 0
    property real from: 0
    property real to: 360
    readonly property alias pressed: area.pressed
    readonly property real _size: Math.min(width, height)

    signal moved(real value)

    implicitWidth: Theme.controlHeightSm - 4
    implicitHeight: implicitWidth

    Rectangle {
        anchors.centerIn: parent
        width: root._size
        height: width
        radius: width / 2
        color: Theme.c.nodeAngleDial.background
        border.width: 1
        border.color: Theme.c.nodeAngleDial.border
    }

    Rectangle {
        x: (root.width - width) / 2
        y: (root.height - root._size) / 2 + 3
        width: 2
        height: root._size / 2 - 3
        radius: 1
        color: Theme.c.nodeAngleDial.needle
        transformOrigin: Item.Bottom
        rotation: root.value
    }

    MouseArea {
        id: area
        anchors.fill: parent
        preventStealing: true

        function setFrom(m) {
            let a = Math.atan2(m.x - width / 2, -(m.y - height / 2)) * 180 / Math.PI
            if (a < 0)
                a += 360
            root.moved(Math.max(root.from, Math.min(root.to, a)))
        }

        onPressed: (m) => setFrom(m)
        onPositionChanged: (m) => { if (pressed) setFrom(m) }
    }
}
