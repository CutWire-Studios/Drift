import QtQuick
import Drift

// Grabbable end of the trim range. The desktop hairline gave no hint that the
// range could be dragged; these are thumb-width and carry a grip.
Rectangle {
    id: handle

    required property var modelData
    required property var preview
    property real stripWidth: 1
    property real inX: 0
    property real outX: 0

    readonly property bool isIn: modelData.edge === "in"

    width: 22
    height: parent ? parent.height : 0
    x: handle.isIn ? handle.inX : handle.outX - width
    color: Theme.c.androidMediaTrimHandle.background
    radius: Theme.c.androidMediaTrimHandle.radius
    z: 3

    Column {
        anchors.centerIn: parent
        spacing: 3
        Repeater {
            model: 3
            Rectangle {
                width: 10
                height: 2
                radius: Theme.c.androidMediaTrimHandle.gripRadius
                color: Theme.c.androidMediaTrimHandle.grip
                opacity: Theme.c.androidMediaTrimHandle.gripOpacity
            }
        }
    }

    // The knob only supplies the hit area and the pointer events; the bar above is its look.
    HandleKnob {
        id: knob
        anchors.centerIn: parent
        shape: "none"
        enabled: !handle.preview.saving
        space: handle.parent
        mouseHitSize: handle.width + 24
        touchHitSize: handle.width + 24
        mouseHitHeight: handle.height + 24
        touchHitHeight: handle.height + 24

        property real grab: 0

        onPressed: (point) => {
            grab = point.x - (handle.isIn ? handle.inX : handle.outX)
            EditorState.assetPreview.pause()
            Haptics.pickUp()
        }
        onReleased: Haptics.drop()
        onMoved: (point) => {
            const t = ((point.x - grab) / Math.max(1, handle.stripWidth)) * handle.preview.durationSeconds
            if (handle.isIn)
                handle.preview.inSeconds = t
            else
                handle.preview.outSeconds = t
            handle.preview.clampRange()
            handle.preview.seekTo(handle.isIn ? handle.preview.inSeconds : handle.preview.outSeconds)
        }
    }
}
