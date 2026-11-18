import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Window
import Drift
// Stickers tab: category bar over a grid of the built-in and imported sticker sets.
Item {
    id: root

    // A clip landed on the timeline. The phone shell closes the sheet on this —
    // the thing you came for is behind it.
    signal added()
    // Set by the phone's add page: tap adds, hold opens the card menu, and the last pick is marked.
    property bool pickerMode: false
    property string pickedId: ""
    // AssetsPanel owns the emoji picker; the button for it lives in this tab's search bar.
    signal emojiPickerRequested()
    property bool emojiPickerOpen: false

    property var categories: EditorState.builtinStickerCategories()
    property var allStickers: EditorState.builtinStickers()
    property var customPacks: EditorState.customStickerPacks()
    readonly property bool hasStickers: allStickers.length > 0
    // The section at the top of the grid; showCategory() scrolls to one.
    readonly property string activeCategory: stickerGrid.currentSectionId
    function showCategory(id) { stickerGrid.scrollToSection(id) }
    property alias searchText: search.text
    readonly property string query: search.text.trim().toLowerCase()
    function refresh() {
        root.categories = EditorState.builtinStickerCategories()
        root.allStickers = EditorState.builtinStickers()
        root.customPacks = EditorState.customStickerPacks()
    }

    // The native dialog spins a nested event loop and an import rebuilds these lists, so both run
    // through Qt.callLater touching only singletons and captured values.
    function importZip() {
        const title = qsTr("Import sticker pack")
        const filters = [qsTr("Zip archives (*.zip)")]
        Qt.callLater(() => {
            const url = FileDialogs.openFile(title, filters)
            if (url && url.toString() !== "")
                EditorState.importStickerPack(url)
        })
    }

    function importFolder() {
        const title = qsTr("Import sticker pack")
        Qt.callLater(() => {
            const url = FileDialogs.openDirectory(title)
            if (url && url.toString() !== "")
                EditorState.importStickerPack(url)
        })
    }

    Connections {
        target: Addons
        function onKindChanged(kind) {
            if (kind === "stickers")
                root.refresh()
        }
    }

    Connections {
        target: EditorState
        function onStickersChanged() { root.refresh() }
    }

    Column {
        anchors.fill: parent
        spacing: 0

        AssetSearchBar {
            id: search
            placeholderText: qsTr("Search stickers")
            historyKey: "stickers"

            // The sticker packs are a curated subset of the emoji set, so the rest live behind
            // this button rather than being unreachable.
            IconButton {
                glyph: Theme.icons.plus
                variant: "ghost"
                buttonSize: Theme.controlHeight
                tooltip: qsTr("More emoji")
                active: root.emojiPickerOpen
                onClicked: root.emojiPickerRequested()
            }

            IconButton {
                glyph: Theme.icons.upload
                variant: "ghost"
                buttonSize: Theme.controlHeight
                tooltip: qsTr("Import or manage sticker packs")
                onClicked: packMenu.popup(0, height)

                ThemedContextMenu {
                    id: packMenu

                    ThemedMenuItem {
                        text: qsTr("Import sticker pack (.zip)…")
                        icon.name: Theme.icons.upload
                        onTriggered: root.importZip()
                    }
                    ThemedMenuItem {
                        visible: FileDialogs.supportsDirectoryPicker()
                        text: qsTr("Import sticker folder…")
                        icon.name: Theme.icons.folderInput
                        onTriggered: root.importFolder()
                    }
                    Instantiator {
                        model: root.customPacks
                        delegate: ThemedMenuItem {
                            required property var modelData
                            text: qsTr("Remove “%1”").arg(modelData.name)
                            icon.name: Theme.icons.trash
                            onTriggered: {
                                const id = modelData.id
                                Qt.callLater(() => EditorState.removeStickerPack(id))
                            }
                        }
                        onObjectAdded: (index, object) => packMenu.addItem(object)
                        onObjectRemoved: (index, object) => packMenu.removeItem(object)
                    }
                }
            }
        }

        Item {
            width: 1
            height: Theme.spacingSm
        }

        AssetCategoryChips {
            id: categoryChips
            width: parent.width
            sections: root.hasStickers ? stickerGrid.visibleSections : []
            activeCategory: root.activeCategory
            searching: root.query.length > 0
            onCategoryActivated: (categoryId) => stickerGrid.scrollToSection(categoryId)
        }

        Item {
            width: parent.width
            height: Math.max(0, parent.height - search.height
                             - Theme.spacingSm - categoryChips.height)

            EmptyState {
                visible: !root.hasStickers
                x: Theme.pagePadding
                y: Theme.pagePadding
                width: parent.width - Theme.pagePadding * 2
                compact: true
                glyph: Theme.icons.smile
                title: qsTr("No sticker packs installed")
                hint: qsTr("Install the emoji pack to add stickers.")
                actionText: qsTr("Get extras")
                actionVariant: "primary"
                onActionTriggered: ShellBridge.main.openAddonManager("stickers")
            }

            EmptyState {
                visible: root.hasStickers && stickerGrid.itemCount === 0
                x: Theme.pagePadding
                y: Theme.pagePadding
                width: parent.width - Theme.pagePadding * 2
                compact: true
                glyph: Theme.icons.smile
                title: qsTr("No stickers match “%1”").arg(search.text.trim())
                hint: qsTr("Try a different name.")
            }

            FontMetrics {
                id: labelMetrics
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeCard
            }

            SectionedAssetGrid {
                id: stickerGrid
                anchors.fill: parent
                visible: itemCount > 0
                items: root.allStickers
                categories: root.categories
                favoritesKind: "stickers"
                query: root.query
                cellHeight: cardSize + Theme.spacingSm + Math.ceil(labelMetrics.height) + Theme.assetCardGap
                cardDelegate: StickerPresetCard {
                    cardSize: stickerGrid.cardSize
                    pickerMode: root.pickerMode
                    picked: root.pickerMode && root.pickedId === modelData.id
                    onAdded: {
                        root.pickedId = modelData.id
                        root.added()
                    }
                }
            }
        }
        
    }
}
