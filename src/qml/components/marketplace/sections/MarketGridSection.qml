import QtQuick
import Drift
// A wrapping grid of cards from one source. The category comes from the page, so a chips
// section can drive it. More is fetched as the page is scrolled near the end of the grid.
Item {
    id: root

    required property var section
    required property var host

    readonly property var source: root.section.source || ({})
    readonly property var sourceInfo: Marketplace.source(root.source.id || "")
    readonly property var sorts: root.sourceInfo.sorts || []
    property string sortValue: root.source.sort || ""

    implicitHeight: column.implicitHeight

    function sortLabel(id) {
        switch (id) {
        case "top": return qsTr("Top")
        case "new": return qsTr("New")
        case "trending": return qsTr("Trending")
        case "popular": return qsTr("Popular")
        }
        return id
    }

    function sortModel() {
        return root.sorts.map(id => ({ id: id, label: root.sortLabel(id) }))
    }

    function checkMore() {
        const flick = root.host.flick
        if (!flick || !browser.hasMore || browser.loading)
            return
        const bottom = root.mapToItem(flick.contentItem, 0, root.height).y
        if (bottom - (flick.contentY + flick.height) < 300)
            browser.loadMore()
    }

    MarketSourceBrowser {
        id: browser
        source: root.source.id || ""
        category: root.host.categoryFor(root.section.id)
        query: searchField.text.trim().length > 0 ? searchField.text.trim() : (root.source.q || "")
        sort: root.sortValue
        period: root.source.period || ""
        provider: root.source.provider || ""
        filters: root.source.filters || ({})
        onItemsChanged: Qt.callLater(root.checkMore)
    }

    Connections {
        target: root.host.flick
        function onContentYChanged() { root.checkMore() }
        function onHeightChanged() { root.checkMore() }
    }

    Column {
        id: column
        width: parent.width
        spacing: Theme.spacingMd

        Text {
            x: root.host.sidePadding
            width: parent.width - root.host.sidePadding * 2
            visible: text.length > 0
            text: root.section.title || ""
            color: Theme.c.marketplace.foreground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeBase
            font.weight: Font.DemiBold
            elide: Text.ElideRight
        }

        Row {
            x: root.host.sidePadding
            width: parent.width - root.host.sidePadding * 2
            spacing: Theme.spacingMd
            visible: !!root.section.searchable || root.sorts.length > 1

            MarketSearchField {
                id: searchField
                visible: !!root.section.searchable
                width: parent.width - (sortBox.visible ? sortBox.width + parent.spacing : 0)
                busy: browser.loading
                placeholderText: qsTr("Search")
                onCancelRequested: browser.cancel()
            }

            ThemedComboBox {
                id: sortBox
                anchors.verticalCenter: parent.verticalCenter
                visible: root.sorts.length > 1
                width: Math.max(120, widestContentWidth + Theme.spacingXl)
                textRole: "label"
                valueRole: "id"
                model: root.sortModel()
                currentIndex: Math.max(0, root.sorts.indexOf(root.sortValue))
                onActivated: root.sortValue = currentValue
            }
        }

        Flow {
            x: root.host.sidePadding
            width: parent.width - root.host.sidePadding * 2
            spacing: Theme.assetCardGap

            Repeater {
                model: browser.items
                delegate: MarketItemCard {
                    required property var modelData
                    item: modelData
                    compact: root.host.compact
                    justAdded: root.host.view.justAddedId === modelData.id
                    onOpenRequested: root.host.openItem(modelData)
                    onActionRequested: root.host.quickAdd(modelData)
                }
            }
        }

        Flow {
            x: root.host.sidePadding
            width: parent.width - root.host.sidePadding * 2
            spacing: Theme.assetCardGap
            visible: browser.loading && browser.items.length === 0

            Repeater {
                model: 8
                delegate: SkeletonBox {
                    width: Theme.assetCardWidth
                    height: Theme.assetCardWidth * 9 / 16
                    animated: parent.visible
                }
            }
        }

        EmptyState {
            visible: browser.error.length > 0 && browser.items.length === 0
            glyph: Theme.icons.error
            compact: true
            title: qsTr("Couldn’t load this")
            hint: browser.error
            actionText: qsTr("Try again")
            onActionTriggered: browser.reload()
        }

        EmptyState {
            visible: !browser.loading && browser.error.length === 0 && browser.items.length === 0
            glyph: Theme.icons.search
            compact: true
            title: searchField.text.trim().length > 0
                   ? qsTr("No results for “%1”").arg(searchField.text.trim())
                   : qsTr("Nothing here yet")
        }

        Item {
            width: parent.width
            height: Theme.controlHeight
            visible: browser.loading && browser.items.length > 0

            CircularProgress {
                anchors.centerIn: parent
                indeterminate: true
                size: Theme.spacing2xl + Theme.spacingSm
            }
        }
    }
}
