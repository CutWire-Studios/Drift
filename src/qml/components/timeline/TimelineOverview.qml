import QtQuick
import Drift
import ".."

// Resolve-style project overview: the entire timeline compressed to fit this
// strip's width, with a rectangle marking what the zoomed track view below
// currently shows. Click or drag anywhere to recenter that view on the
// clicked point — the equivalent of dragging the minimap in Resolve/Premiere.
Item {
    id: overview

    // Owning TimelinePanel; reads pxPerSecond/timelineViewX/timelineViewW and
    // calls scrollToX to move the zoomed view.
    property var panel

    height: Theme.timelineOverviewHeight

    readonly property real duration: Math.max(EditorState.durationSeconds, 1)
    readonly property real overviewPxPerSecond: width / duration

    function xToSeconds(x) {
        return Math.max(0, Math.min(duration, x / overviewPxPerSecond))
    }

    // Recenter the zoomed view on `x` (overview-local coordinate), clamped to
    // the content range so the viewport rectangle never runs past either end.
    function recenterOn(x) {
        const targetSeconds = xToSeconds(x)
        const viewSeconds = panel.timelineViewW / panel.pxPerSecond
        const maxContentX = Math.max(0, panel.timelineContentWidth - panel.timelineViewW)
        const newContentX = Math.max(0, Math.min(maxContentX,
            (targetSeconds - viewSeconds / 2) * panel.pxPerSecond))
        panel.scrollToX(newContentX)
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.panelBackground
    }

    Rectangle {
        anchors.bottom: parent.bottom
        width: parent.width
        height: 1
        color: Theme.panelBorder
    }

    // Flattened content map: every clip on every track, drawn as a thin
    // translucent block at its project-time position.
    Repeater {
        model: panel ? panel.tracks.length : 0
        delegate: Repeater {
            required property int index
            readonly property var trackClips: panel.tracks[index].clips
            model: trackClips.length

            delegate: Rectangle {
                required property int index
                readonly property var clipData: trackClips[index]
                x: clipData.start * overview.overviewPxPerSecond
                width: Math.max(1, clipData.duration * overview.overviewPxPerSecond)
                y: 4
                height: overview.height - 8
                radius: 1
                color: Theme.primary
                opacity: 0.28
            }
        }
    }

    // Viewport indicator: the slice of the project the zoomed track view
    // below is currently showing.
    Rectangle {
        id: viewportRect
        readonly property real viewStartSeconds: panel ? panel.timelineViewX / panel.pxPerSecond : 0
        readonly property real viewEndSeconds: panel ? (panel.timelineViewX + panel.timelineViewW) / panel.pxPerSecond : 0
        // Clamp both edges independently: the visible range can run past
        // `duration` into the Flickable's trailing end pad (always present,
        // and often the whole pad at Fit zoom), which this strip's scale
        // does not otherwise account for — unclamped, the indicator would
        // draw wider than the strip itself.
        readonly property real clampedStartX: Math.max(0, Math.min(overview.width, viewStartSeconds * overview.overviewPxPerSecond))
        readonly property real clampedEndX: Math.max(0, Math.min(overview.width, viewEndSeconds * overview.overviewPxPerSecond))
        x: clampedStartX
        width: Math.max(3, clampedEndX - clampedStartX)
        y: 1
        height: parent.height - 2
        radius: 2
        color: Theme.primary
        opacity: 0.16
        border.width: 1
        border.color: Theme.primary
    }

    // Playhead tick.
    Rectangle {
        visible: overview.width > 0
        x: EditorState.playheadSeconds * overview.overviewPxPerSecond - width / 2
        y: 0
        width: 2
        height: parent.height
        color: Theme.destructive
    }

    MouseArea {
        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        preventStealing: true
        onPressed: (mouse) => overview.recenterOn(mouse.x)
        onPositionChanged: (mouse) => { if (pressed) overview.recenterOn(mouse.x) }
    }
}
