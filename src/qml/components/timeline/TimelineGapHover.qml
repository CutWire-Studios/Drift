import QtQuick
import Drift

// Hover tint on an empty gap between clips.
Rectangle {
    property bool hovered: false
    anchors.fill: parent
    visible: hovered
    color: Qt.alpha(Theme.c.timelineGapHover.background, Theme.c.timelineGapHover.backgroundOpacity)
}
