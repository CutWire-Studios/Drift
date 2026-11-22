import QtQuick
import Drift

// Where a dragged transition would land.
Rectangle {
    property var panel
    property int trackIndex: -1
    visible: panel.transitionDropTrackIndex === trackIndex
    x: panel.transitionDropStart * panel.pxPerSecond
    width: Math.max(8, (panel.transitionDropEnd - panel.transitionDropStart) * panel.pxPerSecond)
    height: parent.height
    radius: Theme.c.timelineTransitionDropOutline.radius
    color: Qt.alpha(Theme.c.timelineTransitionDropOutline.background, Theme.c.timelineTransitionDropOutline.backgroundOpacity)
    border.width: Theme.c.timelineTransitionDropOutline.borderWidth
    border.color: Theme.c.timelineTransitionDropOutline.border
    z: 11
}
