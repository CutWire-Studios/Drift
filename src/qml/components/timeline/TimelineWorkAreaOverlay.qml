import QtQuick
import Drift

// Work area overlay across the track rows.
Item {
    id: workAreaOverlay

    property var panel
    property Flickable view
    property real headerHeight: 0
    z: 1
    y: Theme.timelineRulerHeight + Theme.timelineBookmarkRowHeight
    width: parent.width
    height: Math.max(panel.totalTracksHeightCached + Theme.trackGap,
                     view.height - headerHeight)

    readonly property real inX: EditorState.workAreaInSeconds >= 0
                               ? EditorState.workAreaInSeconds * panel.pxPerSecond
                               : -1
    readonly property real outX: EditorState.workAreaOutSeconds >= 0
                                ? EditorState.workAreaOutSeconds * panel.pxPerSecond
                                : -1

    TimelineWorkAreaShade {
        active: EditorState.workAreaActive
        spanX: parent.inX
        spanWidth: Math.max(0, parent.outX - parent.inX)
    }

    TimelineWorkAreaEdge {
        shown: EditorState.workAreaInSeconds >= 0
        edgeX: parent.inX
    }

    TimelineWorkAreaEdge {
        shown: EditorState.workAreaOutSeconds >= 0
        edgeX: parent.outX - width
    }

    TimelineWorkAreaBakeChip {
        visible: EditorState.workAreaInSeconds >= 0 && EditorState.workAreaOutSeconds > EditorState.workAreaInSeconds
        x: Math.max(parent.inX + Theme.spacingMd, parent.outX - width - Theme.spacingMd)
        y: workAreaOverlay.view.contentY + Theme.spacingMd
        z: 2
    }
}
