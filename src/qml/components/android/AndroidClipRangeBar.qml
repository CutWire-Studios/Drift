import QtQuick
import Drift
// Play/pause plus a scrubber confined to one clip's [start, end] on the timeline, so a tool
// page can audition just the clip it is editing. The time readout is relative to the clip start.
Item {
    id: root

    property int trackIndex: -1
    property int clipIndex: -1

    property real startSeconds: 0
    property real endSeconds: 0
    readonly property real clipDuration: Math.max(0, endSeconds - startSeconds)

    function refresh() {
        if (trackIndex < 0 || clipIndex < 0) {
            startSeconds = 0
            endSeconds = 0
            return
        }
        const r = EditorState.clipTimelineRange(trackIndex, clipIndex)
        startSeconds = r.startSeconds ?? 0
        endSeconds = r.endSeconds ?? 0
    }

    onTrackIndexChanged: refresh()
    onClipIndexChanged: refresh()
    Component.onCompleted: refresh()

    Connections {
        target: EditorState
        function onTracksChanged() { root.refresh() }
    }

    function formatRelative(seconds) {
        const t = Math.max(0, seconds)
        const m = Math.floor(t / 60)
        const s = t - m * 60
        return m + ":" + (s < 10 ? "0" : "") + s.toFixed(1)
    }

    implicitHeight: Theme.androidIconButtonSize

    IconButton {
        id: playButton
        anchors.left: parent.left
        anchors.leftMargin: Theme.spacingSm
        anchors.verticalCenter: parent.verticalCenter
        buttonSize: Theme.androidIconButtonSize
        iconSize: Theme.iconSizeLg
        glyph: EditorState.playing ? Theme.icons.pause : Theme.icons.play
        variant: "text"
        tooltip: EditorState.playing ? qsTr("Pause") : qsTr("Play")
        onClicked: EditorState.togglePlayback()
    }

    Text {
        id: readout
        anchors.right: parent.right
        anchors.rightMargin: Theme.spacingMd
        anchors.verticalCenter: parent.verticalCenter
        text: root.formatRelative(slider.value - root.startSeconds) + " / "
              + root.formatRelative(root.clipDuration)
        color: Theme.c.androidToolPage.foregroundMuted
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSizeSm
    }

    ThemedSlider {
        id: slider
        label: qsTr("Seek")
        anchors.left: playButton.right
        anchors.right: readout.left
        anchors.rightMargin: Theme.spacingMd
        anchors.verticalCenter: parent.verticalCenter
        showValueTooltip: false

        from: root.startSeconds
        // Never a zero-width range: an unresolved clip would make the handle jump.
        to: Math.max(root.startSeconds + 0.001, root.endSeconds)

        onMoved: EditorState.playheadSeconds = value

        Binding on value {
            when: !slider.pressed
            value: Math.min(slider.to, Math.max(slider.from, EditorState.playheadSeconds))
            restoreMode: Binding.RestoreNone
        }
    }
}
