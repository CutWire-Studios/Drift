import QtQuick
import Drift

// Duration handle that sticks out of a transition capsule end: a small rounded ear with two grip
// lines. Purely visual; the MouseArea it sits in does the resizing.
Rectangle {
    id: ear

    property bool shown: false
    property color gripColor: "transparent"
    property color outlineColor: "transparent"

    readonly property real overlap: 2

    implicitWidth: 8
    radius: 3
    opacity: shown ? 1 : 0
    scale: shown ? 1 : 0.7
    border.width: 1
    border.color: outlineColor

    Behavior on opacity {
        NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
    }
    Behavior on scale {
        NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
    }

    Repeater {
        model: 2
        delegate: Rectangle {
            required property int index
            width: 1
            height: Math.max(4, Math.round(ear.height * 0.45))
            anchors.verticalCenter: parent.verticalCenter
            x: Math.round(ear.width / 2) - 2 + index * 2
            color: ear.gripColor
        }
    }
}
