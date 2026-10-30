import QtQuick
import Drift
// A titled row of cards from one source, limited to a handful, with "See all" leading to a
// page. Hidden when the source has nothing to show.
Item {
    id: root

    required property var section
    required property var host

    readonly property var source: root.section.source || ({})
    readonly property bool hasSeeAll: !!root.section.seeAll && !!root.section.seeAll.page

    readonly property bool shown: browser.loading || browser.items.length > 0 || browser.error.length > 0

    visible: shown
    implicitHeight: shown ? column.implicitHeight : 0

    MarketSourceBrowser {
        id: browser
        source: root.source.id || ""
        category: root.source.category || ""
        query: root.source.q || ""
        sort: root.source.sort || ""
        period: root.source.period || ""
        provider: root.source.provider || ""
        filters: root.source.filters || ({})
        limit: root.section.limit || 12
    }

    Column {
        id: column
        width: parent.width
        spacing: Theme.spacingMd

        Item {
            x: root.host.sidePadding
            width: parent.width - root.host.sidePadding * 2
            height: Math.max(titleText.implicitHeight, seeAll.implicitHeight)

            Text {
                id: titleText
                anchors.left: parent.left
                anchors.right: seeAll.visible ? seeAll.left : parent.right
                anchors.rightMargin: Theme.spacingMd
                anchors.verticalCenter: parent.verticalCenter
                text: root.section.title || ""
                color: Theme.c.marketplace.foreground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeBase
                font.weight: Font.DemiBold
                elide: Text.ElideRight
            }

            Text {
                id: seeAll
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                visible: root.hasSeeAll
                text: qsTr("See all")
                color: seeAllHover.hovered ? Theme.c.marketplace.linkHover : Theme.c.marketplace.link
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeSm
                Accessible.role: Accessible.Link
                HoverHandler { id: seeAllHover; cursorShape: Qt.PointingHandCursor }
                TapHandler { onTapped: root.host.openPage(root.section.seeAll.page) }
            }
        }

        Row {
            x: root.host.sidePadding
            spacing: Theme.assetCardGap
            visible: browser.loading && browser.items.length === 0

            Repeater {
                model: 5
                delegate: SkeletonBox {
                    width: Theme.assetCardWidth
                    height: Theme.assetCardWidth * 9 / 16
                    animated: parent.visible
                }
            }
        }

        Row {
            x: root.host.sidePadding
            spacing: Theme.spacingMd
            visible: browser.error.length > 0 && browser.items.length === 0

            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: browser.error
                color: Theme.c.marketplace.error
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeSm
            }
            ThemedButton {
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("Try again")
                onClicked: browser.reload()
            }
        }

        ScrollStrip {
            width: parent.width
            height: cardRow.height
            padding: root.host.sidePadding
            visible: browser.items.length > 0

            Row {
                id: cardRow
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
        }
    }
}
