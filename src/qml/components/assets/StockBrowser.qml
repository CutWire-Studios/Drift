import QtQuick
import QtQuick.Controls.Basic
import QtMultimedia
import Drift
// Stock footage, photos and audio for the active Market type. The query comes from the
// Market field above; Enter there calls submitQuery(). Sources are a quiet line of names
// rather than a dropdown, led by "All" (every source with no download limit, searched
// together). Results take the shape of the medium: 16:9 tiles for video, justified rows at
// each photo's own aspect for photos, and a list with inline playback for audio.
Item {
    id: root

    property string query: ""
    property bool compact: Theme.compact
    property var filterValues: ({})
    property bool filtersExpanded: false
    property bool lastRequestWasResolve: false
    // What was actually sent, so "No results for X" only speaks for a query that ran.
    property string submittedQuery: ""
    property var detailItem: ({})
    property bool detailShown: false

    readonly property real sidePadding: compact ? Theme.androidPagePadding : Theme.pagePadding
    readonly property int activeFilterCount: Object.keys(filterValues).length
    readonly property string mediaKind: {
        const types = Market.types
        for (let i = 0; i < types.length; ++i) {
            if (types[i].id === Market.activeTypeId)
                return types[i].media_kind || types[i].id
        }
        return ""
    }
    readonly property bool queryIsLink: Market.canResolve && /^(https?:\/\/|www\.)\S+$/i.test(query)
    readonly property var linkSource: {
        const q = query.toLowerCase()
        if (/youtu\.?be/.test(q)) return { label: "YouTube", glyph: Theme.icons.brandYoutube }
        if (q.indexOf("instagram.") >= 0) return { label: "Instagram", glyph: Theme.icons.brandInstagram }
        if (q.indexOf("facebook.") >= 0 || q.indexOf("fb.watch") >= 0) return { label: "Facebook", glyph: Theme.icons.brandFacebook }
        if (q.indexOf("tiktok.") >= 0) return { label: "TikTok", glyph: Theme.icons.brandTiktok }
        if (/(^|\/\/|\.)(x|twitter)\.com/.test(q)) return { label: "X", glyph: Theme.icons.brandX }
        return { label: "", glyph: Theme.icons.linkTwo }
    }

    function runSearch() {
        if (!Market.configured || !Market.activeTypeId || !Market.activeProviderId || !Market.canSearch)
            return
        lastRequestWasResolve = false
        submittedQuery = query
        Market.search(query, filterValues)
    }

    function runResolve() {
        if (!query)
            return
        lastRequestWasResolve = true
        submittedQuery = query
        Market.resolveUrl(query)
    }

    function submitQuery() {
        if (queryIsLink)
            runResolve()
        else if (Market.canSearch)
            runSearch()
    }

    function setFilter(id, value, rerun) {
        const next = Object.assign({}, filterValues)
        if (value === undefined || value === null || value === "" || value === false)
            delete next[id]
        else
            next[id] = value === true ? "1" : String(value)
        filterValues = next
        if (rerun !== false)
            searchDebounce.restart()
    }

    function openDetail(item) {
        detailItem = item
        detailShown = true
    }

    function handleBack() {
        if (detailShown) {
            detailShown = false
            return true
        }
        return false
    }

    // Asks for a folder once, then keeps using it; the detail view offers "Change".
    function download(item) {
        if (!item.id || item.downloadable === false)
            return
        const title = item.title || ""
        const kind = item.media_kind || item.type || ""
        if (!FileDialogs.supportsDirectoryPicker()) {
            Market.download(item.id, "", "", title, kind)
            return
        }
        let dir = Market.lastDownloadDir
        if (!dir || dir.toString() === "") {
            dir = FileDialogs.openDirectory(qsTr("Save downloads to"), "")
            if (!dir || dir.toString() === "")
                return
            Market.lastDownloadDir = dir
        }
        Market.download(item.id, "", dir, title, kind)
    }

    function changeFolder() {
        const dir = FileDialogs.openDirectory(qsTr("Save downloads to"), Market.lastDownloadDir)
        if (dir && dir.toString() !== "")
            Market.lastDownloadDir = dir
    }

    onVisibleChanged: ensureResults()
    Component.onCompleted: ensureResults()
    function ensureResults() {
        if (!visible || !Market.configured)
            return
        if (Market.types.length === 0 && !Market.catalogLoading)
            Market.refreshCatalog()
        else if (Market.canSearch && Market.items.length === 0 && !Market.searching)
            searchDebounce.restart()
    }

    Connections {
        target: Market
        function onActiveProviderIdChanged() {
            root.filterValues = ({})
            root.filtersExpanded = false
            root.detailShown = false
            if (Market.canSearch && Market.items.length === 0)
                searchDebounce.restart()
        }
        function onActiveTypeIdChanged() {
            root.detailShown = false
            audioPlayer.stop()
        }
        function onCatalogChanged() {
            if (root.visible && Market.canSearch && Market.items.length === 0 && !Market.searching)
                searchDebounce.restart()
        }
        // A link resolves to one item; open it straight away rather than showing a grid of one.
        function onItemsChanged() {
            if (root.lastRequestWasResolve && Market.items.length === 1)
                root.openDetail(Market.items[0])
        }
    }

    // Defers the one-shot search that a source switch, filter click or first showing kicks off,
    // so several landing together cost one request.
    Timer {
        id: searchDebounce
        interval: 280
        onTriggered: root.runSearch()
    }

    MediaPlayer {
        id: audioPlayer
        property string itemId: ""
        audioOutput: AudioOutput {}
    }

    Column {
        id: header
        width: parent.width
        spacing: Theme.spacingMd
        topPadding: Theme.spacingSm

        // Sources. "All" first when there is more than one unmetered source.
        Item {
            x: root.sidePadding
            width: parent.width - root.sidePadding * 2
            height: Math.max(sourceFlick.height, filterButton.height)

            ScrollStrip {
                id: sourceFlick
                anchors.left: parent.left
                anchors.right: filterButton.visible ? filterButton.left : parent.right
                anchors.verticalCenter: parent.verticalCenter
                height: sourceRow.height

                Row {
                    id: sourceRow
                    spacing: Theme.spacingXl

                    Repeater {
                        model: (Market.canSearchAll ? [{ id: "*", label: qsTr("All") }] : []).concat(Market.providers)
                        delegate: Text {
                            required property var modelData
                            readonly property bool active: Market.activeProviderId === modelData.id
                            readonly property var q: modelData.quota
                            height: Theme.touchUi ? Theme.androidMinTouchTarget : Theme.controlHeightSm
                            verticalAlignment: Text.AlignVCenter
                            text: q && q.remaining !== undefined
                                  ? qsTr("%1 (%2 left)").arg(modelData.label).arg(q.remaining)
                                  : modelData.label
                            color: active || sourceHover.hovered ? Theme.c.stockBrowser.foreground : Theme.c.stockBrowser.foregroundMuted
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSizeSm
                            font.weight: active ? Font.DemiBold : Font.Normal
                            Accessible.role: Accessible.RadioButton
                            Accessible.checked: active
                            Accessible.name: text
                            HoverHandler { id: sourceHover; cursorShape: Qt.PointingHandCursor }
                            TapHandler { onTapped: Market.activeProviderId = modelData.id }
                        }
                    }
                }
            }

            IconButton {
                id: filterButton
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                visible: Market.filters.length > 0 && Market.canSearch
                glyph: Theme.icons.sliders
                active: root.filtersExpanded
                tooltip: root.activeFilterCount > 0
                         ? qsTr("Filters — %n applied", "", root.activeFilterCount)
                         : qsTr("Filters")
                onClicked: root.filtersExpanded = !root.filtersExpanded

                // The row collapses, so an applied filter needs a mark that it is narrowing results.
                Rectangle {
                    visible: root.activeFilterCount > 0
                    anchors.right: parent.right
                    anchors.top: parent.top
                    width: Theme.spacingXl
                    height: width
                    radius: Theme.c.stockBrowser.badgeRadius
                    color: Theme.c.stockBrowser.accent
                    Text {
                        anchors.centerIn: parent
                        text: String(root.activeFilterCount)
                        color: Theme.c.stockBrowser.accentForeground
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSizeXs
                    }
                }
            }
        }

        Flow {
            x: root.sidePadding
            width: parent.width - root.sidePadding * 2
            spacing: Theme.spacingSm
            visible: Market.filters.length > 0 && Market.canSearch && root.filtersExpanded

            Repeater {
                model: Market.filters
                delegate: Item {
                    id: filterRoot
                    required property var modelData
                    width: filterLoader.width
                    height: filterLoader.height

                    Loader {
                        id: filterLoader
                        sourceComponent: filterRoot.modelData.type === "toggle" ? toggleFilter
                                         : (filterRoot.modelData.type === "text" ? textFilter : enumFilter)
                    }
                    Component {
                        id: enumFilter
                        ThemedComboBox {
                            readonly property var filter: filterRoot.modelData
                            textRole: "label"
                            valueRole: "id"
                            model: [{ id: "", label: filter.label }].concat(filter.options || [])
                            onActivated: {
                                if (currentIndex >= 0 && currentIndex < model.length)
                                    root.setFilter(filter.id, model[currentIndex].id)
                            }
                        }
                    }
                    Component {
                        id: toggleFilter
                        ThemedChip {
                            readonly property var filter: filterRoot.modelData
                            text: filter.label
                            selected: !!root.filterValues[filter.id]
                            variant: "outline"
                            onClicked: root.setFilter(filter.id, !selected)
                        }
                    }
                    Component {
                        id: textFilter
                        ThemedTextField {
                            readonly property var filter: filterRoot.modelData
                            width: 140
                            placeholderText: filter.label
                            onTextChanged: root.setFilter(filter.id, text.trim(), false)
                        }
                    }
                }
            }
        }

        // A pasted link becomes this card instead of a second button next to the field.
        Rectangle {
            x: root.sidePadding
            width: parent.width - root.sidePadding * 2
            height: Theme.touchUi ? Theme.androidMinTouchTarget + Theme.spacingLg : Theme.controlHeight * 1.6
            radius: Theme.c.stockBrowser.linkRadius
            color: linkHover.hovered ? Theme.c.stockBrowser.linkBackgroundHover : Theme.c.stockBrowser.linkBackground
            visible: root.queryIsLink && !(root.lastRequestWasResolve && root.submittedQuery === root.query
                                           && (Market.searching || Market.items.length > 0))

            IconGlyph {
                id: linkGlyph
                anchors.left: parent.left
                anchors.leftMargin: Theme.spacingXl
                anchors.verticalCenter: parent.verticalCenter
                glyph: root.linkSource.glyph
                iconSize: Theme.iconSizeLg
                iconColor: Theme.c.stockBrowser.foreground
            }
            Column {
                anchors.left: linkGlyph.right
                anchors.right: parent.right
                anchors.leftMargin: Theme.spacingLg
                anchors.rightMargin: Theme.spacingXl
                anchors.verticalCenter: parent.verticalCenter
                spacing: 2
                Text {
                    width: parent.width
                    text: root.linkSource.label.length > 0 ? qsTr("Get from %1").arg(root.linkSource.label)
                                                           : qsTr("Get from this link")
                    color: Theme.c.stockBrowser.foreground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeSm
                    font.weight: Font.Medium
                    elide: Text.ElideRight
                }
                Text {
                    width: parent.width
                    text: root.query
                    color: Theme.c.stockBrowser.foregroundMuted
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeXs
                    elide: Text.ElideMiddle
                }
            }
            HoverHandler { id: linkHover; cursorShape: Qt.PointingHandCursor }
            TapHandler { onTapped: root.runResolve() }
        }
    }

    Item {
        id: results
        anchors.top: header.bottom
        anchors.topMargin: Theme.spacingMd
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom

        EmptyState {
            anchors.centerIn: parent
            width: parent.width
            visible: Market.searching && Market.items.length === 0 && root.lastRequestWasResolve
            glyph: Theme.icons.spinner
            glyphSpinning: true
            compact: true
            title: qsTr("Looking up that link…")
        }

        Grid {
            anchors.fill: parent
            anchors.leftMargin: root.sidePadding
            visible: Market.searching && Market.items.length === 0 && !root.lastRequestWasResolve
            columns: Math.max(1, Math.floor((width + Theme.assetCardGap) / (Theme.assetCardWidth + Theme.assetCardGap)))
            spacing: Theme.assetCardGap
            Repeater {
                model: 9
                delegate: SkeletonBox {
                    width: Theme.assetCardWidth
                    height: root.mediaKind === "audio" ? Theme.controlHeight : Theme.assetCardWidth * 9 / 16
                    animated: parent.visible
                }
            }
        }

        EmptyState {
            anchors.centerIn: parent
            width: parent.width
            visible: !Market.searching && Market.searchError.length > 0 && Market.items.length === 0
            glyph: Theme.icons.error
            compact: true
            title: root.lastRequestWasResolve ? qsTr("Couldn’t open that link") : qsTr("Search failed")
            hint: Market.searchError
            actionText: Market.searchErrorRetryable ? qsTr("Try again") : ""
            onActionTriggered: root.lastRequestWasResolve ? root.runResolve() : root.runSearch()
        }

        EmptyState {
            anchors.centerIn: parent
            width: parent.width
            visible: !Market.searching && Market.searchError.length === 0 && Market.items.length === 0
                     && !root.queryIsLink
            glyph: Market.canSearch ? Theme.icons.search : Theme.icons.linkTwo
            compact: true
            title: root.submittedQuery.length > 0
                   ? qsTr("No results for “%1”").arg(root.submittedQuery)
                   : (Market.canSearch ? qsTr("Search this source") : qsTr("Paste a link"))
            hint: root.submittedQuery.length > 0
                  ? qsTr("Try different words, another source, or clear a filter.")
                  : (Market.canSearch ? qsTr("Type above and press Enter.") : qsTr("Paste a page link above and press Enter."))
        }

        // Video and green screen: 16:9 tiles.
        GridView {
            id: grid
            readonly property int columnCount: Math.max(1, Math.round(width / (Theme.assetCardWidth * 1.3 + Theme.assetCardGap)))
            readonly property real tileWidth: Math.floor(width / columnCount) - Theme.assetCardGap
            anchors.fill: parent
            anchors.leftMargin: root.sidePadding
            anchors.rightMargin: root.sidePadding - Theme.assetCardGap
            visible: Market.items.length > 0 && root.mediaKind !== "audio" && root.mediaKind !== "image"
            cellWidth: Math.floor(width / columnCount)
            cellHeight: tileWidth * 9 / 16 + Theme.fontSizeXs * 2.6 + Theme.assetCardGap
            cacheBuffer: Math.max(0, Math.round(height))
            clip: true
            model: visible ? Market.items : []
            ScrollBar.vertical: AppScrollBar { }
            activeFocusOnTab: true
            keyNavigationEnabled: true
            Keys.onReturnPressed: if (currentIndex >= 0) root.openDetail(Market.items[currentIndex])
            onAtYEndChanged: if (atYEnd && Market.hasMore && !Market.searching) Market.loadMore()

            delegate: Column {
                required property var modelData
                required property int index
                width: grid.tileWidth
                spacing: Theme.spacingXs

                StockThumb {
                    width: parent.width
                    height: width * 9 / 16
                    item: modelData
                    compact: root.compact
                    current: grid.activeFocus && index === grid.currentIndex
                    onOpenRequested: {
                        grid.currentIndex = index
                        root.openDetail(modelData)
                    }
                }
                Text {
                    width: parent.width
                    text: modelData.title || ""
                    color: Theme.c.stockBrowser.foreground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeXs
                    elide: Text.ElideRight
                    maximumLineCount: 2
                    wrapMode: Text.WordWrap
                }
            }
        }

        // Photos: justified rows at each photo's own aspect, so a portrait shot is not cropped
        // to a landscape tile.
        ListView {
            id: justified
            readonly property real targetHeight: root.compact ? 120 : 96
            readonly property real gap: Theme.spacingSm
            readonly property var rows: {
                const items = Market.items
                const width = justified.width
                if (!visible || width <= 0)
                    return []
                const out = []
                let row = []
                let sum = 0
                for (let i = 0; i < items.length; ++i) {
                    const w = Number(items[i].width || 0), h = Number(items[i].height || 0)
                    const ar = w > 0 && h > 0 ? Math.min(3, Math.max(0.5, w / h)) : 1.5
                    row.push({ index: i, ar: ar })
                    sum += ar
                    if (sum * targetHeight + gap * (row.length - 1) >= width) {
                        const hh = (width - gap * (row.length - 1)) / sum
                        out.push({ h: hh, cells: row.map(c => ({ index: c.index, w: c.ar * hh })) })
                        row = []
                        sum = 0
                    }
                }
                // The last row keeps its natural size rather than being stretched to fill.
                if (row.length > 0)
                    out.push({ h: targetHeight, cells: row.map(c => ({ index: c.index, w: c.ar * targetHeight })) })
                return out
            }
            anchors.fill: parent
            anchors.leftMargin: root.sidePadding
            anchors.rightMargin: root.sidePadding
            visible: Market.items.length > 0 && root.mediaKind === "image"
            model: rows
            spacing: gap
            clip: true
            cacheBuffer: Math.max(0, Math.round(height))
            ScrollBar.vertical: AppScrollBar { }
            onAtYEndChanged: if (atYEnd && Market.hasMore && !Market.searching) Market.loadMore()

            delegate: Row {
                required property var modelData
                spacing: justified.gap
                height: modelData.h

                Repeater {
                    model: modelData.cells
                    delegate: StockThumb {
                        required property var modelData
                        width: Math.floor(modelData.w)
                        height: parent.height
                        item: Market.items[modelData.index] || ({})
                        compact: root.compact
                        onOpenRequested: root.openDetail(item)
                    }
                }
            }
        }

        // Audio: rows with inline playback. A tile of a waveform-less thumbnail says nothing
        // about a sound.
        ListView {
            id: audioList
            anchors.fill: parent
            anchors.leftMargin: root.sidePadding
            anchors.rightMargin: root.sidePadding
            visible: Market.items.length > 0 && root.mediaKind === "audio"
            model: visible ? Market.items : []
            clip: true
            ScrollBar.vertical: AppScrollBar { }
            onAtYEndChanged: if (atYEnd && Market.hasMore && !Market.searching) Market.loadMore()

            delegate: StockAudioRow {
                previewPlayer: audioPlayer
                onOpenRequested: (item) => root.openDetail(item)
            }
        }
    }

    // --- Detail -------------------------------------------------------------------------------

    Rectangle {
        anchors.fill: parent
        color: Theme.c.stockBrowser.scrim
        visible: root.compact && opacity > 0
        opacity: root.detailShown ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: Theme.durationSlow; easing.type: Theme.easing } }
        // A MouseArea rather than a TapHandler, so the tap stops here instead of also landing
        // on the card behind the scrim.
        MouseArea {
            anchors.fill: parent
            hoverEnabled: true
            onClicked: root.detailShown = false
        }
    }

    StockItemDetail {
        width: parent.width
        height: root.compact ? parent.height * 0.85 : parent.height
        x: root.compact ? 0 : (root.detailShown ? 0 : parent.width)
        y: root.compact ? (root.detailShown ? parent.height - height : parent.height) : 0
        visible: root.compact ? y < parent.height : x < parent.width
        radius: root.compact ? Theme.c.stockBrowser.sheetRadius : 0
        compact: root.compact
        shown: root.detailShown
        item: root.detailItem

        Behavior on x { NumberAnimation { duration: Theme.durationSlow; easing.type: Theme.easing } }
        Behavior on y { NumberAnimation { duration: Theme.durationSlow; easing.type: Theme.easing } }

        onBackRequested: root.detailShown = false
        onDownloadRequested: root.download(root.detailItem)
        onChangeFolderRequested: root.changeFolder()
    }
}
