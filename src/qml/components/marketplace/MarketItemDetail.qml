import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Window
import QtMultimedia
import Drift
// One listing in full: preview, title, who made it and under what licence, the variant to
// install, and the install action for its source. Stock goes to a folder the user picks,
// Drift Assets and sounds go into the media bin or onto the timeline, community add-ons install
// into Drift. It slides over the pages so Back returns to the same scroll position.
Rectangle {
    id: root

    property var item: ({})
    property bool compact: Theme.compact
    property bool shown: false

    signal backRequested()
    signal linkRequested(string url)
    signal addToTimelineRequested(var item)

    readonly property string kind: item.mediaKind || ""
    readonly property string family: {
        const source = item.source || ""
        if (source.indexOf("stock.") === 0) return "stock"
        if (source.indexOf("community.") === 0) return "community"
        return source
    }
    readonly property bool isVideo: kind === "video"
    readonly property bool isAudio: kind === "audio" || kind === "music"
    readonly property bool isAsset: kind === "lottie" || kind === "model3d" || kind === "face-prop"
    readonly property real previewHeight: {
        if (isAudio)
            return Theme.controlHeight * 2
        const w = Number(item.width || 16), h = Number(item.height || 9)
        return Math.min(kind === "image" ? 360 : 260, Math.max(120, body.width * h / Math.max(1, w)))
    }
    readonly property var variants: item.variants || []
    property string variantId: ""
    readonly property string installState: {
        void MarketInstall.revision
        return item.id ? MarketInstall.installState(item, variantId) : "none"
    }
    readonly property bool installing: installState === "installing"
    readonly property var job: {
        void Downloads.revision
        const jobs = Downloads.jobs
        const ids = [item.source + "/" + item.id, "sfx:" + item.id, "drift-asset:" + item.id]
        for (let i = 0; i < jobs.length; ++i) {
            if (ids.indexOf(jobs[i].id) >= 0 || (item.source === "drift-assets" && jobs[i].id.indexOf("drift-asset:" + item.id) === 0))
                return jobs[i]
        }
        return ({})
    }
    readonly property string failureText: {
        if (installState !== "failed")
            return ""
        if (job.errorCode === "app_too_old")
            return job.error || qsTr("This needs a newer version of Drift. Update the app to install it.")
        return job.error || qsTr("The install didn’t finish.")
    }

    color: Theme.c.marketplace.detailBackground

    function kindLabel() {
        switch (kind) {
        case "effect": return qsTr("Effect")
        case "transition": return qsTr("Transition")
        case "audio-effect": return qsTr("Audio effect")
        case "theme": return qsTr("Theme")
        case "look": return qsTr("Look")
        case "effect-template": return qsTr("Effect template")
        case "lottie": return qsTr("Animation")
        case "model3d": return qsTr("3D object")
        case "face-prop": return qsTr("Face prop")
        case "video": return qsTr("Video")
        case "image": return qsTr("Photo")
        case "audio": return qsTr("Audio")
        case "music": return qsTr("Music")
        }
        return ""
    }

    function folderName(url) {
        const s = decodeURIComponent(String(url).replace(/^file:\/\//, ""))
        const parts = s.split(/[\/\\]/).filter(p => p.length > 0)
        return parts.length > 0 ? parts[parts.length - 1] : s
    }

    function formatDuration(ms) {
        const s = Math.round(Number(ms || 0) / 1000)
        return s > 0 ? Math.floor(s / 60) + ":" + (s % 60 < 10 ? "0" : "") + (s % 60) : ""
    }

    // Stock asks for a folder once, then keeps using it; "Change" re-asks.
    function downloadStock() {
        let dir = StockInstaller.lastDownloadDir
        if (FileDialogs.supportsDirectoryPicker() && (!dir || dir.toString() === "")) {
            dir = FileDialogs.openDirectory(qsTr("Save downloads to"), "")
            if (!dir || dir.toString() === "")
                return
            StockInstaller.lastDownloadDir = dir
        }
        StockInstaller.download(item.source, item.id, variantId,
                                FileDialogs.supportsDirectoryPicker() ? dir : "", item.title || "", kind)
    }

    function changeFolder() {
        const dir = FileDialogs.openDirectory(qsTr("Save downloads to"), StockInstaller.lastDownloadDir)
        if (dir && dir.toString() !== "")
            StockInstaller.lastDownloadDir = dir
    }

    function primaryAction() {
        if (family === "stock")
            downloadStock()
        else
            MarketInstall.install(item, variantId)
    }

    function primaryText() {
        if (installing)
            return family === "stock" ? qsTr("Downloading…") : qsTr("Installing…")
        if (family === "stock")
            return installState === "installed" ? qsTr("Download again") : qsTr("Download")
        if (family === "community") {
            if (installState === "installed") return qsTr("Reinstall")
            if (installState === "update") return qsTr("Update")
            return qsTr("Install")
        }
        if (kind === "face-prop")
            return qsTr("Add to Face props")
        return qsTr("Add to media bin")
    }

    onItemChanged: variantId = variants.length > 0 ? variants[0].id : ""
    onShownChanged: {
        if (!shown) {
            player.stop()
            audioPlayer.stop()
        }
    }

    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.AllButtons
        onWheel: (wheel) => { wheel.accepted = true }
    }

    IconButton {
        id: backButton
        x: Theme.pagePadding
        y: Theme.spacingSm
        visible: !root.compact
        glyph: Theme.icons.chevronLeft
        tooltip: qsTr("Back")
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
            topPadding: root.compact ? Theme.spacingSm : 0

            Rectangle {
                width: parent.width
                height: root.previewHeight
                radius: Theme.c.marketplace.previewRadius
                color: Theme.c.marketplace.previewBackground
                clip: true

                AnimatedImage {
                    anchors.fill: parent
                    visible: !root.isAudio && (!root.isVideo || player.playbackState !== MediaPlayer.PlayingState)
                    source: root.shown && !root.isVideo && !root.isAudio
                            ? (root.item.previewUrl || root.item.thumbUrl || "")
                            : (root.item.thumbUrl || "")
                    fillMode: Image.PreserveAspectFit
                    asynchronous: true
                    playing: root.shown
                }
                VideoOutput {
                    id: out
                    anchors.fill: parent
                    visible: root.isVideo
                    fillMode: VideoOutput.PreserveAspectFit
                }
                MediaPlayer {
                    id: player
                    audioOutput: AudioOutput {}
                    videoOutput: out
                    loops: MediaPlayer.Infinite
                    source: root.shown && root.isVideo ? (root.item.previewUrl || "") : ""
                    onSourceChanged: if (source.toString().length > 0) play()
                }

                Row {
                    anchors.centerIn: parent
                    visible: root.isAudio
                    spacing: Theme.spacingXl

                    IconButton {
                        anchors.verticalCenter: parent.verticalCenter
                        enabled: !!root.item.previewUrl
                        glyph: audioPlayer.playbackState === MediaPlayer.PlayingState ? Theme.icons.pause : Theme.icons.play
                        tooltip: audioPlayer.playbackState === MediaPlayer.PlayingState ? qsTr("Pause") : qsTr("Play preview")
                        onClicked: {
                            if (audioPlayer.playbackState === MediaPlayer.PlayingState) {
                                audioPlayer.pause()
                                return
                            }
                            if (audioPlayer.source.toString() !== String(root.item.previewUrl))
                                audioPlayer.source = root.item.previewUrl
                            audioPlayer.play()
                        }
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: root.formatDuration(root.item.durationMs)
                        color: Theme.c.marketplace.foregroundMuted
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSizeSm
                    }
                }
                MediaPlayer {
                    id: audioPlayer
                    audioOutput: AudioOutput {}
                }
            }

            Column {
                width: parent.width
                spacing: Theme.spacingXs

                Text {
                    width: parent.width
                    text: root.item.title || ""
                    color: Theme.c.marketplace.foreground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeBase * 1.2
                    font.weight: Font.DemiBold
                    wrapMode: Text.WordWrap
                }
                Text {
                    width: parent.width
                    visible: text.length > 0
                    text: root.item.subtitle || ""
                    color: Theme.c.marketplace.foregroundMuted
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeSm
                    wrapMode: Text.WordWrap
                }
                Text {
                    width: parent.width
                    visible: text.length > 0
                    text: root.kindLabel()
                    color: Theme.c.marketplace.foregroundMuted
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeXs
                }
            }

            Flow {
                width: parent.width
                spacing: Theme.spacingSm
                visible: (root.item.tags || []).length > 0

                Repeater {
                    model: root.item.tags || []
                    delegate: Rectangle {
                        required property string modelData
                        width: tagText.implicitWidth + Theme.spacingXl
                        height: tagText.implicitHeight + Theme.spacingSm
                        radius: Theme.c.marketplace.badgeRadius
                        color: Theme.c.marketplace.cardBackground
                        Text {
                            id: tagText
                            anchors.centerIn: parent
                            text: modelData
                            color: Theme.c.marketplace.cardForegroundMuted
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSizeXs
                        }
                    }
                }
            }

            Column {
                width: parent.width
                spacing: Theme.spacingXs
                visible: !!root.item.license && !!root.item.license.name

                Text {
                    width: parent.width
                    text: qsTr("Licence")
                    color: Theme.c.marketplace.foregroundMuted
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeXs
                }
                Text {
                    width: parent.width
                    text: root.item.license ? root.item.license.name : ""
                    color: licenseUrl ? (licenseHover.hovered ? Theme.c.marketplace.linkHover : Theme.c.marketplace.link)
                                      : Theme.c.marketplace.foreground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeSm
                    wrapMode: Text.WordWrap
                    readonly property string licenseUrl: root.item.license && root.item.license.url ? root.item.license.url : ""
                    HoverHandler { id: licenseHover; cursorShape: parent.licenseUrl ? Qt.PointingHandCursor : Qt.ArrowCursor }
                    TapHandler {
                        enabled: parent.licenseUrl.length > 0
                        onTapped: root.linkRequested(parent.licenseUrl)
                    }
                }
                Text {
                    width: parent.width
                    visible: text.length > 0
                    text: root.item.license && root.item.license.attribution ? root.item.license.attribution : ""
                    color: Theme.c.marketplace.foregroundMuted
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeXs
                    wrapMode: Text.WordWrap
                }
            }

            Column {
                width: parent.width
                spacing: Theme.spacingXs
                visible: root.variants.length > 1

                Text {
                    text: root.family === "stock" ? qsTr("Size") : qsTr("Style")
                    color: Theme.c.marketplace.foregroundMuted
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeXs
                }
                ThemedComboBox {
                    width: Math.min(parent.width, Math.max(200, widestContentWidth + Theme.spacingXl))
                    textRole: "label"
                    valueRole: "id"
                    model: root.variants
                    currentIndex: Math.max(0, root.variants.findIndex(v => v.id === root.variantId))
                    onActivated: root.variantId = currentValue
                }
            }

            Row {
                width: parent.width
                spacing: Theme.spacingLg
                visible: root.installState === "installed" || root.installState === "update"
                         || root.failureText.length > 0

                IconGlyph {
                    glyph: root.installState === "failed" ? Theme.icons.error : Theme.icons.check
                    iconSize: Theme.iconSizeMd
                    iconColor: root.installState === "failed" ? Theme.c.marketplace.stateFailed
                               : root.installState === "update" ? Theme.c.marketplace.stateUpdate
                               : Theme.c.marketplace.stateInstalled
                }
                Text {
                    width: parent.width - Theme.iconSizeMd - parent.spacing
                    text: root.failureText.length > 0 ? root.failureText
                          : (root.installState === "update" ? qsTr("An update is available")
                                                           : qsTr("Installed"))
                    color: root.failureText.length > 0 ? Theme.c.marketplace.error : Theme.c.marketplace.foreground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeSm
                    wrapMode: Text.WordWrap
                }
            }

            Flow {
                width: parent.width
                spacing: Theme.spacingLg

                ThemedButton {
                    variant: "primary"
                    enabled: !root.installing
                    text: root.primaryText()
                    onClicked: root.primaryAction()
                }
                ThemedButton {
                    visible: (root.family === "drift-assets" || root.family === "sfx") && root.kind !== "face-prop"
                    enabled: !root.installing
                    text: qsTr("Add to timeline")
                    onClicked: root.addToTimelineRequested(root.item)
                }
                ThemedButton {
                    visible: root.family === "community" && !!root.item.webUrl
                    text: qsTr("Open on cutwire.studio")
                    onClicked: root.linkRequested(root.item.webUrl)
                }
            }

            Row {
                width: parent.width
                spacing: Theme.spacingLg
                visible: root.family === "stock" && FileDialogs.supportsDirectoryPicker()

                Text {
                    width: Math.min(implicitWidth, parent.width - changeFolder.width - parent.spacing)
                    text: StockInstaller.lastDownloadDir.toString().length > 0
                          ? qsTr("Saves to %1").arg(root.folderName(StockInstaller.lastDownloadDir))
                          : qsTr("You’ll choose a folder the first time")
                    color: Theme.c.marketplace.foregroundMuted
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeXs
                    elide: Text.ElideMiddle
                }
                Text {
                    id: changeFolder
                    visible: StockInstaller.lastDownloadDir.toString().length > 0
                    text: qsTr("Change")
                    color: changeHover.hovered ? Theme.c.marketplace.linkHover : Theme.c.marketplace.link
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeXs
                    HoverHandler { id: changeHover; cursorShape: Qt.PointingHandCursor }
                    TapHandler { onTapped: root.changeFolder() }
                }
            }
        }
    }
}
