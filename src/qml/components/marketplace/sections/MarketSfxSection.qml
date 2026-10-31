import QtQuick
import QtQuick.Controls.Basic
import QtMultimedia
import Drift
// CutWire's sound effects: category chips (a group's sounds as a second row), then a row per
// sound with an inline preview, its waveform and length. Sounds install on first use, then go
// onto the timeline or stay in the media bin.
Item {
    id: root

    required property var section
    required property var host

    readonly property var source: root.section.source || ({})
    readonly property real sidePadding: root.host.sidePadding

    onVisibleChanged: if (!visible) player.stop()

    MarketSourceBrowser {
        id: browser
        source: root.source.id || ""
        category: root.host.categoryFor(root.section.id)
        query: searchField.text.trim()
        limit: 50
    }

    MediaPlayer {
        id: player
        property string soundId: ""
        audioOutput: AudioOutput {}
    }

    Column {
        id: header
        width: parent.width
        spacing: Theme.spacingSm
        topPadding: Theme.spacingSm

        MarketSearchField {
            id: searchField
            x: root.sidePadding
            width: parent.width - root.sidePadding * 2
            busy: browser.loading
            placeholderText: qsTr("Search sound effects")
            onCancelRequested: browser.cancel()
        }

        MarketChipsSection {
            width: parent.width
            section: ({ source: root.source, target: root.section.id })
            host: root.host
        }
    }

    EmptyState {
        anchors.centerIn: parent
        width: parent.width
        visible: browser.loading && browser.items.length === 0
        glyph: Theme.icons.spinner
        glyphSpinning: true
        title: qsTr("Loading sound effects…")
    }

    EmptyState {
        anchors.centerIn: parent
        width: parent.width
        visible: !browser.loading && browser.error.length > 0 && browser.items.length === 0
        glyph: Theme.icons.error
        title: qsTr("Couldn’t load sound effects")
        hint: browser.error
        actionText: qsTr("Try again")
        onActionTriggered: browser.reload()
    }

    Text {
        anchors.top: header.bottom
        anchors.topMargin: Theme.spacingLg
        x: root.sidePadding
        width: parent.width - root.sidePadding * 2
        visible: !browser.loading && browser.error.length === 0 && browser.items.length === 0
        text: searchField.text.trim().length > 0
              ? qsTr("No sound effects match “%1”.").arg(searchField.text.trim())
              : qsTr("No sound effects here.")
        color: Theme.c.marketplace.foregroundMuted
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSizeSm
        wrapMode: Text.WordWrap
    }

    ListView {
        anchors.top: header.bottom
        anchors.topMargin: Theme.spacingSm
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.leftMargin: root.sidePadding
        anchors.rightMargin: root.sidePadding
        visible: browser.items.length > 0
        model: browser.items
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: AppScrollBar { }
        footer: Item { width: 1; height: Theme.spacing3xl }
        onAtYEndChanged: if (atYEnd && browser.hasMore && !browser.loading) browser.loadMore()

        delegate: SfxSoundRow {
            compact: root.host.compact
            justAddedId: root.host.view.justAddedId
            previewPlayer: player
            onOpenRequested: (sound) => root.host.openItem(sound)
            onKeepRequested: (sound) => root.host.view.timelineAdd.request(sound, "keep")
            onUseRequested: (sound) => root.host.view.timelineAdd.request(sound, "use")
        }
    }
}
