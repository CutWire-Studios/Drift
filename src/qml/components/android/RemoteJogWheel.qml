import QtQuick
import Drift

// An endless wheel: turning it reports the angle moved, in degrees, as `turned`.
Item {
    id: root

    property real degreesPerTick: 12
    property real angle: 0
    property real _last: 0

    signal turned(real ticks)

    implicitWidth: 200
    implicitHeight: 200

    Rectangle {
        anchors.fill: parent
        radius: width / 2
        color: Theme.c.phoneRemote.wheelBackground
        border.width: Theme.c.phoneRemote.borderWidth
        border.color: Theme.c.phoneRemote.wheelBorder
    }

    Item {
        anchors.fill: parent
        rotation: root.angle

        Repeater {
            model: 24

            Rectangle {
                required property int index
                width: index % 6 === 0 ? 3 : 2
                height: index % 6 === 0 ? 14 : 8
                radius: width / 2
                color: Theme.c.phoneRemote.wheelTick
                x: root.width / 2 - width / 2
                y: 8
                transformOrigin: Item.Bottom
                transform: Rotation {
                    origin.x: width / 2
                    origin.y: root.height / 2 - 8
                    angle: index * 15
                }
            }
        }

        Rectangle {
            width: 14
            height: 14
            radius: 7
            color: Theme.c.phoneRemote.wheelThumb
            x: root.width / 2 - width / 2
            y: 26
        }
    }

    MouseArea {
        anchors.fill: parent
        preventStealing: true
        onPressed: mouse => root._last = Math.atan2(mouse.y - height / 2, mouse.x - width / 2) * 180 / Math.PI
        onPositionChanged: mouse => {
            const now = Math.atan2(mouse.y - height / 2, mouse.x - width / 2) * 180 / Math.PI
            let delta = now - root._last
            if (delta > 180)
                delta -= 360
            else if (delta < -180)
                delta += 360
            root._last = now
            root.angle += delta
            root.turned(delta / root.degreesPerTick)
        }
    }
}
