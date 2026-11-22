import QtQuick
import Drift

// Body of the ghost lane a drop would insert.
Rectangle {
    anchors.fill: parent
    radius: Theme.c.timelineNewTrackGhostLane.radius
    color: Qt.alpha(Theme.c.timelineNewTrackGhostLane.background, Theme.c.timelineNewTrackGhostLane.backgroundOpacity)
    border.width: Theme.c.timelineNewTrackGhostLane.borderWidth
    border.color: Theme.c.timelineNewTrackGhostLane.border
}
