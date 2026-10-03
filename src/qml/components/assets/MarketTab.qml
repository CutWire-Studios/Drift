import QtQuick
import Drift
import ".."

// The Market: one search field, then text tabs, Drift Assets first and the stock types from
// the marketplace catalog after it. Drift Assets are CutWire's own and free, so they need no
// opt-in; the consent gate stands in front of the stock tabs only, which is where the quotas
// and third-party terms it explains actually apply.
Item {
    id: root

    // Phone layout. Defaults to the window's own size class; the host can force it.
    property bool compact: Theme.compact
    property alias searchText: field.text
    property alias submittedQuery: stock.submittedQuery
    // "assets", or a Market type id.
    property string section: "assets"

    readonly property string query: field.text.trim()
    readonly property real sidePadding: compact ? Theme.androidPagePadding : Theme.pagePadding
    // Until the catalog is in (no consent yet, still loading, or unreachable) the stock tabs
    // collapse into one that leads to the consent panel or the loading and error states.
    readonly property var stockTabs: Market.consented && Market.types.length > 0
                                     ? Market.types
                                     : [{ id: "stock", label: qsTr("Stock") }]

    // Android Back, forwarded by AndroidMarket.
    function handleBack() {
        if (root.section === "assets")
            return home.handleBack()
        return stock.handleBack()
    }

    function showSection(id) {
        root.section = id
        if (id !== "assets" && id !== "stock" && Market.activeTypeId !== id)
            Market.activeTypeId = id
    }

    // A search that the pack could not answer, carried over to stock footage.
    function searchStock(typeId) {
        if (!Market.consented) {
            root.showSection("stock")
            return
        }
        // Switching type already starts a search with the field's text; only an unchanged type
        // needs asking.
        const sameType = Market.activeTypeId === typeId
        root.showSection(typeId)
        if (sameType)
            stock.runSearch()
    }

    // Market.types is not persisted, and StockBrowser can't ask for it: it only shows once the
    // types are there. Without this, consent from an earlier run leaves no stock tabs at all.
    onVisibleChanged: ensureCatalog()
    Component.onCompleted: ensureCatalog()
    function ensureCatalog() {
        if (!visible || !Market.configured || !Market.consented)
            return
        if (Market.types.length === 0 && !Market.catalogLoading)
            Market.refreshCatalog()
    }

    Connections {
        target: Market
        function onActiveTypeIdChanged() {
            if (root.section !== "assets" && Market.activeTypeId)
                root.section = Market.activeTypeId
        }
        function onConsentedChanged() {
            if (Market.consented && root.section === "stock")
                root.section = Market.activeTypeId || "stock"
        }
    }

    EmptyState {
        anchors.centerIn: parent
        width: parent.width
        visible: !Market.configured
        glyph: Theme.icons.store
        title: qsTr("Marketplace unavailable")
        hint: qsTr("This build does not include the marketplace.")
    }

    Column {
        id: top
        visible: Market.configured
        width: parent.width
        spacing: Theme.spacingMd
        topPadding: Theme.spacingLg

        MarketSearchField {
            id: field
            x: root.sidePadding
            width: parent.width - root.sidePadding * 2
            busy: root.section !== "assets" && Market.searching
            placeholderText: root.section === "assets"
                             ? qsTr("Search assets")
                             : (Market.canResolve ? qsTr("Search, or paste a link") : qsTr("Search"))
            onSubmitted: {
                if (root.section !== "assets" && Market.consented)
                    stock.submitQuery()
            }
            onCancelRequested: Market.cancelSearch()
        }

        // Text tabs: the medium is the first choice, so it stays visible rather than collapsed
        // into a dropdown.
        Flickable {
            width: parent.width
            height: tabRow.height
            contentWidth: tabRow.width + root.sidePadding * 2
            flickableDirection: Flickable.HorizontalFlick
            boundsBehavior: Flickable.StopAtBounds
            clip: true

            Row {
                id: tabRow
                x: root.sidePadding
                spacing: Theme.spacing2xl

                Repeater {
                    model: [{ id: "assets", label: qsTr("Assets") }].concat(root.stockTabs)
                    delegate: Item {
                        required property var modelData
                        readonly property bool active: root.section === modelData.id
                        width: tabLabel.implicitWidth
                        height: tabLabel.implicitHeight + Theme.spacingLg + 2
                        Accessible.role: Accessible.PageTab
                        Accessible.name: tabLabel.text
                        Accessible.checked: active

                        Text {
                            id: tabLabel
                            anchors.top: parent.top
                            anchors.topMargin: Theme.spacingSm
                            text: modelData.label || modelData.id
                            color: parent.active || tabHover.hovered ? Theme.panelForeground : Theme.mutedForeground
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSizeSm
                            font.weight: parent.active ? Font.DemiBold : Font.Medium
                        }
                        Rectangle {
                            anchors.bottom: parent.bottom
                            width: parent.width
                            height: 2
                            radius: 1
                            color: Theme.primary
                            opacity: parent.active ? 1 : 0
                            Behavior on opacity { NumberAnimation { duration: Theme.durationBase; easing.type: Theme.easing } }
                        }
                        HoverHandler { id: tabHover; cursorShape: Qt.PointingHandCursor }
                        TapHandler { onTapped: root.showSection(modelData.id) }
                    }
                }
            }
        }

        Rectangle {
            width: parent.width
            height: Theme.borderWidth
            color: Theme.panelBorder
        }
    }

    Item {
        id: pages
        visible: Market.configured
        anchors.top: top.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        clip: true

        DriftAssetsHome {
            id: home
            anchors.fill: parent
            visible: root.section === "assets"
            compact: root.compact
            query: root.section === "assets" ? root.query : ""
            stockTypes: Market.consented && Market.types.length > 0
                        ? Market.types : [{ id: "stock", label: qsTr("Stock footage") }]
            onSearchStockRequested: (typeId) => root.searchStock(typeId)
        }

        MarketConsentPanel {
            anchors.fill: parent
            visible: root.section !== "assets" && !Market.consented
            sideMargin: root.sidePadding
            onAccepted: root.ensureCatalog()
        }

        EmptyState {
            anchors.centerIn: parent
            width: parent.width
            visible: root.section !== "assets" && Market.consented && Market.catalogLoading
                     && Market.types.length === 0
            glyph: Theme.icons.spinner
            glyphSpinning: true
            title: qsTr("Loading sources…")
        }

        EmptyState {
            anchors.centerIn: parent
            width: parent.width
            visible: root.section !== "assets" && Market.consented && !Market.catalogLoading
                     && Market.catalogError.length > 0 && Market.types.length === 0
            glyph: Theme.icons.error
            title: qsTr("Couldn’t reach the marketplace")
            hint: Market.catalogError
            // Only when trying again could differ: a signing mismatch fails identically forever.
            actionText: Market.catalogErrorRetryable ? qsTr("Try again") : ""
            onActionTriggered: Market.refreshCatalog()
        }

        StockBrowser {
            id: stock
            anchors.fill: parent
            visible: root.section !== "assets" && Market.consented && Market.types.length > 0
            compact: root.compact
            query: root.query
        }
    }
}
