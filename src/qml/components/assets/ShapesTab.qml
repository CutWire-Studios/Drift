import QtQuick
import QtQuick.Controls.Basic
import Drift
// Shapes tab. Shapes used to be a page inside the Stickers tab, where they came
// and went with the sticker addon and had no room for categories of their own.
Item {
    id: root

    // A clip landed on the timeline. The phone shell closes the sheet on this —
    // the thing you came for is behind it.
    signal added()

    readonly property string favoritesId: "__favorites__"
    readonly property var categories: EditorState.builtinShapeCategories()
    readonly property var allShapes: EditorState.builtinShapes()
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

    readonly property var currentShapes: {
        void root.favoritesTick
        const q = root.query
        if (q.length > 0) {
            return root.allShapes.filter(function(s) {
                const label = (s.label || "").toLowerCase()
                const id = (s.id || "").toLowerCase()
                return label.indexOf(q) >= 0 || id.indexOf(q) >= 0
            })
        }
        if (root.activeCategory === root.favoritesId) {
            return root.allShapes.filter(function(s) {
                return EditorState.isAssetFavorite("shapes", s.id)
            })
        }
        return root.allShapes.filter(function(s) { return s.category === root.activeCategory })
    }

    Column {
        anchors.fill: parent
        spacing: 0

        AssetSearchBar {
            id: search
            placeholderText: qsTr("Search shapes")
            historyKey: "shapes"
        }

        Item {
            width: 1
            height: Theme.spacingSm
        }

        AssetCategoryChips {
            id: categoryChips
            width: parent.width
            categories: root.categories
            activeCategory: root.activeCategory
            searching: root.query.length > 0
            onCategoryActivated: (categoryId) => root.activeCategory = categoryId
        }

        Item {
            width: parent.width
            height: Math.max(0, parent.height - search.height - Theme.spacingSm - categoryChips.height)

            Text {
                id: emptySearchHint
                x: Theme.pagePadding
                y: Theme.pagePadding
                width: parent.width - Theme.pagePadding * 2
                visible: root.currentShapes.length === 0
                text: root.query.length > 0
                      ? qsTr("No shapes match “%1”.").arg(search.text.trim())
                      : (root.activeCategory === root.favoritesId
                         ? qsTr("No favorites yet. Star shapes to save them here.")
                         : qsTr("Nothing in this category."))
                color: Theme.c.shapesTab.foregroundMuted
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeSm
                wrapMode: Text.WordWrap
            }

            FontMetrics {
                id: labelMetrics
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeCard
            }

            // One trailing gap wider than the padded area, so the cards pack from the left exactly
            // as the old Grid did.
            GridView {
                id: shapeGrid
                // As many columns as fit at the nominal card size, rounded, with the cards stretched or
                // squeezed a little to fill the row — a fixed card left a column's worth of empty space.
                readonly property int columnCount: Math.max(1, Math.round(width / (Theme.assetCardWidth + Theme.assetCardGap)))
                readonly property real cardSize: Math.floor(width / columnCount) - Theme.assetCardGap
                x: Theme.pagePadding
                width: parent.width - Theme.pagePadding * 2 + Theme.assetCardGap
                height: parent.height
                topMargin: Theme.pagePadding
                bottomMargin: Theme.pagePadding
                visible: root.currentShapes.length > 0
                clip: true
                reuseItems: true
                boundsBehavior: Flickable.StopAtBounds
                acceptedButtons: Theme.touchUi ? Qt.LeftButton : Qt.NoButton
                ScrollBar.vertical: AppScrollBar { }
                cellWidth: Math.floor(width / columnCount)
                cellHeight: shapeGrid.cardSize + Theme.spacingSm + Math.ceil(labelMetrics.height) + Theme.assetCardGap
                model: root.currentShapes

                delegate: ShapePresetCard {
                    cardSize: shapeGrid.cardSize
                    onAdded: root.added()
                }
            }
        }
        
    }
}
