import QtQuick
import QtQuick.Controls.Basic
import QtMultimedia
import Drift
// One stock audio result: preview, title, creator and download progress.
Rectangle {
    id: root

    property var previewPlayer: null
    signal openRequested(var item)

    required property var modelData
    readonly property bool isPlaying: previewPlayer.itemId === modelData.id
                                      && previewPlayer.playbackState === MediaPlayer.PlayingState
    readonly property var job: {
        void Downloads.revision
        return StockInstaller.downloadInfo(modelData.source + "/" + modelData.id)
    }
    width: ListView.view ? ListView.view.width : 0
    height: Theme.touchUi ? Theme.androidMinTouchTarget + Theme.spacingLg : Theme.controlHeight + Theme.spacingLg
    radius: Theme.c.stockAudioRow.radius
    color: rowHover.hovered ? Theme.c.stockAudioRow.backgroundHover : "transparent"

    HoverHandler { id: rowHover; cursorShape: Qt.PointingHandCursor }
    TapHandler { onTapped: openRequested(root.modelData) }

    IconButton {
        id: playButton
        anchors.left: parent.left
        anchors.verticalCenter: parent.verticalCenter
        glyph: root.isPlaying ? Theme.icons.pause : Theme.icons.play
        enabled: !!root.modelData.previewUrl
        tooltip: root.isPlaying ? qsTr("Pause") : qsTr("Play preview")
        onClicked: {
            if (root.isPlaying) {
                previewPlayer.pause()
                return
            }
            if (previewPlayer.itemId !== root.modelData.id) {
                previewPlayer.itemId = root.modelData.id
                previewPlayer.source = root.modelData.previewUrl
            }
            previewPlayer.play()
        }
    }

    Column {
        anchors.left: playButton.right
        anchors.right: trailing.left
        anchors.leftMargin: Theme.spacingMd
        anchors.rightMargin: Theme.spacingMd
        anchors.verticalCenter: parent.verticalCenter
        spacing: 2
        Text {
            width: parent.width
            text: root.modelData.title || ""
            color: Theme.c.stockAudioRow.foreground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeSm
            elide: Text.ElideRight
        }
        Text {
            width: parent.width
            visible: text.length > 0
            text: root.modelData.subtitle || ""
            color: Theme.c.stockAudioRow.foregroundMuted
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeXs
            elide: Text.ElideRight
        }
        // Playback position of the row that is playing.
        Rectangle {
            width: parent.width
            height: 2
            radius: Theme.c.stockAudioRow.trackRadius
            color: Theme.c.stockAudioRow.trackBackground
            visible: previewPlayer.itemId === root.modelData.id && previewPlayer.duration > 0
            Rectangle {
                width: parent.width * Math.min(1, previewPlayer.position / Math.max(1, previewPlayer.duration))
                height: parent.height
                radius: Theme.c.stockAudioRow.trackRadius
                color: Theme.c.stockAudioRow.accent
            }
        }
    }

    Item {
        id: trailing
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        width: Math.max(durationText.implicitWidth, ring.width)
        height: parent.height

        Text {
            id: durationText
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            visible: !ring.visible
            text: {
                const n = Math.round(Number(root.modelData.durationMs || 0) / 1000)
                return n > 0 ? Math.floor(n / 60) + ":" + (n % 60 < 10 ? "0" : "") + (n % 60) : ""
            }
            color: Theme.c.stockAudioRow.foregroundMuted
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeXs
        }
        CircularProgress {
            id: ring
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            visible: ["waiting", "queued", "processing", "downloading", "importing"].indexOf(root.job.status) >= 0
            value: Number(root.job.progress || 0)
            indeterminate: Number(root.job.progress || 0) <= 0
            size: Theme.spacing2xl + Theme.spacingSm
        }
    }
}
