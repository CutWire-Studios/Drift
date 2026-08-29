import QtQuick
import Drift

// Slim determinate progress bar for long-running jobs shown inline in a panel.
//
// The large ring (LabelledProgressRing) is for dialogs that have the room; this is what a panel
// section uses when a job needs a bar next to a status line and a Cancel button.
Item {
    id: root

    // 0..1, clamped.
    property real value: 0
    property real barHeight: Theme.spacingMd

    implicitHeight: barHeight
    height: barHeight

    Rectangle {
        anchors.fill: parent
        radius: height / 2
        color: Theme.panelMuted

        Rectangle {
            width: parent.width * Math.max(0, Math.min(1, root.value))
            height: parent.height
            radius: parent.radius
            color: Theme.primary

            Behavior on width {
                NumberAnimation { duration: Theme.durationBase; easing.type: Theme.easing }
            }
        }
    }
}
