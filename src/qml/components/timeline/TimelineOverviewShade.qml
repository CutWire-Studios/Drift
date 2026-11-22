import QtQuick
import Drift

// Dims the part of the minimap outside the current viewport.
Rectangle {
    property real spanX: 0
    property real spanWidth: 0
    x: spanX
    width: spanWidth
    y: 0
    height: parent.height - 1
    color: Theme.c.timelineOverviewShade.background
    opacity: Theme.c.timelineOverviewShade.backgroundOpacity
}
