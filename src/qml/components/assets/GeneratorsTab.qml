import QtQuick
import QtQuick.Controls.Basic
import Drift

// Generators tab: solid colours, gradients, noise, test patterns, the countdown leader and the
// particle emitter with its presets. Like shapes, a click puts one on the timeline at the playhead.
Item {
    id: root

    signal added()
    property var headerTabs: []
    property string headerTabId: ""
    signal headerTabSelected(string tabId)

    readonly property var categories: EditorState.generatorCategories()
    readonly property var allGenerators: EditorState.generatorCatalog()
    readonly property string activeCategory: grid.currentSectionId
    property alias searchText: search.text
    readonly property string query: search.text.trim().toLowerCase()
    function showCategory(id) { grid.scrollToSection(id) }

    Column {
        anchors.fill: parent
        spacing: 0

        AssetSearchBar {
            id: search
            tabs: root.headerTabs
            currentTabId: root.headerTabId
            onTabSelected: tabId => root.headerTabSelected(tabId)
            placeholderText: qsTr("Search generators")
            historyKey: "generators"
        }

        Item {
            id: chipsGap
            width: 1
            height: Theme.phone ? 0 : Theme.spacingSm
        }

        AssetCategoryChips {
            id: categoryChips
            flickable: grid
            width: parent.width
            sections: grid.visibleSections
            activeCategory: root.activeCategory
            searching: root.query.length > 0
            onCategoryActivated: categoryId => grid.scrollToSection(categoryId)
        }

        Item {
            width: parent.width
            height: Math.max(0, parent.height - search.height - chipsGap.height - categoryChips.height)

            Text {
                x: Theme.pagePadding
                y: Theme.pagePadding
                width: parent.width - Theme.pagePadding * 2
                visible: grid.itemCount === 0
                text: qsTr("No generators match “%1”.").arg(search.text.trim())
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
                id: grid
                anchors.fill: parent
                visible: itemCount > 0
                items: root.allGenerators
                categories: root.categories
                query: root.query
                cellHeight: cardSize + Theme.spacingSm + Math.ceil(labelMetrics.height) + Theme.assetCardGap
                cardDelegate: GeneratorCard {
                    cardSize: grid.cardSize
                    onAdded: root.added()
                }
            }
        }
    }
}
