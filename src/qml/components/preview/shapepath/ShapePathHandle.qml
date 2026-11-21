import QtQuick
import Drift

// A tangent handle's knob. The line from it to its vertex is drawn by the overlay's Shape.
Rectangle {
    id: knob

    required property var info

    readonly property real side: Theme.spacingMd * 1.1

    x: info.px - side / 2
    y: info.py - side / 2
    width: side
    height: side
    radius: side / 2
    z: 4
    color: Theme.c.shapePathOverlay.handle
    border.width: 1
    border.color: Theme.c.shapePathOverlay.handleBorder
}
