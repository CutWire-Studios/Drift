import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Window
import Drift
// Stickers tab: category bar over a grid of the built-in sticker set.
Item {
    id: root

    // A clip landed on the timeline. The phone shell closes the sheet on this —
    // the thing you came for is behind it.
    signal added()
    // AssetsPanel owns the emoji picker; the button for it lives in this tab's search bar.
    signal emojiPickerRequested()
    property bool emojiPickerOpen: false

    property var categories: EditorState.builtinStickerCategories()
    property var allStickers: EditorState.builtinStickers()
    readonly property bool hasStickers: allStickers.length > 0
    // The section at the top of the grid; showCategory() scrolls to one.
    readonly property string activeCategory: stickerGrid.currentSectionId
    function showCategory(id) { stickerGrid.scrollToSection(id) }
    property alias searchText: search.text
    readonly property string query: search.text.trim().toLowerCase()
    Connections {
        target: Addons
        function onKindChanged(kind) {
            if (kind !== "stickers")
                return
            root.categories = EditorState.builtinStickerCategories()
            root.allStickers = EditorState.builtinStickers()
        }
    }

    Column {
        anchors.fill: parent
        spacing: 0

        AssetSearchBar {
            id: search
            visible: root.hasStickers
            height: visible ? implicitHeight : 0
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
        }

        Item {
            width: 1
            height: root.hasStickers ? Theme.spacingSm : 0
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
                             - (root.hasStickers ? Theme.spacingSm : 0) - categoryChips.height)

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
                    onAdded: root.added()
                }
            }
        }
        
    }
}
