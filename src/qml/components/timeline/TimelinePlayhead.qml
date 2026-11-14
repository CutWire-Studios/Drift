import QtQuick
import Drift

// Playhead line and scrubber head.
Item {
    id: playhead

    property var panel
    property Flickable view
    property var interpolator
    readonly property bool headDragging: playheadDragArea.drag.active
    y: 0
    // Above the seek strip (z: 2), otherwise the scrub area
    // covers the handle and swallows the press before drag starts.
    z: 3
    width: Theme.playheadLineWidth
    height: Theme.timelineRulerHeight + Theme.timelineBookmarkRowHeight
            + panel.totalTracksHeightCached

    Binding {
        target: playhead
        property: "x"
        value: interpolator.seconds * panel.pxPerSecond
        when: !playheadDragArea.drag.active && !playheadLineDrag.drag.active
    }

    // Follow either drag live so the preview scrubs with it.
    onXChanged: {
        if (playheadDragArea.drag.active || playheadLineDrag.drag.active)
            EditorState.playheadSeconds = playhead.x / panel.pxPerSecond
    }

    function finishSeek() {
        EditorState.playheadSeconds =
            EditorState.snapTime(playhead.x / panel.pxPerSecond)
    }

    TimelinePlayheadLine { }

    TimelinePlayheadHead { }

    // Wide grab across the seek strip head.
    MouseArea {
        id: playheadDragArea
        width: Theme.playheadSeekGrabWidth
        height: Theme.timelineRulerHeight + Theme.timelineBookmarkRowHeight
        x: -(width - Theme.playheadLineWidth) / 2
        y: 0
        preventStealing: true
        hoverEnabled: true
        TimelineCursorRequest {
            kind: "playheadScrub"
            active: playheadDragArea.containsMouse || playheadDragArea.pressed
        }
        drag.target: playhead
        drag.axis: Drag.XAxis
        drag.threshold: 0
        drag.minimumX: 0
        drag.maximumX: view.contentWidth - Theme.playheadLineWidth
        onPressed: panel.beginPlayheadSeek()
        onReleased: {
            playhead.finishSeek()
            panel.endPlayheadSeek()
        }
        onCanceled: {
            playhead.finishSeek()
            panel.endPlayheadSeek()
        }
    }

    // Narrow grab down the timeline line — stays thin so clip
    // clicks aren't stolen.
    MouseArea {
        id: playheadLineDrag
        width: 7
        height: parent.height - playheadDragArea.height
        x: -(width - Theme.playheadLineWidth) / 2
        y: playheadDragArea.height
        preventStealing: true
        hoverEnabled: true
        TimelineCursorRequest {
            kind: "playheadScrub"
            active: playheadLineDrag.containsMouse || playheadLineDrag.pressed
        }
        drag.target: playhead
        drag.axis: Drag.XAxis
        drag.threshold: 0
        drag.minimumX: 0
        drag.maximumX: view.contentWidth - Theme.playheadLineWidth
        onPressed: panel.beginPlayheadSeek()
        onReleased: {
            playhead.finishSeek()
            panel.endPlayheadSeek()
        }
        onCanceled: {
            playhead.finishSeek()
            panel.endPlayheadSeek()
        }
    }
}
