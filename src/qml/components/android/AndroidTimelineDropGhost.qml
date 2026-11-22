import QtQuick
import Drift

// Where a dragged clip or asset would land on an existing track.
Rectangle {
    id: ghost

    property int dropTrackIndex: -1
    property real dropStartSeconds: 0
    property real dropDurationSeconds: 0
    property real pxPerSecond: 1
    property real seekHeaderHeight: 0
    property real trackOffsetY: 0
    property real trackHeight: 0

    visible: ghost.dropTrackIndex >= 0
    x: ghost.dropStartSeconds * ghost.pxPerSecond
    y: ghost.seekHeaderHeight + ghost.trackOffsetY
    width: ghost.dropDurationSeconds * ghost.pxPerSecond
    height: ghost.dropTrackIndex >= 0 ? ghost.trackHeight : 0
    radius: Theme.c.androidTimelineDropGhost.radius
    color: Qt.alpha(Theme.c.androidTimelineDropGhost.background, Theme.c.androidTimelineDropGhost.backgroundOpacity)
    border.width: Theme.c.androidTimelineDropGhost.borderWidth
    border.color: Theme.c.androidTimelineDropGhost.border
    z: 5
}
