import QtQuick
import Drift

// Dims everything outside the kept rect. Four bands rather than a mask: no shader, no clipping
// cost. `extent` is the area the bands cover, `frame` the kept rect, both in this item's coordinates.
Item {
    id: scrim

    property rect frame: Qt.rect(0, 0, 0, 0)
    property rect extent: Qt.rect(0, 0, 0, 0)

    readonly property real left: Math.max(extent.x, frame.x)
    readonly property real right: Math.min(extent.x + extent.width, frame.x + frame.width)
    readonly property real top: Math.max(extent.y, frame.y)
    readonly property real bottom: Math.min(extent.y + extent.height, frame.y + frame.height)

    Rectangle {
        color: Theme.c.cropFrame.scrim
        opacity: Theme.c.cropFrame.scrimOpacity
        x: scrim.extent.x
        y: scrim.extent.y
        width: Math.max(0, scrim.frame.x - scrim.extent.x)
        height: scrim.extent.height
    }
    Rectangle {
        color: Theme.c.cropFrame.scrim
        opacity: Theme.c.cropFrame.scrimOpacity
        x: scrim.frame.x + scrim.frame.width
        y: scrim.extent.y
        width: Math.max(0, scrim.extent.x + scrim.extent.width - x)
        height: scrim.extent.height
    }
    Rectangle {
        color: Theme.c.cropFrame.scrim
        opacity: Theme.c.cropFrame.scrimOpacity
        x: scrim.frame.x
        y: scrim.extent.y
        width: Math.max(0, scrim.frame.width)
        height: Math.max(0, scrim.frame.y - scrim.extent.y)
    }
    Rectangle {
        color: Theme.c.cropFrame.scrim
        opacity: Theme.c.cropFrame.scrimOpacity
        x: scrim.frame.x
        y: scrim.frame.y + scrim.frame.height
        width: Math.max(0, scrim.frame.width)
        height: Math.max(0, scrim.extent.y + scrim.extent.height - y)
    }
}
