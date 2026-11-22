import QtQuick
import QtQuick.Controls.Basic
import Drift

// Centre slot of the bottom rail: everything that puts a new clip on the timeline.
AbstractButton {
    id: addButton

    signal requested()

    height: parent ? parent.height : 0
    hoverEnabled: true

    Accessible.role: Accessible.Button
    Accessible.name: qsTr("Add to timeline")

    scale: addButton.down ? Theme.pressScale : 1.0

    Behavior on scale {
        NumberAnimation { duration: Theme.durationPress; easing.type: Theme.easing }
    }

    background: Item { }

    contentItem: Item {
        anchors.fill: parent

        Rectangle {
            anchors.centerIn: parent
            width: Theme.androidRailFabSize
            height: Theme.androidRailFabSize
            radius: Theme.c.androidRailAddButton.radius
            // The one filled control in the shell, so "add something" reads
            // as the rail's primary action rather than a fifth tab. `primary`
            // is a fill here, not a foreground, so it needs no light-mode
            // variant — primaryForeground is 9.8:1 on it in both themes.
            color: addButton.down ? Theme.c.androidRailAddButton.backgroundPressed
                                  : Theme.c.androidRailAddButton.background

            Behavior on color {
                ColorAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
            }

            IconGlyph {
                anchors.centerIn: parent
                glyph: Theme.icons.plus
                iconSize: Theme.iconSizeXl
                iconColor: Theme.c.androidRailAddButton.icon
            }
        }
    }

    onClicked: {
        Haptics.confirm()
        addButton.requested()
    }
}
