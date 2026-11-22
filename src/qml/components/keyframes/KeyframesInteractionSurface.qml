pragma ComponentBehavior: Bound
import QtQuick
import Drift
import "../timeline/TimelineWheel.js" as TimelineWheel

Item {
    id: root

    required property var panel
    required property var lanes

    readonly property var timeView: panel.timeView
    readonly property alias anchorProxy: anchorProxy
    readonly property alias interaction: interaction
    readonly property bool busy: interaction.busy

    signal contextRequested(real x, real y, var hit)

    function cancelDrag() {
        interaction.cancel()
    }

    function handleKey(event) {
        return input.handleKey(event)
    }

    Item {
        id: anchorProxy
        width: 1
        height: 1
    }

    KeyframeInteraction {
        id: interaction

        mode: "lanes"
        selection: root.panel.selection
        track: root.panel.track
        clip: root.panel.clipIndex
        rowHeight: root.lanes.rowHeight
        contentY: root.lanes.contentY
        viewWidth: root.width
        viewHeight: root.height
        playhead: root.panel.playhead
        fps: Math.max(1, EditorState.projectFile.projectFps())

        Component.onCompleted: attach(root.panel.rowsModel, null, root.timeView, EditorState.keyframes)
        onContextRequested: (x, y, hit) => {
            if (hit.row < 0)
                return
            hit.rowData = root.panel.rows[hit.row]
            root.contextRequested(x, y, hit)
        }
        onPlayheadRequested: (seconds) => EditorState.playheadSeconds = seconds
        onPanRequested: (dx, dy) => {
            root.timeView.contentX -= dx
            root.lanes.scrollBy(-dy)
        }
        onScrollRequested: (dx, dy) => {
            if (dx !== 0) {
                const lo = root.lanes.scrollMin
                const hi = Math.max(lo, root.lanes.scrollMin + root.lanes.scrollTotal - root.lanes.width)
                root.timeView.contentX = Math.max(lo, Math.min(hi, root.timeView.contentX + dx))
            }
            if (dy !== 0)
                root.lanes.scrollBy(dy)
        }
    }

    Binding {
        target: root.lanes
        property: "hoverRow"
        value: interaction.hoverRow
    }

    Binding {
        target: root.lanes
        property: "hoverSeconds"
        value: interaction.hoverSeconds
    }

    Connections {
        target: root.panel

        function onTrackChanged() { interaction.cancel() }
        function onClipIndexChanged() { interaction.cancel() }
    }

    KeyframeInputArea {
        id: input
        anchors.fill: parent
        interaction: interaction
        focusTarget: root.panel
        pinchTimeView: root.timeView
        onWheelMoved: (wheel) => {
            const mods = wheel.modifiers
            if (mods & Qt.ControlModifier) {
                root.timeView.zoomAt(wheel.x, Math.exp(wheel.angleDelta.y / 600))
                return
            }
            const d = TimelineWheel.delta(TimelineWheel.input(wheel, PointerDevice.TouchPad), 0.5)
            const dx = d.x
            const dy = d.y
            if (mods & Qt.ShiftModifier) {
                root.lanes.panBy(-(dy !== 0 ? dy : dx))
                return
            }
            if (dx !== 0)
                root.lanes.panBy(-dx)
            if (dy !== 0)
                root.lanes.scrollBy(-dy)
        }
    }
}
