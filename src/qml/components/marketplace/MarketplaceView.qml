import QtQuick
import QtQuick.Controls.Basic
import Drift
// The Marketplace: a tab per page of the server layout, then a fixed Downloads tab. One page
// shows at a time, and a page keeps its scroll position and search once it has been visited.
// A listing opens in a detail layer over everything (a slide-over on desktop, a bottom sheet
// when compact). The same view fills the desktop window and the phone page.
//
// open(pageOrTab, source, itemId) is the one entry point for navigation: a page id or
// "downloads", plus an optional listing to show.
Item {
    id: root

    property bool compact: Theme.compact
    property string currentPage: ""
    // The last page shown that was not Downloads, so the header button can go back to it.
    property string lastPage: ""
    property var detailItem: ({})
    property bool detailShown: false

    readonly property alias timelineAdd: addHelper
    readonly property string justAddedId: addHelper.justAddedId
    readonly property real sidePadding: compact ? Theme.androidPagePadding : Theme.pagePadding
    readonly property var pages: Marketplace.pages
    readonly property var tabs: pages.map(p => ({ id: p.id, label: p.title }))
                                     .concat([{ id: "downloads", label: qsTr("Downloads") }])
    readonly property string activePage: {
        if (currentPage === "downloads")
            return currentPage
        for (let i = 0; i < pages.length; ++i) {
            if (pages[i].id === currentPage)
                return currentPage
        }
        return pages.length > 0 ? pages[0].id : ""
    }
    property var visited: ({})

    function markVisited() {
        if (activePage.length === 0)
            return
        if (!visited[activePage]) {
            const next = Object.assign({}, visited)
            next[activePage] = true
            visited = next
        }
        if (activePage !== "downloads")
            lastPage = activePage
    }

    onActivePageChanged: markVisited()

    function selectPage(id) {
        if (id === "downloads") {
            currentPage = id
            return true
        }
        for (let i = 0; i < pages.length; ++i) {
            if (pages[i].id === id) {
                currentPage = id
                return true
            }
        }
        return false
    }

    function open(pageOrTab, source, itemId) {
        if (pageOrTab && pageOrTab.length > 0)
            selectPage(pageOrTab)
        if (source && itemId) {
            itemLoader.source = source
            itemLoader.itemId = itemId
            itemLoader.load()
            loadingItem = true
        }
    }

    property bool loadingItem: false

    function showItem(item) {
        detailItem = item
        detailShown = true
    }

    // Android Back, forwarded by the host. True when it closed something.
    function handleBack() {
        if (detailShown) {
            detailShown = false
            return true
        }
        return false
    }

    function quickAdd(item) {
        if (item.mediaKind === "face-prop" || item.source.indexOf("community.") === 0)
            MarketInstall.install(item)
        else
            addHelper.request(item, "use")
    }

    function runAction(action) {
        if (!action)
            return
        switch (action.type) {
        case "open-page":
            selectPage(action.page)
            break
        case "open-item":
            open("", action.source, action.id)
            break
        case "open-url":
            openUrl(action.url)
            break
        }
    }

    function isTrustedHost(url) {
        const match = /^https:\/\/([^\/:?#]+)/i.exec(url)
        if (!match)
            return false
        const host = match[1].toLowerCase()
        return host === "cutwire.studio" || host.endsWith(".cutwire.studio")
    }

    // https only; anything outside cutwire.studio is confirmed first.
    function openUrl(url) {
        const text = String(url || "")
        if (!/^https:\/\//i.test(text))
            return
        if (isTrustedHost(text)) {
            Qt.openUrlExternally(text)
            return
        }
        urlDialog.url = text
        urlDialog.open()
    }

    Component.onCompleted: {
        Marketplace.refresh()
        markVisited()
    }

    MarketTimelineAdd { id: addHelper }

    MarketItemLoader {
        id: itemLoader
        onItemChanged: {
            if (root.loadingItem && item.id) {
                root.loadingItem = false
                root.showItem(item)
            }
        }
        onErrorChanged: {
            if (root.loadingItem && error.length > 0) {
                root.loadingItem = false
                AppController.setLastMessage(error, "error")
            }
        }
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.c.marketplace.background
    }

    Column {
        id: top
        width: parent.width
        spacing: Theme.spacingSm
        topPadding: Theme.spacingLg

        ScrollStrip {
            width: parent.width
            height: tabRow.height
            padding: root.sidePadding

            Row {
                id: tabRow
                spacing: Theme.spacing2xl

                Repeater {
                    model: root.tabs
                    delegate: Row {
                        id: tabEntry
                        required property var modelData
                        spacing: Theme.spacingSm

                        MarketSectionTab {
                            modelData: tabEntry.modelData
                            section: root.activePage
                            onSectionSelected: (id) => root.selectPage(id)
                        }
                        Rectangle {
                            visible: tabEntry.modelData.id === "downloads" && Downloads.activeCount > 0
                            y: Theme.spacingSm
                            width: Math.max(Theme.spacingXl, countText.implicitWidth + Theme.spacingSm)
                            height: Theme.spacingXl
                            radius: Theme.c.marketplace.badgeRadius
                            color: Theme.c.marketplace.badge

                            Text {
                                id: countText
                                anchors.centerIn: parent
                                text: String(Downloads.activeCount)
                                color: Theme.c.marketplace.badgeForeground
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontSizeXs
                            }
                        }
                    }
                }
            }
        }

        Rectangle {
            width: parent.width
            height: Theme.c.marketplace.borderWidth
            color: Theme.c.marketplace.border
        }

        Rectangle {
            x: root.sidePadding
            width: parent.width - root.sidePadding * 2
            height: bannerRow.implicitHeight + Theme.spacingLg * 2
            radius: Theme.c.marketplace.bannerRadius
            color: Theme.c.marketplace.bannerBackground
            visible: Marketplace.offline || Marketplace.error.length > 0

            Row {
                id: bannerRow
                anchors.fill: parent
                anchors.margins: Theme.spacingLg
                spacing: Theme.spacingLg

                IconGlyph {
                    anchors.verticalCenter: parent.verticalCenter
                    glyph: Theme.icons.warning
                    iconSize: Theme.iconSizeBase
                    iconColor: Theme.c.marketplace.bannerIcon
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    width: parent.width - Theme.iconSizeBase - retry.width - parent.spacing * 2
                    text: Marketplace.error.length > 0 ? Marketplace.error
                                                       : qsTr("You’re offline. Showing what was saved last time.")
                    color: Theme.c.marketplace.bannerForeground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeSm
                    wrapMode: Text.WordWrap
                }
                ThemedButton {
                    id: retry
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Retry")
                    enabled: !Marketplace.loading
                    onClicked: Marketplace.refresh()
                }
            }
        }
    }

    Item {
        id: stage
        anchors.top: top.bottom
        anchors.topMargin: Theme.spacingSm
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        clip: true

        EmptyState {
            anchors.centerIn: parent
            width: parent.width
            visible: root.pages.length === 0
            glyph: Marketplace.loading ? Theme.icons.spinner : Theme.icons.store
            glyphSpinning: Marketplace.loading
            title: Marketplace.loading ? qsTr("Loading the Marketplace…") : qsTr("Marketplace unavailable")
            hint: Marketplace.loading ? "" : qsTr("There is nothing to show right now.")
        }

        Repeater {
            model: root.pages

            delegate: Loader {
                required property var modelData
                anchors.fill: parent
                active: root.visited[modelData.id] === true
                visible: root.activePage === modelData.id
                sourceComponent: MarketplacePage {
                    page: modelData
                    view: root
                    compact: root.compact
                }
            }
        }

        Loader {
            anchors.fill: parent
            active: root.visited["downloads"] === true
            visible: root.activePage === "downloads"
            sourceComponent: MarketDownloadsSection {
                section: ({})
                host: root
            }
        }
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.c.marketplace.detailScrim
        visible: root.compact && opacity > 0
        opacity: root.detailShown ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: Theme.durationSlow; easing.type: Theme.easing } }
        MouseArea {
            anchors.fill: parent
            hoverEnabled: true
            onClicked: root.detailShown = false
        }
    }

    MarketItemDetail {
        width: parent.width
        height: root.compact ? parent.height * 0.85 : parent.height
        x: root.compact ? 0 : (root.detailShown ? 0 : parent.width)
        y: root.compact ? (root.detailShown ? parent.height - height : parent.height) : 0
        visible: root.compact ? y < parent.height : x < parent.width
        radius: root.compact ? Theme.c.marketplace.detailRadius : 0
        compact: root.compact
        shown: root.detailShown
        item: root.detailItem

        Behavior on x { NumberAnimation { duration: Theme.durationSlow; easing.type: Theme.easing } }
        Behavior on y { NumberAnimation { duration: Theme.durationSlow; easing.type: Theme.easing } }

        onBackRequested: root.detailShown = false
        onLinkRequested: (url) => root.openUrl(url)
        onAddToTimelineRequested: (item) => addHelper.request(item, "use")
    }

    ThemedDialog {
        id: urlDialog

        property string url: ""

        title: qsTr("Open link")
        acceptText: qsTr("Open")
        preferredWidth: Theme.dialogWidthSm
        onAccepted: Qt.openUrlExternally(urlDialog.url)

        contentItem: Text {
            width: parent ? parent.width : 320
            wrapMode: Text.WrapAnywhere
            text: qsTr("This opens %1 in your browser. It is not on cutwire.studio.").arg(urlDialog.url)
            color: Theme.c.marketplace.foreground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeSm
        }
    }
}
