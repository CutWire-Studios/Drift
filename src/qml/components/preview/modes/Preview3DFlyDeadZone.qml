import QtQuick
import Drift

// The still zone, so there is something to aim the pointer at.
Rectangle {
    id: zone

    required property bool steering
    required property real fraction

    x: parent.width / 2 - width / 2
    y: parent.height / 2 - height / 2
    width: zone.fraction * parent.width
    height: zone.fraction * parent.height
    radius: Theme.radiusPill
    color: "transparent"
    border.width: Theme.c.preview3DFlyDeadZone.borderWidth
    border.color: zone.steering ? Theme.c.preview3DFlyDeadZone.borderActive : Theme.c.preview3DFlyDeadZone.border
}
