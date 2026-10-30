import QtQuick
import QtQuick.Controls.Basic
import Drift
// One page of the server layout: its sections in order, each loaded by type. A section type
// this build does not know, or one whose source is gone from the manifest, is skipped. A page
// whose only section manages its own scrolling (a browser or the downloads list) fills the view;
// otherwise the sections stack in one scrolling column.
//
// Page-level state: a chips section does not own a list, it names the grid it filters, so the
// chosen category lives here and the grid reads it back.
Item {
    id: root

    required property var page
    required property var view
    property bool compact: Theme.compact

    readonly property real sidePadding: compact ? Theme.androidPagePadding : Theme.pagePadding
    readonly property alias flick: flickable
    property var categories: ({})
    readonly property var sections: page.sections || []
    readonly property bool singleFill: sections.length === 1 && isFill(sections[0].type)

    function isFill(type) {
        return type === "stock-browser" || type === "sfx-browser" || type === "downloads"
    }

    function usable(section) {
        const known = ["hero", "notice", "shelf", "grid", "chips", "stock-browser", "sfx-browser", "downloads"]
        if (known.indexOf(section.type) < 0)
            return false
        if (section.source && Object.keys(Marketplace.source(section.source.id)).length === 0)
            return false
        return true
    }

    function gridCategory(gridId) {
        for (let i = 0; i < sections.length; ++i) {
            if (sections[i].id === gridId && sections[i].source)
                return sections[i].source.category || ""
        }
        return ""
    }

    function categoryFor(gridId) {
        return categories.hasOwnProperty(gridId) ? categories[gridId] : gridCategory(gridId)
    }

    function setCategory(gridId, category) {
        const next = Object.assign({}, categories)
        next[gridId] = category
        categories = next
    }

    function openItem(item) { view.showItem(item) }
    function openPage(id) { view.selectPage(id) }
    function runAction(action) { view.runAction(action) }
    function quickAdd(item) { view.quickAdd(item) }

    Flickable {
        id: flickable
        anchors.fill: parent
        interactive: !root.singleFill
        contentHeight: root.singleFill ? height : column.implicitHeight + Theme.spacing3xl
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: AppScrollBar { }

        Column {
            id: column
            width: flickable.width
            spacing: Theme.spacingXl
            topPadding: root.singleFill ? 0 : Theme.spacingLg

            Repeater {
                model: root.sections

                delegate: Loader {
                    id: entry
                    required property var modelData
                    readonly property bool fills: root.isFill(modelData.type)
                    width: column.width
                    height: fills ? (root.singleFill ? root.height : Math.max(360, root.height - Theme.spacing3xl))
                                  : (item ? item.implicitHeight : 0)
                    visible: root.usable(modelData)
                    active: visible
                    sourceComponent: {
                        switch (modelData.type) {
                        case "hero": return hero
                        case "notice": return notice
                        case "shelf": return shelf
                        case "grid": return grid
                        case "chips": return chips
                        case "stock-browser": return stock
                        case "sfx-browser": return sfx
                        case "downloads": return downloads
                        }
                        return null
                    }

                    Component { id: hero; MarketHeroSection { section: entry.modelData; host: root } }
                    Component { id: notice; MarketNoticeSection { section: entry.modelData; host: root } }
                    Component { id: shelf; MarketShelfSection { section: entry.modelData; host: root } }
                    Component { id: grid; MarketGridSection { section: entry.modelData; host: root } }
                    Component { id: chips; MarketChipsSection { section: entry.modelData; host: root } }
                    Component { id: stock; MarketStockBrowserSection { section: entry.modelData; host: root } }
                    Component { id: sfx; MarketSfxSection { section: entry.modelData; host: root } }
                    Component { id: downloads; MarketDownloadsSection { section: entry.modelData; host: root } }
                }
            }
        }
    }
}
