import QtQuick
import Drift

// While a slip drag is under way, the program monitor shows the two frames that matter: the new
// first and last frame of the clip. They are decoded by image://slipframe on request.
Rectangle {
    id: overlay

    readonly property var frames: ShellBridge.slipFrames
    readonly property real fps: {
        void EditorState.tracksRevision
        const f = EditorState.projectFile.projectFps()
        return f > 0 ? f : 30
    }
    // Copied from ShellBridge by a short timer, so a fast drag decodes a few dozen frames a second
    // and not one per pixel.
    property string firstSource: ""
    property string lastSource: ""
    // The source time of the frame each picture shows (reverse, loops and speed accounted for).
    property real firstAt: 0
    property real lastAt: 0

    visible: !!frames && frames.first !== ""
    color: Theme.c.previewPanel.background

    function tc(seconds) {
        const total = Math.round(Math.max(0, seconds) * overlay.fps)
        const f = total % Math.round(overlay.fps)
        const s = Math.floor(total / overlay.fps) % 60
        const m = Math.floor(total / (overlay.fps * 60)) % 60
        const h = Math.floor(total / (overlay.fps * 3600))
        function pad(n) { return n.toString().padStart(2, "0") }
        return pad(h) + ":" + pad(m) + ":" + pad(s) + ":" + pad(f)
    }

    onFramesChanged: {
        if (!frames) {
            firstSource = ""
            lastSource = ""
            syncTimer.stop()
        } else if (!syncTimer.running) {
            syncTimer.start()
        }
    }

    Timer {
        id: syncTimer
        interval: 40
        repeat: false
        onTriggered: {
            const f = overlay.frames
            if (!f)
                return
            overlay.firstSource = f.first
            overlay.lastSource = f.last
            overlay.firstAt = f.firstAt
            overlay.lastAt = f.lastAt
        }
    }

    Row {
        anchors.fill: parent
        anchors.margins: Theme.spacingLg
        spacing: Theme.spacingLg

        Repeater {
            model: [
                { kind: "first" },
                { kind: "last" }
            ]

            delegate: Item {
                id: cell
                required property var modelData
                width: (parent.width - Theme.spacingLg) / 2
                height: parent.height

                Image {
                    anchors.top: parent.top
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: caption.top
                    anchors.bottomMargin: Theme.spacingSm
                    asynchronous: true
                    // The previous frame stays until the next one is decoded, so a drag doesn't flicker.
                    retainWhileLoading: true
                    cache: false
                    fillMode: Image.PreserveAspectFit
                    sourceSize.width: Math.max(1, Math.round(cell.width))
                    sourceSize.height: Math.max(1, Math.round(cell.height - caption.height - Theme.spacingSm))
                    source: cell.modelData.kind === "first" ? overlay.firstSource : overlay.lastSource
                }

                Rectangle {
                    id: caption
                    anchors.bottom: parent.bottom
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: captionText.implicitWidth + Theme.spacingLg * 2
                    height: captionText.implicitHeight + Theme.spacingSm * 2
                    color: Theme.c.themedToolTip.background
                    border.width: Theme.c.themedToolTip.borderWidth
                    border.color: Theme.c.themedToolTip.border
                    radius: Theme.c.themedToolTip.radius

                    Text {
                        id: captionText
                        anchors.centerIn: parent
                        text: cell.modelData.kind === "first"
                              ? qsTr("First frame %1").arg(overlay.tc(overlay.firstAt))
                              : qsTr("Last frame %1").arg(overlay.tc(overlay.lastAt))
                        color: Theme.c.themedToolTip.foreground
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSizeXs
                    }
                }
            }
        }
    }
}
