import QtQuick
import QtQuick.Controls.Basic
import Drift

// The full Unicode emoji set, offered next to the curated sticker packs — those only cover a
// couple of hundred glyphs. Picking one rasterises it and drops it on an image track, so from the
// timeline's point of view it is the same thing a sticker is.
Popup {
    id: root

    signal addonManagerRequested()
    // A clip landed on the timeline. The phone shell closes the sheet this picker
    // hangs off on this.
    signal added()

    // Rebuilt on install rather than at load: the catalog is empty until the sticker pack, which
    // carries the emoji font, is there.
    property var allEmoji: []
    property var groups: []
    property string fontFamily: ""
    readonly property bool available: fontFamily.length > 0

    property string activeGroup: ""
    readonly property string query: search.text.trim().toLowerCase()

    // Search spans every group — once you have a name, the category is in the way.
    readonly property var visibleEmoji: {
        if (query.length > 0) {
            return allEmoji.filter(function(entry) {
                return entry.name.indexOf(query) >= 0 || entry.keywords.indexOf(query) >= 0
            })
        }
        return allEmoji.filter(function(entry) { return entry.group === activeGroup })
    }

    // Built on first open, not at panel construction: it is ~1900 entries crossing into JS, and
    // most sessions never open the picker at all.
    property bool loaded: false

    function reload() {
        root.loaded = true
        const catalog = EditorState.emojiCatalog()
        // Lower-cased once here so the filter is not re-casing 1900 strings per keystroke.
        for (let i = 0; i < catalog.length; ++i) {
            catalog[i].name = catalog[i].name.toLowerCase()
            catalog[i].keywords = catalog[i].keywords.toLowerCase()
        }
        root.allEmoji = catalog
        root.groups = EditorState.emojiGroups()
        root.fontFamily = EditorState.emojiFontFamily()
        root.activeGroup = root.groups.length > 0 ? root.groups[0] : ""
    }

    Connections {
        target: Addons
        function onKindChanged(kind) {
            if (kind !== "emoji-font")
                return
            root.loaded = false
            if (root.visible)
                root.reload()
        }
    }

    width: 340
    height: 420
    padding: Theme.pagePadding
    modal: false
    focus: true

    onAboutToShow: {
        if (!root.loaded)
            root.reload()
    }

    onOpened: {
        search.clear()
        search.forceActiveFocus()
    }

    background: Rectangle {
        radius: Theme.radiusSm
        color: Theme.panelBackground
        border.width: Theme.borderWidth
        border.color: Theme.panelBorder
    }

    contentItem: Item {
        EmptyState {
            anchors.centerIn: parent
            width: parent.width
            visible: !root.available
            compact: true
            glyph: Theme.icons.smile
            title: qsTr("No emoji pack installed")
            hint: qsTr("Install the sticker pack to use emoji.")
            actionText: qsTr("Get extras")
            actionVariant: "primary"
            onActionTriggered: {
                root.close()
                root.addonManagerRequested()
            }
        }

        ThemedTextField {
            id: search
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            visible: root.available
            placeholderText: qsTr("Search")
            font.family: Theme.fontFamily

            Keys.onEscapePressed: function(event) {
                root.close()
                event.accepted = true
            }
        }

        // The group row scrolls sideways rather than wrapping: nine categories would otherwise eat
        // a third of the popup before a single emoji is visible.
        Flickable {
            id: groupBar
            anchors.top: search.bottom
            anchors.topMargin: Theme.spacingMd
            anchors.left: parent.left
            anchors.right: parent.right
            height: Theme.controlHeightSm
            contentWidth: groupRow.width
            flickableDirection: Flickable.HorizontalFlick
            clip: true
            visible: root.available && root.query.length === 0

            Row {
                id: groupRow
                spacing: Theme.spacingSm

                Repeater {
                    model: root.groups
                    delegate: ThemedChip {
                        required property string modelData
                        text: modelData
                        variant: "secondary"
                        selected: root.activeGroup === modelData
                        onClicked: root.activeGroup = modelData
                    }
                }
            }
        }

        GridView {
            id: grid
            reuseItems: true
            anchors.top: groupBar.bottom
            anchors.topMargin: Theme.spacingMd
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            visible: root.available
            cellWidth: Math.floor(width / Math.max(1, Math.floor(width / 40)))
            cellHeight: cellWidth
            clip: true
            model: root.visibleEmoji
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: AppScrollBar { }

            delegate: Item {
                id: cell
                required property var modelData
                width: grid.cellWidth
                height: grid.cellHeight

                Rectangle {
                    anchors.fill: parent
                    anchors.margins: 2
                    radius: Theme.radiusSm
                    color: cellHover.hovered ? Theme.popoverHover : "transparent"

                    Behavior on color {
                        ColorAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
                    }

                    Text {
                        anchors.centerIn: parent
                        text: cell.modelData.emoji
                        // The addon face, not the system one, so the grid shows what the
                        // rasteriser will actually draw.
                        font.family: root.fontFamily
                        font.pixelSize: Math.round(grid.cellWidth * 0.6)
                    }
                }

                HoverHandler {
                    id: cellHover
                    cursorShape: Qt.PointingHandCursor
                }

                ThemedToolTip {
                    text: cell.modelData.name
                    visible: cellHover.hovered
                }

                TapHandler {
                    onTapped: {
                        EditorState.addEmojiClip(cell.modelData.emoji, cell.modelData.name, -1)
                        root.close()
                        root.added()
                    }
                }
            }
        }

        Text {
            anchors.top: grid.top
            anchors.left: parent.left
            anchors.right: parent.right
            visible: root.available && root.visibleEmoji.length === 0
            text: qsTr("No emoji match “%1”.").arg(search.text)
            color: Theme.mutedForeground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeSm
            elide: Text.ElideRight
        }
    }
}
