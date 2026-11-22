import QtQuick
import Drift

// In or out marker of the work area.
Rectangle {
    property bool shown: false
    property real edgeX: 0

    visible: shown
    x: edgeX
    width: 2
    anchors.top: parent.top
    anchors.bottom: parent.bottom
    color: Theme.c.timelineWorkAreaEdge.foreground
    opacity: Theme.c.timelineWorkAreaEdge.foregroundOpacity
}
