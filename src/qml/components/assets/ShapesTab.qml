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
    // The section at the top of the grid; showCategory() scrolls to one.
    readonly property string activeCategory: shapeGrid.currentSectionId
    function showCategory(id) { shapeGrid.scrollToSection(id) }
    // Bumped when the library changes, so the lists are read again.
    property int userShapesTick: 0
    readonly property var allShapes: { void userShapesTick; return EditorState.builtinShapes() }
    readonly property bool hasUserShapes: allShapes.some(s => s.user === true)
    property alias searchText: search.text
    readonly property string query: search.text.trim().toLowerCase()
    Connections {
        target: EditorState
        function onUserShapesChanged() { root.userShapesTick++ }
    }

    function importSvg() {
        const url = FileDialogs.openFile(qsTr("Import SVG"), [qsTr("SVG images (*.svg)")])
        if (url.toString().length > 0)
            EditorState.importSvgToLibrary(url)
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

        Row {
            id: toolbar
            x: Theme.pagePadding
            width: parent.width - Theme.pagePadding * 2
            height: visible ? implicitHeight + Theme.spacingSm : 0
            spacing: Theme.spacingSm

            ThemedButton {
                variant: "secondary"
                glyph: Theme.icons.penTool
                text: qsTr("Draw")
                tooltip: qsTr("Draw a line or custom shape")
                onClicked: EditorState.preview.shapePathMode = "draw"
            }
            ThemedButton {
                variant: "secondary"
                visible: !root.pickerMode
                glyph: Theme.icons.upload
                text: qsTr("Import SVG")
                tooltip: qsTr("Add an SVG file to My shapes")
                onClicked: root.importSvg()
            }
        }

        Text {
            id: userHint
            visible: !root.pickerMode && !root.hasUserShapes && root.query.length === 0
            x: Theme.pagePadding
            width: parent.width - Theme.pagePadding * 2
            height: visible ? implicitHeight + Theme.spacingSm : 0
            text: qsTr("My shapes: draw or import a shape, then Save to My shapes.")
            color: Theme.c.shapesTab.foregroundMuted
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeXs
            wrapMode: Text.WordWrap
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
            height: Math.max(0, parent.height - search.height - Theme.spacingSm - toolbar.height - userHint.height - categoryChips.height)

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
                isMine: function(item) { return item.user === true }
                mineLabel: qsTr("My shapes")
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
