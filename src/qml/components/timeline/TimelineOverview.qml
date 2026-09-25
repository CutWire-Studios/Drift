import QtQuick
import Drift
import ".."

// Resolve-style project overview: the entire timeline compressed to fit this
// strip's width, with a rectangle marking what the zoomed track view below
// currently shows. Click or drag anywhere to recenter that view on the
// clicked point — the equivalent of dragging the minimap in Resolve/Premiere.
//
// Can be switched off (EditorState.timelineOverviewVisible) on a timeline long
// enough that repainting every clip on every edit is felt.
Item {
    id: overview

    // Owning TimelinePanel; reads pxPerSecond/timelineViewX/timelineViewW and
    // calls scrollToX to move the zoomed view.
    property var panel

    height: Theme.timelineOverviewHeight

    readonly property real duration: Math.max(EditorState.durationSeconds, 1)
    readonly property real overviewPxPerSecond: width / duration

    // Matches drift::ClipType's order, which is what timelineOverviewBlocks() emits.
    function blockColor(typeCode) {
        switch (typeCode) {
        case 1: return Theme.clipAudio       // audio
        case 3: return Theme.clipText        // text
        case 4: return Theme.clipSubtitle    // subtitle
        case 6: return Theme.clipEffect      // adjustment
        case 2:                              // image
        case 5:                              // shape
        case 7:                              // vector
        case 8: return Theme.clipGraphic     // 3D model
        default: return Theme.clipVideoOverview
        }
    }

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

    // Flattened content map: one thin band per track, each clip drawn in its type's colour.
    //
    // Painted rather than instantiated. This was a Rectangle per clip in the whole project, none
    // of them cullable, each re-running its x and width bindings on every edit — for a strip
    // where most blocks are a pixel wide. One item now, repainted on the revision counter, and
    // fed a flat array of numbers from C++ rather than walking the clip graph in JS.
    Canvas {
        id: contentMap
        anchors.fill: parent

        // [lane, typeCode, startSeconds, durationSeconds] per clip.
        property var blocks: []
        property int laneCount: 1
        // The scale the blocks are drawn at, folding in both the project duration and this
        // strip's width.
        readonly property real pps: overview.overviewPxPerSecond
        readonly property int revision: EditorState.tracksRevision

        function refresh() {
            if (!overview.visible)
                return
            blocks = EditorState.timelineOverviewBlocks()
            laneCount = Math.max(1, EditorState.timelineOverviewLaneCount())
            requestPaint()
        }

        onRevisionChanged: refresh()
        onPpsChanged: requestPaint()
        onHeightChanged: requestPaint()
        Component.onCompleted: refresh()

        Connections {
            target: overview
            function onVisibleChanged() { contentMap.refresh() }
        }

        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()
            if (!panel || overview.width <= 0)
                return
            const top = 3
            const available = Math.max(1, overview.height - 7)
            // One pixel of gap between bands, so adjacent tracks stay countable.
            const laneHeight = Math.max(2, available / contentMap.laneCount - 1)
            const laneStride = available / contentMap.laneCount
            const blocks = contentMap.blocks
            for (let i = 0; i + 3 < blocks.length; i += 4) {
                ctx.fillStyle = overview.blockColor(blocks[i + 1])
                ctx.fillRect(blocks[i + 2] * contentMap.pps, top + blocks[i] * laneStride,
                             Math.max(1, blocks[i + 3] * contentMap.pps), laneHeight)
            }
        }
    }

    // Viewport indicator: the slice of the project the zoomed track view below is currently
    // showing. Drawn as a window rather than a tint — the strip is full of colour now, and a
    // translucent fill over it read as nothing at all.
    QtObject {
        id: viewport
        readonly property real viewStartSeconds: panel ? panel.timelineViewX / panel.pxPerSecond : 0
        readonly property real viewEndSeconds: panel ? (panel.timelineViewX + panel.timelineViewW) / panel.pxPerSecond : 0
        // Clamp both edges independently: the visible range can run past
        // `duration` into the Flickable's trailing end pad (always present,
        // and often the whole pad at Fit zoom), which this strip's scale
        // does not otherwise account for — unclamped, the indicator would
        // draw wider than the strip itself.
        readonly property real startX: Math.max(0, Math.min(overview.width, viewStartSeconds * overview.overviewPxPerSecond))
        readonly property real endX: Math.max(0, Math.min(overview.width, viewEndSeconds * overview.overviewPxPerSecond))
    }

    Rectangle {
        x: 0
        width: viewport.startX
        y: 0
        height: parent.height - 1
        color: Theme.panelBackground
        opacity: 0.6
    }

    Rectangle {
        x: viewport.endX
        width: Math.max(0, overview.width - viewport.endX)
        y: 0
        height: parent.height - 1
        color: Theme.panelBackground
        opacity: 0.6
    }

    Rectangle {
        x: viewport.startX
        width: Math.max(3, viewport.endX - viewport.startX)
        y: 1
        height: parent.height - 3
        radius: 2
        color: "transparent"
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
