import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Window
import QtMultimedia
import Drift
import ".."

// One stock result: its preview, who made it and under what licence, and Download. Replaces
// the modal dialog; it slides over the results so Back returns to the same scroll position.
Rectangle {
    id: root

    property var item: ({})
    property bool compact: Theme.compact
    property bool shown: false

    signal backRequested()
    signal downloadRequested()
    signal changeFolderRequested()

    readonly property string kind: item.media_kind || item.type || ""
    readonly property bool isImage: kind === "photo" || kind === "image"
    readonly property var job: {
        void Market.downloadsRevision
        return item.id ? Market.downloadInfo(item.id) : ({})
    }
    readonly property bool busy: job.status === "waiting" || job.status === "queued"
                                 || job.status === "processing" || job.status === "downloading"
                                 || job.status === "importing"
    readonly property int price: Number(item.price_coins || 0)

    color: Theme.panelBackground

    // A Rectangle takes no input, and TapHandlers only grab passively, so without this a tap
    // on the panel (a colour swatch, a button) also reached the card lying underneath it.
    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.AllButtons
        onWheel: (wheel) => { wheel.accepted = true }
    }

    function folderName(url) {
        const s = decodeURIComponent(String(url).replace(/^file:\/\//, ""))
        const parts = s.split(/[\/\\]/).filter(p => p.length > 0)
        return parts.length > 0 ? parts[parts.length - 1] : s
    }

    onShownChanged: {
        if (!shown)
            player.stop()
    }

    // Fixed above the scrolling body so Back is always in reach. The phone sheet has none.
    IconButton {
        id: backButton
        x: Theme.pagePadding
        y: Theme.spacingSm
        visible: !root.compact
        glyph: Theme.icons.chevronLeft
        tooltip: qsTr("Back to results")
        onClicked: root.backRequested()
    }

    Flickable {
        id: flick
        anchors.top: backButton.visible ? backButton.bottom : parent.top
        anchors.topMargin: backButton.visible ? Theme.spacingSm : 0
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        contentHeight: body.implicitHeight + Theme.spacing3xl
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: AppScrollBar { }

        Column {
            id: body
            x: root.compact ? Theme.androidPagePadding : Theme.pagePadding
            width: flick.width - x * 2
            spacing: Theme.spacingXl

            Rectangle {
                width: parent.width
                height: {
                    const w = Number(root.item.width || 16), h = Number(root.item.height || 9)
                    return Math.min(260, width * h / Math.max(1, w))
                }
                radius: Theme.radiusMd
                color: Theme.panelAccent
                clip: true

                Image {
                    anchors.fill: parent
                    visible: root.isImage || player.playbackState !== MediaPlayer.PlayingState
                    source: root.isImage ? (root.item.preview_url || root.item.thumb_url || "") : (root.item.thumb_url || "")
                    fillMode: Image.PreserveAspectFit
                    asynchronous: true
                    sourceSize.height: Math.ceil(260 * Screen.devicePixelRatio)
                }
                VideoOutput {
                    id: out
                    anchors.fill: parent
                    visible: !root.isImage
                    fillMode: VideoOutput.PreserveAspectFit
                }
                MediaPlayer {
                    id: player
                    audioOutput: AudioOutput {}
                    videoOutput: out
                    loops: MediaPlayer.Infinite
                    source: root.shown && !root.isImage ? (root.item.preview_url || "") : ""
                    onSourceChanged: if (source.toString().length > 0) play()
                }
            }

            Text {
                width: parent.width
                text: root.item.title || ""
                color: Theme.panelForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeBase * 1.2
                font.weight: Font.DemiBold
                wrapMode: Text.WordWrap
            }

            Column {
                width: parent.width
                spacing: Theme.spacingXs

                Text {
                    width: parent.width
                    visible: !!(root.item.creator && root.item.creator.name)
                    text: qsTr("By %1").arg(root.item.creator ? root.item.creator.name : "")
                    color: Theme.panelForeground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeSm
                    wrapMode: Text.WordWrap
                }
                Text {
                    width: parent.width
                    visible: text.length > 0
                    text: {
                        const l = root.item.license
                        if (!l)
                            return ""
                        return l.attribution || l.name || ""
                    }
                    color: Theme.mutedForeground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeXs
                    wrapMode: Text.WordWrap
                }
                Text {
                    width: parent.width
                    visible: root.price > 0
                    text: qsTr("%n coin(s)", "", root.price)
                    color: Theme.panelForeground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeXs
                }
            }

            ThemedButton {
                variant: "primary"
                enabled: !root.busy && root.item.downloadable !== false
                text: root.busy ? qsTr("Downloading…")
                                : (root.job.status === "done" ? qsTr("Download again") : qsTr("Download"))
                tooltip: root.item.downloadable === false ? qsTr("You have used today’s downloads from this source") : ""
                onClicked: root.downloadRequested()
            }

            // Where downloads go, so it is never a surprise; asked for only the first time.
            Row {
                width: parent.width
                spacing: Theme.spacingLg
                visible: FileDialogs.supportsDirectoryPicker()

                Text {
                    width: Math.min(implicitWidth, parent.width - changeFolder.width - parent.spacing)
                    text: Market.lastDownloadDir.toString().length > 0
                          ? qsTr("Saves to %1").arg(root.folderName(Market.lastDownloadDir))
                          : qsTr("You’ll choose a folder the first time")
                    color: Theme.mutedForeground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeXs
                    elide: Text.ElideMiddle
                }
                Text {
                    id: changeFolder
                    visible: Market.lastDownloadDir.toString().length > 0
                    text: qsTr("Change")
                    color: changeHover.hovered ? Theme.panelForeground : Theme.accentOnPanel
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeXs
                    HoverHandler { id: changeHover; cursorShape: Qt.PointingHandCursor }
                    TapHandler { onTapped: root.changeFolderRequested() }
                }
            }
        }
    }
}
