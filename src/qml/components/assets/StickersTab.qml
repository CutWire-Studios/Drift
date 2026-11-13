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

    readonly property string favoritesId: "__favorites__"
    property var categories: EditorState.builtinStickerCategories()
    property var allStickers: EditorState.builtinStickers()
    readonly property bool hasStickers: allStickers.length > 0
    property string activeCategory: categories.length > 0 ? categories[0].id : ""
    property alias searchText: search.text
    readonly property string query: search.text.trim().toLowerCase()
    property int favoritesTick: 0

    Connections {
        target: EditorState
        function onAssetFavoritesChanged() {
            root.favoritesTick++
        }
    }

    readonly property var currentStickers: {
        void root.favoritesTick
        const q = root.query
        if (q.length > 0) {
            return root.allStickers.filter(function(s) {
                const label = (s.label || "").toLowerCase()
                const id = (s.id || "").toLowerCase()
                return label.indexOf(q) >= 0 || id.indexOf(q) >= 0
            })
        }
        if (root.activeCategory === root.favoritesId) {
            return root.allStickers.filter(function(s) {
                return EditorState.isAssetFavorite("stickers", s.id)
            })
        }
        return root.allStickers.filter(function(s) { return s.category === root.activeCategory })
    }

    Connections {
        target: Addons
        function onKindChanged(kind) {
            if (kind !== "stickers")
                return
            root.categories = EditorState.builtinStickerCategories()
            root.allStickers = EditorState.builtinStickers()
            root.activeCategory = root.categories.length > 0 ? root.categories[0].id : ""
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
            categories: root.hasStickers ? root.categories : []
            activeCategory: root.activeCategory
            searching: root.query.length > 0
            onCategoryActivated: (categoryId) => root.activeCategory = categoryId
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
                visible: root.hasStickers && root.currentStickers.length === 0
                x: Theme.pagePadding
                y: Theme.pagePadding
                width: parent.width - Theme.pagePadding * 2
                compact: true
                glyph: Theme.icons.smile
                title: root.query.length > 0
                       ? qsTr("No stickers match “%1”").arg(search.text.trim())
                       : (root.activeCategory === root.favoritesId
                          ? qsTr("No favorites yet")
                          : qsTr("Nothing in this category"))
                hint: root.query.length > 0
                      ? qsTr("Try a different name.")
                      : (root.activeCategory === root.favoritesId
                         ? qsTr("Star stickers to save them here.")
                         : qsTr("Pick another category."))
            }

            FontMetrics {
                id: labelMetrics
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeCard
            }

            // One trailing gap wider than the padded area, so the cards pack from the left
            // exactly as the old Grid did.
            GridView {
                id: stickerGrid
                // As many columns as fit at the nominal card size, rounded, with the cards stretched or
                // squeezed a little to fill the row — a fixed card left a column's worth of empty space.
                readonly property int columnCount: Math.max(1, Math.round(width / (Theme.assetCardWidth + Theme.assetCardGap)))
                readonly property real cardSize: Math.floor(width / columnCount) - Theme.assetCardGap
                x: Theme.pagePadding
                width: parent.width - Theme.pagePadding * 2 + Theme.assetCardGap
                height: parent.height
                topMargin: Theme.pagePadding
                bottomMargin: Theme.pagePadding
                visible: root.currentStickers.length > 0
                clip: true
                reuseItems: true
                // A mouse drag on a card drags the sticker; the wheel and touchpad scroll.
                acceptedButtons: Theme.touchUi ? Qt.LeftButton : Qt.NoButton
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: AppScrollBar { }
                cellWidth: Math.floor(width / columnCount)
                cellHeight: stickerGrid.cardSize + Theme.spacingSm + Math.ceil(labelMetrics.height) + Theme.assetCardGap
                model: root.currentStickers

                delegate: StickerPresetCard {
                    cardSize: stickerGrid.cardSize
                    onAdded: root.added()
                }
            }
        }
        
    }
}
