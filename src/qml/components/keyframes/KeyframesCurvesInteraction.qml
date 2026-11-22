pragma ComponentBehavior: Bound
import QtQuick
import Drift
import "../timeline/TimelineWheel.js" as TimelineWheel

Item {
    id: root

    required property var panel
    required property var view

    readonly property var timeView: panel.timeView
    readonly property alias interaction: interaction
    readonly property bool busy: interaction.busy

    signal contextRequested(real x, real y, var hit)

    function cancelDrag() {
        interaction.cancel()
    }

    function handleKey(event) {
        return input.handleKey(event)
    }

    KeyframeInteraction {
        id: interaction

        mode: "curves"
        selection: root.panel.selection
        track: root.panel.track
        clip: root.panel.clipIndex
        viewWidth: root.width
        viewHeight: root.height
        playhead: root.panel.playhead
        fps: Math.max(1, EditorState.projectFile.projectFps())
        entryTarget: "value"

        Component.onCompleted: attach(root.panel.rowsModel, root.view.curves, root.timeView, EditorState.keyframes)
        onFocusRequested: (track, rowProp) => {
            root.view.focusedTrack = track
            root.panel.focusedProp = rowProp
        }
        onContextRequested: (x, y, hit) => root.contextRequested(x, y, hit)
        onPlayheadRequested: (seconds) => EditorState.playheadSeconds = seconds
        onPanRequested: (dx, dy) => {
            root.timeView.contentX -= dx
            if (!root.panel.normalize)
                root.view.curves.panValuesBy(dy)
        }
        onScrollRequested: (dx, dy) => {
            const lo = root.timeView.clipStart * root.timeView.pxPerSecond - root.width * 0.25
            const hi = Math.max(lo, (root.timeView.clipStart + root.timeView.clipDuration) * root.timeView.pxPerSecond
                                    + root.width * 0.25 - root.width)
            root.timeView.contentX = Math.max(lo, Math.min(hi, root.timeView.contentX + dx))
        }
    }

    Binding {
        target: root.view
        property: "hoverId"
        value: interaction.hoverId
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
            const d = TimelineWheel.delta(TimelineWheel.input(wheel, PointerDevice.TouchPad), 0.5)
            const dx = d.x
            const dy = d.y
            const sharedRange = !root.panel.normalize
            if (mods & Qt.ControlModifier) {
                if (mods & Qt.ShiftModifier) {
                    if (sharedRange)
                        root.view.curves.zoomValuesAt(wheel.y, Math.exp(-(dy !== 0 ? dy : dx) / 300))
                } else {
                    root.timeView.zoomAt(wheel.x, Math.exp(wheel.angleDelta.y / 600))
                }
                return
            }
            if (mods & Qt.ShiftModifier) {
                root.timeView.contentX -= (dy !== 0 ? dy : dx)
                return
            }
            if (dx !== 0)
                root.timeView.contentX -= dx
            if (dy !== 0 && sharedRange)
                root.view.curves.panValuesBy(dy)
        }
    }
}
