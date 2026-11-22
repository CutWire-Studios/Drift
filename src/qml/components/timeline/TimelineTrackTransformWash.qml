import QtQuick
import Drift

// Faint wash on a transform layer's row.
Rectangle {
    property var panel
    property int trackIndex: -1
    anchors.fill: parent
    visible: panel.tracks[trackIndex].isTransformLayer === true
    color: Qt.alpha(Theme.c.timelineTrackTransformWash.background, Theme.c.timelineTrackTransformWash.backgroundOpacity)
}
