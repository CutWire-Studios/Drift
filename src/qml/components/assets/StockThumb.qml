import QtQuick
import QtMultimedia
import Drift
// One stock result as a tile: thumbnail, price and duration badges, and the download state
// drawn over it (progress with cancel, or failure with retry). A video tile plays its preview,
// muted, while hovered.
Rectangle {
    id: root

    required property var item
    property bool compact: Theme.compact
    property bool current: false

    signal openRequested()

    readonly property var job: {
        void Downloads.revision
        return StockInstaller.downloadInfo(item.id)
    }
    readonly property bool busy: job.status === "waiting" || job.status === "queued"
                                 || job.status === "processing" || job.status === "downloading"
                                 || job.status === "importing"
    readonly property string kind: item.media_kind || item.type || ""
    readonly property bool isVideo: kind === "video" || kind === "greenscreen"

    radius: Theme.c.stockThumb.radius
    color: Theme.c.stockThumb.background
    clip: true
    border.width: current ? Theme.c.stockThumb.borderWidth : 0
    border.color: Theme.c.stockThumb.borderFocus

    function formatDuration(ms) {
        const n = Number(ms)
        if (!n || n <= 0)
            return ""
        const s = Math.round(n / 1000)
        return Math.floor(s / 60) + ":" + (s % 60 < 10 ? "0" : "") + (s % 60)
    }

    HoverHandler {
        id: hover
        cursorShape: Qt.PointingHandCursor
    }

    SkeletonBox {
        anchors.fill: parent
        visible: thumb.status === Image.Loading
    }

    Image {
        id: thumb
        anchors.fill: parent
        source: root.item.thumb_url || ""
        fillMode: Image.PreserveAspectCrop
        asynchronous: true
        cache: true
        // Decoded at the drawn size; the served thumbnail is several times larger.
        sourceSize.width: Math.max(1, Math.round(parent.width))
        opacity: status === Image.Ready ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: Theme.durationBase; easing.type: Theme.easing } }
    }

    // One player at a time: only the hovered tile creates one.
    Loader {
        anchors.fill: parent
        active: root.isVideo && hover.hovered && !root.busy && !!root.item.preview_url && !root.compact
        sourceComponent: Item {
            VideoOutput {
                id: out
                anchors.fill: parent
                fillMode: VideoOutput.PreserveAspectCrop
            }
            MediaPlayer {
                source: root.item.preview_url
                videoOutput: out
                loops: MediaPlayer.Infinite
                Component.onCompleted: play()
            }
        }
    }

    Rectangle {
        visible: durLabel.text.length > 0
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: Theme.spacingSm
        color: Theme.c.stockThumb.badgeBackground
        radius: Theme.c.stockThumb.badgeRadius
        width: durLabel.implicitWidth + Theme.spacingLg
        height: durLabel.implicitHeight + Theme.spacingSm
        Text {
            id: durLabel
            anchors.centerIn: parent
            text: root.formatDuration(root.item.duration_ms)
            color: Theme.c.stockThumb.foregroundOnMedia
            font.pixelSize: Theme.fontSizeXs
            font.family: Theme.fontFamily
        }
    }

    TapHandler {
        onTapped: root.openRequested()
    }

    // Progress with a way out: a job wedged on a slow source otherwise held its tile until the
    // file timeout. On touch the ring and a real cancel button are both shown.
    Rectangle {
        visible: root.busy
        anchors.fill: parent
        color: Theme.c.stockThumb.scrim

        CircularProgress {
            anchors.centerIn: parent
            visible: root.compact || !busyHover.hovered
            value: Number(root.job.progress || 0)
            indeterminate: Number(root.job.progress || 0) <= 0
            size: Theme.spacing3xl
            progressColor: Theme.c.stockThumb.foregroundOnMedia
        }
        IconGlyph {
            anchors.centerIn: parent
            visible: !root.compact && busyHover.hovered
            glyph: Theme.icons.x
            iconSize: Theme.iconSizeBase
            iconColor: Theme.c.stockThumb.foregroundOnMedia
        }
        IconButton {
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: Theme.spacingXs
            visible: root.compact
            buttonSize: Theme.androidMinTouchTarget
            iconSize: Theme.iconSizeMd
            glyph: Theme.icons.x
            tooltip: qsTr("Cancel download")
            onClicked: StockInstaller.cancelDownload(root.item.id)
        }
        HoverHandler {
            id: busyHover
            enabled: !root.compact
            cursorShape: Qt.PointingHandCursor
        }
        ThemedToolTip {
            text: qsTr("Cancel download")
            visible: busyHover.hovered
            y: parent.height + 4
        }
        TapHandler {
            enabled: !root.compact
            onTapped: StockInstaller.cancelDownload(root.item.id)
        }
    }

    Rectangle {
        visible: root.job.status === "failed"
        anchors.fill: parent
        color: Theme.c.stockThumb.scrim

        Column {
            anchors.centerIn: parent
            spacing: Theme.spacingSm
            IconGlyph {
                anchors.horizontalCenter: parent.horizontalCenter
                glyph: Theme.icons.error
                iconSize: Theme.iconSizeBase
                iconColor: Theme.c.stockThumb.foregroundOnMedia
            }
            ThemedButton {
                anchors.horizontalCenter: parent.horizontalCenter
                visible: root.job.retryable === true
                text: qsTr("Retry")
                onClicked: StockInstaller.retryDownload(root.item.id)
            }
        }
        // Retry is the one thing to press on a failed tile.
        TapHandler { }
    }
}
