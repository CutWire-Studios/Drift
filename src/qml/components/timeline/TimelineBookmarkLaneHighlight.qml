import QtQuick
import Drift

// Work area highlight in the bookmark lane (between In/Out markers).
Rectangle {
    property real pxPerSecond: 1

    visible: EditorState.workAreaActive
    y: Theme.timelineRulerHeight
    height: Theme.timelineBookmarkRowHeight
    x: EditorState.workAreaInSeconds * pxPerSecond
    width: Math.max(0, (EditorState.workAreaOutSeconds - EditorState.workAreaInSeconds) * pxPerSecond)
    color: Qt.alpha(Theme.c.timelineBookmarkLaneHighlight.background, Theme.c.timelineBookmarkLaneHighlight.backgroundOpacity)
    z: 1
}
