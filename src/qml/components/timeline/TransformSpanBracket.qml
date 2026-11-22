import QtQuick
import Drift

// The span of one transform layer, drawn in the track header column: a line from the middle of
// the layer's row down to the bottom of the last row it covers, with a tick at the top and a foot
// at the bottom. The foot is the only interactive part — dragging it re-targets the span end,
// snapping to the ends transformSpanOptions allows.
Item {
    id: root

    // Supplied by the header column.
    property var tracks: []
    property int layerIndex: -1
    property real contentY: 0
    property real indentStep: 8
    // Header geometry, shared with the rows: function(index) -> content y / height.
    property var rowTop: null
    property var rowHeight: null

    // While the foot is dragged: the end it would snap to, for the header tint and the chip.
    readonly property int previewEnd: dragArea.active && dragArea.snapEnd >= 0 ? dragArea.snapEnd : -1
    signal scrollRequested(real dy)

    readonly property var layerTrack: layerIndex >= 0 && layerIndex < tracks.length ? tracks[layerIndex] : null
    readonly property int endIndex: layerTrack ? layerTrack.spanEndIndex : -1
    readonly property int shownEnd: previewEnd >= 0 ? previewEnd : endIndex
    readonly property int depth: layerTrack ? layerTrack.spanDepth : 0
    readonly property bool bypassed: layerTrack ? layerTrack.hidden === true : false

    // Full strength while the layer, or anything it moves, is selected.
    readonly property bool active: {
        const t = EditorState.selectedTrack
        if (t < 0 || !layerTrack)
            return false
        if (t === layerIndex)
            return true
        const coveredBy = tracks[t] ? tracks[t].transformCoveredBy : undefined
        return coveredBy !== undefined && coveredBy.indexOf(layerIndex) !== -1
    }

    readonly property real topY: rowTop ? rowTop(layerIndex) + rowHeight(layerIndex) / 2 - contentY : 0
    readonly property real bottomY: shownEnd >= 0 && rowTop
                                    ? rowTop(shownEnd) + rowHeight(shownEnd) - contentY - 3
                                    : topY
    readonly property real lineX: 3 + depth * indentStep

    visible: layerTrack !== null && shownEnd > layerIndex
    opacity: bypassed ? 0.3 : (active || dragArea.active ? 1.0 : 0.6)

    Behavior on opacity {
        NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
    }

    // Draws itself downward the first time it appears.
    property real reveal: 0
    Component.onCompleted: reveal = 1
    Behavior on reveal {
        NumberAnimation { duration: Theme.durationBase; easing.type: Theme.easing }
    }

    readonly property real drawnBottom: topY + (bottomY - topY) * reveal

    TimelineSpanBracketTick {
        lineX: root.lineX
        topY: root.topY
        indentStep: root.indentStep
    }

    // The line: solid, or dashed while the layer is turned off.
    TimelineSpanBracketLine {
        shown: !root.bypassed
        lineX: root.lineX
        topY: root.topY
        drawnBottom: root.drawnBottom
    }
    Column {
        visible: root.bypassed
        x: root.lineX
        y: root.topY
        spacing: 3
        Repeater {
            model: Math.max(0, Math.floor((root.drawnBottom - root.topY) / 7))
            delegate: TimelineSpanBracketDash { }
        }
    }

    // The foot, and its handle.
    TimelineSpanBracketFoot {
        lineX: root.lineX
        drawnBottom: root.drawnBottom
        indentStep: root.indentStep
        hot: dragArea.hovered || dragArea.active
    }

    TimelineSpanBracketChip {
        shown: dragArea.active
        lineX: root.lineX
        indentStep: root.indentStep
        bottomY: root.bottomY
        snapCount: dragArea.snapCount
    }

    HandleKnob {
        id: dragArea
        releaseOnCancel: false
        onCanceled: EditorState.drag.cancel()
        x: root.lineX - width / 2 + 1
        y: root.bottomY - height / 2
        shape: "none"
        space: root
        cursor: Qt.SizeVerCursor

        property var options: []
        property int snapEnd: -1
        property int snapCount: 0

        Accessible.role: Accessible.Slider
        Accessible.name: qsTr("Transform layer span end")
        Accessible.description: qsTr("Covers %n track(s)", "", dragArea.coveredCount())

        function coveredCount() {
            const layerTrack = root.layerTrack
            if (!layerTrack)
                return 0
            const options = EditorState.transformSpanOptions(root.layerIndex)
            for (let i = 0; i < options.length; ++i) {
                if (options[i].endIndex === root.endIndex)
                    return options[i].count
            }
            return 0
        }

        function snapTo(contentYAtPointer) {
            let best = -1
            let bestDistance = 1e9
            let count = 0
            for (let i = 0; i < options.length; ++i) {
                const end = options[i].endIndex
                const bottom = root.rowTop(end) + root.rowHeight(end)
                const distance = Math.abs(bottom - contentYAtPointer)
                if (distance < bestDistance) {
                    bestDistance = distance
                    best = end
                    count = options[i].count
                }
            }
            if (best !== snapEnd && best >= 0)
                Haptics.lane(best)
            snapEnd = best
            snapCount = count
        }

        onPressed: (point, modifiers, device) => {
            options = EditorState.transformSpanOptions(root.layerIndex)
            snapEnd = root.endIndex
            snapCount = coveredCount()
            Haptics.pickUp()
            EditorState.drag.begin("transformSpan", device, qsTr("Change transform span"))
        }
        onMoved: (point, modifiers, device) => {
            const p = point
            snapTo(p.y + root.contentY)
            // Near an edge the list scrolls, so an end off screen is reachable.
            const edge = 24
            if (p.y < edge)
                root.scrollRequested(-(edge - p.y))
            else if (p.y > root.height - edge)
                root.scrollRequested(p.y - (root.height - edge))
        }
        onReleased: {
            EditorState.drag.commit()
            const end = snapEnd
            snapEnd = -1
            if (end < 0 || end === root.endIndex) {
                Haptics.drop()
                return
            }
            for (let i = 0; i < options.length; ++i) {
                if (options[i].endIndex === end) {
                    Haptics.confirm()
                    EditorState.setTransformSpan(root.layerIndex, options[i].endId)
                    return
                }
            }
        }

        Connections {
            target: EditorState.drag
            function onCancelled() {
                if (dragArea.active)
                    dragArea.snapEnd = -1
            }
        }
    }
}
