import QtQuick
import Drift

// Resize grip on one edge of a subtitle cue. `edge` is -1 (start) or +1 (end).
Item {
    id: grip

    property var lane
    property var cue
    property int edge: -1

    property real pressSceneX: 0

    anchors.fill: parent

    Rectangle {
        x: grip.edge < 0 ? 0 : parent.width - width
        width: grip.cue.gripW
        height: parent.height
        color: grip.cue.isSelected ? Theme.c.subtitleCueEdgeGrip.background : "transparent"
        opacity: Theme.c.subtitleCueEdgeGrip.backgroundOpacity
    }

    HandleKnob {
        id: edgeKnob
        releaseOnCancel: false
        onCanceled: EditorState.drag.cancel()
        readonly property real zoneX: grip.edge < 0
           ? ((Theme.touchInput && !grip.cue.narrowTouch) ? -grip.cue.edgeHitW / 2 : 0)
           : grip.width - ((Theme.touchInput && !grip.cue.narrowTouch) ? grip.cue.edgeHitW / 2 : grip.cue.edgeHitW)
        x: zoneX + grip.cue.edgeHitW / 2 - width / 2
        y: 0
        width: Math.max(mouseHitSize, grip.cue.edgeHitW)
        height: grip.height
        shape: "none"
        cursor: Qt.SizeHorCursor
        space: null
        mouseHitSize: 14
        mouseHitHeight: height
        // Only a selected cue takes a finger; on an unselected one the press goes to the lane's hold-to-move.
        touchHitSize: grip.cue.isSelected ? grip.cue.edgeHitW : 0
        touchHitHeight: grip.cue.isSelected ? height : 0

        onPressed: (point, modifiers, device) => {
            grip.pressSceneX = mapToItem(null, point.x, 0).x
            EditorState.drag.begin("subtitleEdge", device, qsTr("Adjust subtitle timing"))
            grip.cue.startEdit()
        }
        onMoved: (point, modifiers, device) => {
            if (!grip.cue.dragging)
                return
            const translation = mapToItem(null, point.x, 0).x - grip.pressSceneX
            const snapping = !(modifiers & Qt.ControlModifier)
            let value
            if (grip.edge < 0) {
                let ns = grip.cue.editStart + translation / grip.lane.pxPerSecond
                if (snapping)
                    ns = grip.lane.snap(ns, grip.cue.index)
                ns = grip.lane.clampVal(ns, grip.lane.loBound(grip.cue.index),
                                        grip.cue.editEnd - grip.lane.minCueDur)
                grip.cue.dragX = grip.lane.xForSeconds(ns)
                grip.cue.dragW = (grip.cue.editEnd - ns) * grip.lane.pxPerSecond
                grip.lane.commitCue(grip.cue.index, ns, grip.cue.editEnd)
                value = ns
            } else {
                let ne = grip.cue.editEnd + translation / grip.lane.pxPerSecond
                if (snapping)
                    ne = grip.lane.snap(ne, grip.cue.index)
                ne = grip.lane.clampVal(ne, grip.cue.editStart + grip.lane.minCueDur,
                                        grip.lane.hiBound(grip.cue.index))
                grip.cue.dragW = (ne - grip.cue.editStart) * grip.lane.pxPerSecond
                grip.lane.commitCue(grip.cue.index, grip.cue.editStart, ne)
                value = ne
            }
            const from = grip.edge < 0 ? grip.cue.editStart : grip.cue.editEnd
            EditorState.drag.update(grip.lane.formatDelta(value - from) + qsTr("  →  ")
                                    + grip.lane.formatTimecode(grip.lane.clipStart + value, true))
            grip.lane.setDragPointer(mapToItem(null, point.x, point.y).x, mapToItem(null, point.x, point.y).y)
        }
        onReleased: {
            grip.cue.finishEdit()
            EditorState.drag.commit()
        }


        Connections {
            target: EditorState.drag
            function onCancelled() {
                if (edgeKnob.active && grip.cue.dragging)
                    grip.cue.finishEdit()
            }
        }
    }
}
