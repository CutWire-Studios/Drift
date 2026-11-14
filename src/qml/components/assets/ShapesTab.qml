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
    // Set by the phone's add page: tap adds, hold opens the card menu, and the last pick is marked.
    property bool pickerMode: false
    property string pickedId: ""

    readonly property var categories: EditorState.builtinShapeCategories()
    readonly property var allShapes: EditorState.builtinShapes()
    // The section at the top of the grid; showCategory() scrolls to one.
    readonly property string activeCategory: shapeGrid.currentSectionId
    function showCategory(id) { shapeGrid.scrollToSection(id) }
    property alias searchText: search.text
    readonly property string query: search.text.trim().toLowerCase()
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
            sections: shapeGrid.visibleSections
            activeCategory: root.activeCategory
            searching: root.query.length > 0
            onCategoryActivated: (categoryId) => shapeGrid.scrollToSection(categoryId)
        }

        Item {
            width: parent.width
            height: Math.max(0, parent.height - search.height - Theme.spacingSm - categoryChips.height)

            Text {
                id: emptySearchHint
                x: Theme.pagePadding
                y: Theme.pagePadding
                width: parent.width - Theme.pagePadding * 2
                visible: shapeGrid.itemCount === 0
                text: qsTr("No shapes match “%1”.").arg(search.text.trim())
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

            SectionedAssetGrid {
                id: shapeGrid
                anchors.fill: parent
                visible: itemCount > 0
                items: root.allShapes
                categories: root.categories
                favoritesKind: "shapes"
                query: root.query
                cellHeight: cardSize + Theme.spacingSm + Math.ceil(labelMetrics.height) + Theme.assetCardGap
                cardDelegate: ShapePresetCard {
                    cardSize: shapeGrid.cardSize
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
