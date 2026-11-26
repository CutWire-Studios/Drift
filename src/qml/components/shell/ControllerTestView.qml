import QtQuick
import Drift

// Live view of the controller state the editor sees: both sticks after dead zone and curve, the
// triggers, and the buttons currently held.
Column {
    id: view

    spacing: Theme.spacingMd

    Row {
        spacing: Theme.spacingLg

        Repeater {
            model: [
                { x: Gamepad.leftX, y: Gamepad.leftY },
                { x: Gamepad.rightX, y: Gamepad.rightY }
            ]

            Rectangle {
                required property var modelData
                width: 72
                height: 72
                radius: width / 2
                color: Theme.c.controllerTest.background
                border.width: Theme.c.controllerTest.borderWidth
                border.color: Theme.c.controllerTest.border

                Rectangle {
                    width: 12
                    height: 12
                    radius: 6
                    color: Theme.c.controllerTest.marker
                    x: parent.width / 2 - width / 2 + modelData.x * (parent.width / 2 - width / 2)
                    y: parent.height / 2 - height / 2 - modelData.y * (parent.height / 2 - height / 2)
                }
            }
        }

        Repeater {
            model: [Gamepad.leftTrigger, Gamepad.rightTrigger]

            Rectangle {
                required property real modelData
                width: 14
                height: 72
                radius: Theme.radiusSm
                color: Theme.c.controllerTest.background
                border.width: Theme.c.controllerTest.borderWidth
                border.color: Theme.c.controllerTest.border

                Rectangle {
                    anchors.bottom: parent.bottom
                    width: parent.width
                    height: parent.height * modelData
                    radius: parent.radius
                    color: Theme.c.controllerTest.marker
                }
            }
        }
    }

    Text {
        width: parent.width
        text: Gamepad.pressed.length > 0 ? Gamepad.pressed.join("  ") : qsTr("No buttons held")
        color: Theme.c.controllerTest.foreground
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSizeSm
        wrapMode: Text.WordWrap
    }
}
