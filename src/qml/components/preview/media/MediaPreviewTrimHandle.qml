import QtQuick
import Drift

// In or out handle on the filmstrip.
Rectangle {
    id: handle

    required property var modelData
    required property var editor
    required property var track
    required property real inX
    required property real outX
    required property bool editing

    visible: handle.editor.canTrim
    width: Theme.touchUi ? 14 : 8
    height: parent.height
    x: (handle.modelData.edge === "in" ? handle.inX : handle.outX) - width / 2
    color: Theme.c.mediaPreviewTrimHandle.background
    z: 3

    // The knob only supplies the hit area and the pointer events; the bar above is its look.
    HandleKnob {
        id: knob
        anchors.centerIn: parent
        shape: "none"
        enabled: handle.editing
        cursor: Qt.SizeHorCursor
        space: handle.parent
        mouseHitSize: handle.width + 8
        touchHitSize: handle.width + 16
        mouseHitHeight: handle.height
        touchHitHeight: handle.height

        property real grab: 0

        function edgeX() {
            return handle.modelData.edge === "in" ? handle.inX : handle.outX
        }

        onPressed: (point) => { grab = point.x - edgeX() }
        onMoved: (point) => {
            const t = ((point.x - grab) / Math.max(1, handle.track.width)) * handle.editor.durationSeconds
            if (handle.modelData.edge === "in")
                handle.editor.inSeconds = t
            else
                handle.editor.outSeconds = t
            handle.editor.clampRange()
            handle.editor.seekTo(handle.modelData.edge === "in" ? handle.editor.inSeconds
                                                                : handle.editor.outSeconds)
        }
    }
}
