import QtQuick
import Drift

Row {
    id: root

    property int count: 0
    property int current: 0

    spacing: Theme.spacingSm

    Repeater {
        model: root.count

        Rectangle {
            required property int index
            readonly property bool active: index === root.current

            anchors.verticalCenter: parent.verticalCenter
            width: active ? 22 : 8
            height: 8
            radius: height / 2
            color: active ? Theme.c.quickSetup.dotActive : Theme.c.quickSetup.dotInactive

            Behavior on width {
                NumberAnimation { duration: Theme.durationBase; easing.type: Theme.easing }
            }
            Behavior on color {
                ColorAnimation { duration: Theme.durationBase }
            }
        }
    }

    Accessible.role: Accessible.ProgressBar
    Accessible.name: qsTr("Step %1 of %2").arg(root.current + 1).arg(root.count)
}
