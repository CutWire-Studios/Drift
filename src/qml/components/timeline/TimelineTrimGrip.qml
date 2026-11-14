import QtQuick
import Drift

// Two short lines: says "this edge moves" without a tooltip, which touch cannot show.
Row {
    anchors.centerIn: parent
    spacing: 2

    Repeater {
        model: 2
        delegate: Rectangle {
            width: 2
            height: Math.min(14, parent.parent.height * 0.35)
            radius: Theme.c.timelineTrimGrip.radius
            color: Theme.c.timelineTrimGrip.foreground
            opacity: 0.9
        }
    }
}
