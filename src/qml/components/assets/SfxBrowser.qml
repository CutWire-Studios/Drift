import QtQuick
import QtQuick.Controls.Basic
import QtMultimedia
import Drift
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

        ScrollStrip {
            width: parent.width
            height: Theme.controlHeightSm
            padding: root.sidePadding

            Row {
                id: categoryRow
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

        ScrollStrip {
            width: parent.width
            height: Theme.controlHeightSm
            visible: root.subcategories.length > 1
            padding: root.sidePadding

            Row {
                id: subRow
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
        color: Theme.c.sfxBrowser.foregroundMuted
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

        delegate: SfxSoundRow {
            compact: root.compact
            justAddedId: root.justAddedId
            previewPlayer: player
            onKeepRequested: (sound) => root.act(sound, "keep")
            onUseRequested: (sound) => root.act(sound, "use")
        }
    }
}
