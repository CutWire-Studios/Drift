import QtQuick
import QtQuick.Controls.Basic
import QtMultimedia
import Drift
// One sound effect: preview, name, waveform and add actions.
Rectangle {
    id: root

    property bool compact: false
    property string justAddedId: ""
    property var previewPlayer: null
    signal openRequested(var sound)
    signal keepRequested(var sound)
    signal useRequested(var sound)

    function formatDuration(seconds) {
        const s = Number(seconds || 0)
        if (s < 10)
            return s.toFixed(1) + "s"
        const n = Math.round(s)
        return Math.floor(n / 60) + ":" + (n % 60 < 10 ? "0" : "") + (n % 60)
    }

    required property var modelData
    readonly property bool isCurrent: previewPlayer.soundId === modelData.id
    readonly property bool isPlaying: isCurrent && previewPlayer.playbackState === MediaPlayer.PlayingState
    readonly property string installState: {
        void MarketInstall.revision
        return MarketInstall.installState(modelData)
    }
    readonly property bool busy: installState === "installing"
    readonly property var peaks: Sfx.peaksOf(modelData)
    readonly property real progress: isCurrent && previewPlayer.duration > 0
                                     ? Math.min(1, previewPlayer.position / previewPlayer.duration) : 0

    width: ListView.view ? ListView.view.width : 0
    height: Theme.touchUi ? Theme.androidMinTouchTarget + Theme.spacingLg : Theme.controlHeight + Theme.spacingLg
    radius: Theme.c.sfxSoundRow.radius
    color: rowHover.hovered ? Theme.c.sfxSoundRow.backgroundHover : "transparent"

    HoverHandler { id: rowHover }
    TapHandler { onTapped: root.openRequested(root.modelData) }

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
            if (!root.isCurrent) {
                previewPlayer.soundId = root.modelData.id
                previewPlayer.source = root.modelData.previewUrl
            }
            previewPlayer.play()
        }
    }

    Column {
        id: label
        anchors.left: playButton.right
        anchors.leftMargin: Theme.spacingMd
        anchors.verticalCenter: parent.verticalCenter
        width: compact ? parent.width - playButton.width - trailing.width - Theme.spacingMd * 2
                            : Math.min(200, (parent.width - playButton.width - trailing.width) * 0.4)
        spacing: 2

        Text {
            width: parent.width
            text: root.modelData.title || ""
            color: Theme.c.sfxSoundRow.foreground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeSm
            elide: Text.ElideRight
        }
        Text {
            width: parent.width
            text: formatDuration(Number(root.modelData.durationMs || 0) / 1000)
            color: Theme.c.sfxSoundRow.foregroundMuted
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeXs
            elide: Text.ElideRight
        }
    }

    // The sound's shape, filled up to the playback position while it plays.
    Canvas {
        id: wave
        anchors.left: label.right
        anchors.right: trailing.left
        anchors.leftMargin: Theme.spacingLg
        anchors.rightMargin: Theme.spacingLg
        anchors.verticalCenter: parent.verticalCenter
        height: parent.height * 0.5
        visible: !compact && root.peaks.length > 0
        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()
            const n = root.peaks.length
            if (n === 0)
                return
            const step = width / n
            const bar = Math.max(1, step * 0.6)
            for (let i = 0; i < n; ++i) {
                const h = Math.max(2, root.peaks[i] * height)
                ctx.fillStyle = (i + 0.5) / n <= root.progress ? Theme.c.sfxSoundRow.waveAccent : Theme.c.sfxSoundRow.waveMuted
                ctx.fillRect(i * step, (height - h) / 2, bar, h)
            }
        }
        onWidthChanged: requestPaint()
        Connections {
            target: root
            function onProgressChanged() { wave.requestPaint() }
            function onPeaksChanged() { wave.requestPaint() }
        }
    }

    Row {
        id: trailing
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        spacing: Theme.spacingXs

        CircularProgress {
            anchors.verticalCenter: parent.verticalCenter
            visible: root.busy
            indeterminate: true
            size: Theme.spacing2xl + Theme.spacingSm
        }
        IconButton {
            anchors.verticalCenter: parent.verticalCenter
            visible: !root.busy
            glyph: Theme.icons.folderInput
            tooltip: qsTr("Add to the media bin")
            onClicked: keepRequested(root.modelData)
        }
        AssetQuickAddButton {
            anchors.verticalCenter: parent.verticalCenter
            visible: !root.busy
            revealed: rowHover.hovered
            done: justAddedId === root.modelData.id
            tooltip: qsTr("Add to the timeline")
            onClicked: useRequested(root.modelData)
        }
    }
}
