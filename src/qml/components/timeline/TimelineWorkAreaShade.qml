import QtQuick
import Drift

// Translucent band between the work-area in and out points.
Rectangle {
    property bool active: false
    property real spanX: 0
    property real spanWidth: 0

    visible: active
    x: spanX
    width: spanWidth
    anchors.top: parent.top
    anchors.bottom: parent.bottom
    color: Qt.alpha(Theme.c.timelineWorkAreaShade.background, Theme.c.timelineWorkAreaShade.backgroundOpacity)
}
