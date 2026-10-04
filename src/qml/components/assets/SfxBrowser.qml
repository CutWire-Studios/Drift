import QtQuick
import QtQuick.Controls.Basic
import QtMultimedia
import Drift
import ".."

// CutWire's sound effects: category chips, then a row per sound with an inline preview, its
// waveform and length. The Market field above filters by name. Sounds are downloaded into app
// data on first use, then go onto the timeline ("use") or into the media bin ("keep").
Item {
    id: root

    property string query: ""
    property bool compact: Theme.compact

    property string category: ""
    property string subcategory: ""
    // Actions waiting on a download, keyed by sound id: {id: mode}.
    property var pending: ({})
    property string justAddedId: ""

    readonly property real sidePadding: compact ? Theme.androidPagePadding : Theme.pagePadding
    readonly property var subcategories: {
        const cats = Sfx.categories
        for (let i = 0; i < cats.length; ++i) {
            if (cats[i].id === category)
                return cats[i].subcategories || []
        }
        return []
    }
    readonly property var results: {
        void Sfx.sounds
        return visible ? Sfx.filter(query, category, subcategory) : []
    }

    function act(sound, mode) {
        const next = Object.assign({}, pending)
        next[sound.id] = mode
        pending = next
        Sfx.install(sound.id)
    }

    function formatDuration(seconds) {
        const s = Number(seconds || 0)
        if (s < 10)
            return s.toFixed(1) + "s"
        const n = Math.round(s)
        return Math.floor(n / 60) + ":" + (n % 60 < 10 ? "0" : "") + (n % 60)
    }

    onCategoryChanged: subcategory = ""
    onVisibleChanged: {
        if (!visible)
            player.stop()
    }

    Timer {
        id: addedTimer
        interval: 1500
        onTriggered: root.justAddedId = ""
    }

    Connections {
        target: Sfx

        function onReady(id, binAssetId) {
            const mode = root.pending[id]
            if (!mode)
                return
            const next = Object.assign({}, root.pending)
            delete next[id]
            root.pending = next
            if (mode === "keep")
                AppController.setLastMessage(qsTr("Added to the media bin"), "success")
            else
                AppController.addClipsFromAssets([binAssetId])
            root.justAddedId = id
            addedTimer.restart()
        }

        function onFailed(id, message) {
            const next = Object.assign({}, root.pending)
            delete next[id]
            root.pending = next
            AppController.setLastMessage(message, "error")
        }
    }

    MediaPlayer {
        id: player
        property string soundId: ""
        audioOutput: AudioOutput {}
    }

    EmptyState {
        anchors.centerIn: parent
        width: parent.width
        visible: Sfx.loading && Sfx.sounds.length === 0
        glyph: Theme.icons.spinner
        glyphSpinning: true
        title: qsTr("Loading sound effects…")
    }

    EmptyState {
        anchors.centerIn: parent
        width: parent.width
        visible: !Sfx.loading && Sfx.error.length > 0 && Sfx.sounds.length === 0
        glyph: Theme.icons.error
        title: qsTr("Couldn’t load sound effects")
        hint: Sfx.error
        actionText: qsTr("Try again")
        onActionTriggered: Sfx.refresh()
    }

    Column {
        id: header
        width: parent.width
        spacing: Theme.spacingSm
        topPadding: Theme.spacingMd
        bottomPadding: Theme.spacingSm
        visible: Sfx.sounds.length > 0

        Flickable {
            width: parent.width
            height: Theme.controlHeightSm
            contentWidth: categoryRow.width + root.sidePadding * 2
            flickableDirection: Flickable.HorizontalFlick
            boundsBehavior: Flickable.StopAtBounds
            clip: true

            Row {
                id: categoryRow
                x: root.sidePadding
                height: parent.height
                spacing: Theme.spacingSm

                ThemedChip {
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("All")
                    variant: "secondary"
                    selected: root.category === ""
                    onClicked: root.category = ""
                }
                Repeater {
                    model: Sfx.categories
                    delegate: ThemedChip {
                        required property var modelData
                        required property int index
                        anchors.verticalCenter: parent.verticalCenter
                        text: modelData.label
                        variant: "secondary"
                        accentColor: Theme.categoryColor(index)
                        selected: root.category === modelData.id
                        onClicked: root.category = modelData.id
                    }
                }
            }
        }

        Flickable {
            width: parent.width
            height: Theme.controlHeightSm
            visible: root.subcategories.length > 1
            contentWidth: subRow.width + root.sidePadding * 2
            flickableDirection: Flickable.HorizontalFlick
            boundsBehavior: Flickable.StopAtBounds
            clip: true

            Row {
                id: subRow
                x: root.sidePadding
                height: parent.height
                spacing: Theme.spacingSm

                ThemedChip {
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("All")
                    variant: "secondary"
                    selected: root.subcategory === ""
                    onClicked: root.subcategory = ""
                }
                Repeater {
                    model: root.subcategories
                    delegate: ThemedChip {
                        required property var modelData
                        anchors.verticalCenter: parent.verticalCenter
                        text: modelData.label
                        variant: "secondary"
                        selected: root.subcategory === modelData.id
                        onClicked: root.subcategory = modelData.id
                    }
                }
            }
        }
    }

    Text {
        anchors.top: header.bottom
        anchors.topMargin: Theme.spacingLg
        x: root.sidePadding
        width: parent.width - root.sidePadding * 2
        visible: Sfx.sounds.length > 0 && root.results.length === 0
        text: root.query.length > 0 ? qsTr("No sound effects match “%1”.").arg(root.query)
                                    : qsTr("No sound effects here.")
        color: Theme.mutedForeground
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSizeSm
        wrapMode: Text.WordWrap
    }

    ListView {
        id: list
        anchors.top: header.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.leftMargin: root.sidePadding
        anchors.rightMargin: root.sidePadding
        visible: root.results.length > 0
        model: root.results
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: AppScrollBar { }
        footer: Item { width: 1; height: Theme.spacing3xl }

        delegate: Rectangle {
            id: row
            required property var modelData
            readonly property bool isCurrent: player.soundId === modelData.id
            readonly property bool isPlaying: isCurrent && player.playbackState === MediaPlayer.PlayingState
            readonly property string installState: {
                void Sfx.revision
                return Sfx.state(modelData.id)
            }
            readonly property bool busy: installState === "installing"
            readonly property var peaks: modelData.peaks || []
            readonly property real progress: isCurrent && player.duration > 0
                                             ? Math.min(1, player.position / player.duration) : 0

            width: list.width
            height: Theme.touchUi ? Theme.androidMinTouchTarget + Theme.spacingLg : Theme.controlHeight + Theme.spacingLg
            radius: Theme.radiusSm
            color: rowHover.hovered ? Theme.popoverHover : "transparent"

            HoverHandler { id: rowHover }

            IconButton {
                id: playButton
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                glyph: row.isPlaying ? Theme.icons.pause : Theme.icons.play
                enabled: !!row.modelData.preview_url
                tooltip: row.isPlaying ? qsTr("Pause") : qsTr("Play preview")
                onClicked: {
                    if (row.isPlaying) {
                        player.pause()
                        return
                    }
                    if (!row.isCurrent) {
                        player.soundId = row.modelData.id
                        player.source = row.modelData.preview_url
                    }
                    player.play()
                }
            }

            Column {
                id: label
                anchors.left: playButton.right
                anchors.leftMargin: Theme.spacingMd
                anchors.verticalCenter: parent.verticalCenter
                width: root.compact ? parent.width - playButton.width - trailing.width - Theme.spacingMd * 2
                                    : Math.min(200, (parent.width - playButton.width - trailing.width) * 0.4)
                spacing: 2

                Text {
                    width: parent.width
                    text: row.modelData.name || ""
                    color: Theme.panelForeground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeSm
                    elide: Text.ElideRight
                }
                Text {
                    width: parent.width
                    text: root.formatDuration(row.modelData.duration)
                    color: Theme.mutedForeground
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
                visible: !root.compact && row.peaks.length > 0
                onPaint: {
                    const ctx = getContext("2d")
                    ctx.reset()
                    const n = row.peaks.length
                    if (n === 0)
                        return
                    const step = width / n
                    const bar = Math.max(1, step * 0.6)
                    for (let i = 0; i < n; ++i) {
                        const h = Math.max(2, row.peaks[i] * height)
                        ctx.fillStyle = (i + 0.5) / n <= row.progress ? Theme.primary : Theme.mutedForeground
                        ctx.fillRect(i * step, (height - h) / 2, bar, h)
                    }
                }
                onWidthChanged: requestPaint()
                Connections {
                    target: row
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
                    visible: row.busy
                    indeterminate: true
                    size: Theme.spacing2xl + Theme.spacingSm
                }
                IconButton {
                    anchors.verticalCenter: parent.verticalCenter
                    visible: !row.busy
                    glyph: Theme.icons.folderInput
                    tooltip: qsTr("Add to the media bin")
                    onClicked: root.act(row.modelData, "keep")
                }
                IconButton {
                    anchors.verticalCenter: parent.verticalCenter
                    visible: !row.busy
                    glyph: root.justAddedId === row.modelData.id ? Theme.icons.check : Theme.icons.plus
                    tooltip: qsTr("Add to the timeline")
                    onClicked: root.act(row.modelData, "use")
                }
            }
        }
    }
}
