import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtMultimedia
import Drift
import ".."

// Catalog-driven stock browser. Types and providers come from the marketplace API;
// this tab has no store-specific adapters. Downloaded files land in the media bin.
Item {
    id: root

    property var filterValues: ({})
    property bool filtersExpanded: false
    property string previewId: ""
    readonly property var previewItem: {
        void Market.items
        return previewId.length > 0 ? Market.itemById(previewId) : ({})
    }
    readonly property string query: search.text.trim()
    readonly property int activeFilterCount: Object.keys(root.filterValues).length

    // Only a provider that can resolve treats a pasted link as a link; for a search-only
    // source the same text is just an unusual query and must not disable searching.
    readonly property bool queryIsLink: Market.canResolve
                                        && /^(https?:\/\/|www\.)\S+$/i.test(root.query)

    function formatDuration(ms) {
        const n = Number(ms)
        if (!n || n <= 0)
            return ""
        const s = Math.round(n / 1000)
        const m = Math.floor(s / 60)
        const r = s % 60
        return m + ":" + (r < 10 ? "0" : "") + r
    }

    function kindGlyph(item) {
        const kind = item.media_kind || item.type || ""
        if (kind === "audio")
            return Theme.icons.music
        if (kind === "photo" || kind === "image")
            return Theme.icons.image
        return Theme.icons.film
    }

    function runSearch() {
        if (!Market.configured)
            return
        if (!Market.activeTypeId || !Market.activeProviderId)
            return
        if (!Market.canSearch)
            return
        Market.search(root.query, root.filterValues)
    }

    function runResolve() {
        const url = root.query
        if (!url)
            return
        Market.resolveUrl(url)
    }

    // Enter in the one query field goes wherever the text points: a link to look-up,
    // anything else to search.
    function submitQuery() {
        if (root.queryIsLink)
            root.runResolve()
        else if (Market.canSearch)
            root.runSearch()
    }

    function setFilter(id, value) {
        const next = Object.assign({}, root.filterValues)
        if (value === undefined || value === null || value === "" || value === false)
            delete next[id]
        else
            next[id] = value === true ? "1" : String(value)
        root.filterValues = next
        searchDebounce.restart()
    }

    function openPreview(itemId) {
        root.previewId = itemId
        previewDialog.open()
    }

    onVisibleChanged: {
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
            search.text = ""
            if (Market.canSearch && Market.items.length === 0)
                searchDebounce.restart()
        }
        function onCatalogChanged() {
            if (root.visible && Market.canSearch && Market.items.length === 0 && !Market.searching)
                searchDebounce.restart()
        }
    }

    Timer {
        id: searchDebounce
        interval: 280
        onTriggered: root.runSearch()
    }

    EmptyState {
        anchors.centerIn: parent
        width: parent.width
        visible: !Market.configured
        glyph: Theme.icons.store
        title: qsTr("Marketplace unavailable")
        hint: qsTr("This build does not include the marketplace.")
    }

    EmptyState {
        anchors.centerIn: parent
        width: parent.width
        visible: Market.configured && Market.catalogLoading && Market.types.length === 0
        glyph: Theme.icons.spinner
        glyphSpinning: true
        title: qsTr("Loading marketplace…")
        hint: qsTr("Fetching available sources.")
    }

    EmptyState {
        anchors.centerIn: parent
        width: parent.width
        visible: Market.configured && !Market.catalogLoading && Market.catalogError.length > 0
                 && Market.types.length === 0
        glyph: Theme.icons.error
        title: qsTr("Couldn’t reach the marketplace")
        hint: Market.catalogError
        actionText: qsTr("Try again")
        onActionTriggered: Market.refreshCatalog()
    }

    EmptyState {
        anchors.centerIn: parent
        width: parent.width
        visible: Market.configured && !Market.catalogLoading && Market.catalogError.length === 0
                 && Market.types.length === 0
        glyph: Theme.icons.store
        title: qsTr("Nothing listed")
        hint: qsTr("No sources are available right now.")
        actionText: qsTr("Refresh")
        onActionTriggered: Market.refreshCatalog()
    }

    ColumnLayout {
        id: browser
        visible: Market.configured && Market.types.length > 0
        anchors.fill: parent
        spacing: Theme.spacingSm

        // Chips rather than a dropdown: there are only a handful of types and the active one
        // has to stay visible, which a collapsed combo cannot do.
        Flickable {
            id: typeFlick
            Layout.fillWidth: true
            Layout.topMargin: Theme.spacingSm
            Layout.preferredHeight: typeRow.height
            contentWidth: typeRow.width + Theme.pagePadding * 2
            flickableDirection: Flickable.HorizontalFlick
            clip: true
            boundsBehavior: Flickable.StopAtBounds

            Row {
                id: typeRow
                x: Theme.pagePadding
                spacing: Theme.spacingSm

                Repeater {
                    model: Market.types
                    delegate: ThemedChip {
                        required property var modelData
                        text: modelData.label || modelData.id
                        selected: Market.activeTypeId === modelData.id
                        variant: "outline"
                        onClicked: Market.activeTypeId = modelData.id
                    }
                }
            }
        }

        // Source and the filter disclosure lead; the quota is trailing status rather than a
        // control, so it sits at the far edge instead of between the two things you click.
        RowLayout {
            id: providerRow
            Layout.fillWidth: true
            Layout.leftMargin: Theme.pagePadding
            Layout.rightMargin: Theme.pagePadding
            spacing: Theme.spacingSm

            ThemedComboBox {
                id: providerCombo
                Layout.preferredWidth: Math.max(widestContentWidth, 120)
                Layout.maximumWidth: Math.max(120, browser.width * 0.55)
                textRole: "label"
                valueRole: "id"
                model: Market.providers
                tooltip: qsTr("Source")
                currentIndex: {
                    const list = Market.providers
                    for (let i = 0; i < list.length; ++i) {
                        if (list[i].id === Market.activeProviderId)
                            return i
                    }
                    return 0
                }
                onActivated: {
                    const list = Market.providers
                    if (currentIndex >= 0 && currentIndex < list.length)
                        Market.activeProviderId = list[currentIndex].id
                }
            }

            IconButton {
                id: filterButton
                visible: Market.filters.length > 0 && Market.canSearch
                glyph: Theme.icons.sliders
                active: root.filtersExpanded
                tooltip: root.activeFilterCount > 0
                         ? qsTr("Filters — %n applied", "", root.activeFilterCount)
                         : qsTr("Filters")
                onClicked: root.filtersExpanded = !root.filtersExpanded

                // The filter row collapses, so an applied filter would otherwise be invisible
                // and quietly narrow every later search with nothing on screen to explain it.
                Rectangle {
                    visible: root.activeFilterCount > 0
                    anchors.right: parent.right
                    anchors.top: parent.top
                    width: Theme.spacingXl
                    height: width
                    radius: width / 2
                    color: Theme.primary

                    Text {
                        anchors.centerIn: parent
                        text: String(root.activeFilterCount)
                        color: Theme.primaryForeground
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSizeXs
                    }
                }
            }

            Item { Layout.fillWidth: true }

            ThemedLabel {
                id: quotaLabel
                horizontalAlignment: Text.AlignRight
                visible: {
                    const q = Market.quota
                    return q && q.limit !== undefined && q.limit !== null && Number(q.limit) >= 0
                           && q.remaining !== undefined
                }
                text: {
                    const q = Market.quota
                    if (!q || q.remaining === undefined)
                        return ""
                    const n = Number(q.remaining)
                    return qsTr("%n remaining today", "", n)
                }
            }
        }

        // One input for both capabilities. A provider like YouTube advertises search *and*
        // resolve, which rendered two stacked full-width fields that read as duplicates;
        // recognising a pasted link switches this row into look-up mode instead.
        RowLayout {
            id: queryRow
            Layout.fillWidth: true
            Layout.leftMargin: Theme.pagePadding
            Layout.rightMargin: Theme.pagePadding
            spacing: Theme.spacingSm
            visible: Market.canSearch || Market.canResolve

            ThemedTextField {
                id: search
                Layout.fillWidth: true
                placeholderText: Market.canSearch && Market.canResolve
                                 ? qsTr("Search or paste a link")
                                 : (Market.canSearch ? qsTr("Search") : qsTr("Paste a link"))
                font.family: Theme.fontFamily
                // A half-typed URL is not a search term: hold the debounce and wait for
                // Look up rather than firing a query at every keystroke of a paste.
                onTextChanged: {
                    if (root.queryIsLink || !Market.canSearch)
                        return
                    searchDebounce.restart()
                }
                Keys.onReturnPressed: root.submitQuery()
                Keys.onEnterPressed: root.submitQuery()
            }

            ThemedButton {
                id: resolveButton
                text: qsTr("Look up")
                variant: "secondary"
                visible: root.queryIsLink
                enabled: !Market.searching
                onClicked: root.runResolve()
            }
        }

        // Collapsed by default: filters are per-provider and most searches never touch them,
        // so they no longer push results below the fold on every source.
        Flow {
            id: filterFlow
            Layout.fillWidth: true
            Layout.leftMargin: Theme.pagePadding
            Layout.rightMargin: Theme.pagePadding
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
                        sourceComponent: {
                            const t = filterRoot.modelData.type
                            if (t === "toggle")
                                return toggleFilter
                            if (t === "text")
                                return textFilter
                            return enumFilter
                        }
                    }

                    Component {
                        id: enumFilter
                        ThemedComboBox {
                            readonly property var filter: filterRoot.modelData
                            textRole: "label"
                            valueRole: "id"
                            model: [{ id: "", label: filter.label }].concat(filter.options || [])
                            onActivated: {
                                const opts = model
                                if (currentIndex >= 0 && currentIndex < opts.length)
                                    root.setFilter(filter.id, opts[currentIndex].id)
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
                            onTextChanged: root.setFilter(filter.id, text.trim())
                        }
                    }
                }
            }
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            // A link resolves to a single item, so tiles would promise a grid that never
            // arrives; that path keeps a labelled spinner.
            EmptyState {
                anchors.centerIn: parent
                width: parent.width
                visible: Market.searching && Market.items.length === 0 && root.queryIsLink
                glyph: Theme.icons.spinner
                glyphSpinning: true
                compact: true
                title: qsTr("Looking up that link…")
            }

            Grid {
                id: searchSkeleton
                anchors.fill: parent
                anchors.margins: Theme.pagePadding
                anchors.topMargin: Theme.spacingMd
                visible: Market.searching && Market.items.length === 0 && !root.queryIsLink
                columns: Math.max(1, Math.floor((width + Theme.assetCardGap)
                                                / (Theme.assetCardWidth + Theme.assetCardGap)))
                spacing: Theme.assetCardGap

                Repeater {
                    model: 9
                    delegate: SkeletonBox {
                        width: Theme.assetCardWidth
                        height: Theme.assetCardWidth * 9 / 16
                        animated: searchSkeleton.visible
                    }
                }
            }

            EmptyState {
                anchors.centerIn: parent
                width: parent.width
                visible: !Market.searching && Market.searchError.length > 0 && Market.items.length === 0
                glyph: Theme.icons.error
                compact: true
                title: Market.canResolve && !Market.canSearch
                       ? qsTr("Couldn’t open that link")
                       : qsTr("Search failed")
                hint: Market.searchError
                actionText: Market.canResolve && !Market.canSearch ? qsTr("Look up") : qsTr("Try again")
                onActionTriggered: Market.canResolve && !Market.canSearch
                                   ? root.runResolve()
                                   : root.runSearch()
            }

            EmptyState {
                anchors.centerIn: parent
                width: parent.width
                visible: !Market.searching && Market.searchError.length === 0 && Market.items.length === 0
                glyph: Market.canResolve && !Market.canSearch ? Theme.icons.linkTwo : Theme.icons.search
                compact: true
                title: {
                    if (Market.canResolve && !Market.canSearch)
                        return qsTr("Paste a link")
                    if (root.query.length > 0)
                        return qsTr("No results for “%1”").arg(root.query)
                    return qsTr("Search this source")
                }
                hint: Market.canResolve && !Market.canSearch
                      ? qsTr("Paste a page link from this source, then look it up.")
                      : qsTr("Pick a source, then search for photos, video or audio.")
            }

            GridView {
                id: grid
                anchors.fill: parent
                anchors.margins: Theme.pagePadding
                anchors.topMargin: Theme.spacingMd
                anchors.rightMargin: Theme.pagePadding - Theme.assetCardGap
                visible: Market.items.length > 0
                cellWidth: Theme.assetCardWidth + Theme.assetCardGap
                cellHeight: (Theme.assetCardWidth * 9 / 16) + Theme.spacing3xl + Theme.assetCardGap
                clip: true
                model: Market.items
                ScrollBar.vertical: AppScrollBar { }
                activeFocusOnTab: true
                keyNavigationEnabled: true
                highlightMoveDuration: Theme.durationFast
                Keys.onReturnPressed: {
                    if (currentIndex >= 0 && currentIndex < Market.items.length)
                        root.openPreview(Market.items[currentIndex].id)
                }

                // Reaching the end used to show nothing at all while the next page loaded,
                // which reads as "that is everything" rather than "still coming".
                footer: Item {
                    width: grid.width
                    height: Market.searching && Market.items.length > 0
                            ? Theme.spacing3xl + Theme.spacing2xl : 0
                    visible: height > 0

                    IconGlyph {
                        id: moreSpinner
                        anchors.centerIn: parent
                        glyph: Theme.icons.spinner
                        iconSize: Theme.iconSizeBase
                        iconColor: Theme.mutedForeground
                        RotationAnimator on rotation {
                            running: moreSpinner.visible
                            loops: Animation.Infinite
                            from: 0
                            to: 360
                            duration: 900
                        }
                    }
                }

                onAtYEndChanged: {
                    if (atYEnd && Market.hasMore && !Market.searching)
                        Market.loadMore()
                }

                delegate: Column {
                    id: card
                    required property var modelData
                    required property int index
                    width: Theme.assetCardWidth
                    spacing: 4

                    readonly property var job: {
                        void Market.downloadsRevision
                        return Market.downloadInfo(modelData.id)
                    }
                    readonly property bool busy: job.status === "queued"
                                                 || job.status === "processing"
                                                 || job.status === "downloading"
                                                 || job.status === "importing"
                    readonly property int price: Number(modelData.price_coins || 0)

                    Rectangle {
                        id: cardThumb
                        width: Theme.assetCardWidth
                        height: Theme.assetCardWidth * 9 / 16
                        radius: Theme.radiusSm
                        color: Theme.panelAccent
                        clip: true

                        readonly property bool focusRingVisible: grid.activeFocus
                                                                 && card.GridView.isCurrentItem
                        border.width: focusRingVisible ? Theme.borderWidthFocus
                                      : (cardHover.hovered ? Theme.borderWidth : 0)
                        border.color: focusRingVisible ? Theme.focusRing : Theme.primary
                        scale: cardTap.pressed ? Theme.pressScale
                               : (cardHover.hovered ? 1.03 : 1.0)

                        Behavior on scale {
                            NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
                        }
                        Behavior on border.width {
                            NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
                        }

                        HoverHandler {
                            id: cardHover
                            cursorShape: Qt.PointingHandCursor
                        }

                        SkeletonBox {
                            anchors.fill: parent
                            visible: thumb.status === Image.Loading
                        }

                        IconGlyph {
                            anchors.centerIn: parent
                            glyph: root.kindGlyph(modelData)
                            iconSize: Theme.iconSizeXl
                            iconColor: Theme.mutedForeground
                            visible: !thumb.status || thumb.status === Image.Null
                                     || thumb.status === Image.Error
                        }

                        Image {
                            id: thumb
                            anchors.fill: parent
                            source: modelData.thumb_url || ""
                            fillMode: Image.PreserveAspectCrop
                            asynchronous: true
                            cache: true
                            opacity: status === Image.Ready ? 1 : 0

                            Behavior on opacity {
                                NumberAnimation { duration: Theme.durationBase; easing.type: Theme.easing }
                            }
                        }

                        Rectangle {
                            visible: card.price > 0
                            anchors.left: parent.left
                            anchors.top: parent.top
                            anchors.margins: Theme.spacingSm
                            color: Theme.scrimStrong
                            radius: Theme.radiusXs
                            width: coinLabel.implicitWidth + Theme.spacingLg
                            height: coinLabel.implicitHeight + Theme.spacingSm
                            Text {
                                id: coinLabel
                                anchors.centerIn: parent
                                text: String(card.price)
                                color: Theme.onMedia
                                font.pixelSize: Theme.fontSizeXs
                                font.family: Theme.fontFamily
                            }
                        }

                        Rectangle {
                            visible: root.formatDuration(modelData.duration_ms).length > 0
                            anchors.right: parent.right
                            anchors.bottom: parent.bottom
                            anchors.margins: Theme.spacingSm
                            color: Theme.scrimStrong
                            radius: Theme.radiusXs
                            width: durLabel.implicitWidth + Theme.spacingLg
                            height: durLabel.implicitHeight + Theme.spacingSm
                            Text {
                                id: durLabel
                                anchors.centerIn: parent
                                text: root.formatDuration(modelData.duration_ms)
                                color: Theme.onMedia
                                font.pixelSize: Theme.fontSizeXs
                                font.family: Theme.fontFamily
                            }
                        }

                        Rectangle {
                            visible: card.busy
                            anchors.fill: parent
                            color: Theme.scrimStrong
                            CircularProgress {
                                anchors.centerIn: parent
                                value: Number(card.job.progress || 0)
                                indeterminate: Number(card.job.progress || 0) <= 0
                                size: Theme.spacing3xl
                                progressColor: Theme.onMedia
                            }
                        }

                        TapHandler {
                            id: cardTap
                            // Move the keyboard cursor to what was clicked, so tabbing back
                            // into the grid resumes where the pointer left off.
                            onTapped: {
                                grid.currentIndex = card.index
                                root.openPreview(card.modelData.id)
                            }
                        }
                    }

                    Text {
                        width: parent.width
                        text: modelData.title || ""
                        color: Theme.panelForeground
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSizeXs
                        elide: Text.ElideRight
                        maximumLineCount: 2
                        wrapMode: Text.WordWrap
                    }
                }
            }
        }
    }

    ThemedDialog {
        id: previewDialog
        title: root.previewItem.title || qsTr("Preview")
        preferredWidth: Theme.dialogWidthLg
        acceptText: {
            const job = Market.downloadInfo(root.previewId)
            if (job.status === "queued" || job.status === "processing"
                    || job.status === "downloading" || job.status === "importing")
                return qsTr("Working…")
            const price = Number(root.previewItem.price_coins || 0)
            if (price > 0)
                return qsTr("Download · %1").arg(price)
            return qsTr("Download")
        }
        acceptVariant: "primary"
        rejectText: qsTr("Close")

        readonly property bool previewIsImage: {
            const item = root.previewItem
            const kind = item.media_kind || item.type || ""
            return kind === "photo" || kind === "image"
        }

        contentItem: Column {
            width: parent ? parent.width : Theme.dialogWidthLg
            spacing: Theme.spacingLg

            Rectangle {
                width: parent.width
                height: Math.min(220, width * 9 / 16)
                radius: Theme.radiusSm
                color: Theme.panelAccent
                clip: true

                Image {
                    anchors.fill: parent
                    visible: previewDialog.previewIsImage || !(previewPlayer.source && previewPlayer.source != "")
                    source: root.previewItem.preview_url || root.previewItem.thumb_url || ""
                    fillMode: Image.PreserveAspectFit
                    asynchronous: true
                }

                VideoOutput {
                    id: previewOut
                    anchors.fill: parent
                    visible: !previewDialog.previewIsImage
                    fillMode: VideoOutput.PreserveAspectFit
                }

                MediaPlayer {
                    id: previewPlayer
                    audioOutput: AudioOutput {}
                    videoOutput: previewOut
                    source: previewDialog.previewIsImage ? "" : (root.previewItem.preview_url || "")
                    autoPlay: previewDialog.visible && !previewDialog.previewIsImage
                               && source.toString().length > 0
                }
            }

            ThemedLabel {
                width: parent.width
                visible: !!((root.previewItem.creator && root.previewItem.creator.name)
                            || (root.previewItem.license && root.previewItem.license.attribution))
                text: {
                    const item = root.previewItem
                    const bits = []
                    if (item.creator && item.creator.name)
                        bits.push(item.creator.name)
                    if (item.license && item.license.attribution)
                        bits.push(item.license.attribution)
                    else if (item.license && item.license.name)
                        bits.push(item.license.name)
                    return bits.join(" · ")
                }
            }

            ThemedLabel {
                width: parent.width
                visible: Number(root.previewItem.price_coins || 0) > 0
                text: qsTr("%n coin(s)", "", Number(root.previewItem.price_coins || 0))
                color: Theme.panelForeground
            }
        }

        onOpened: {
            if (!previewDialog.previewIsImage && previewPlayer.source.toString().length > 0)
                previewPlayer.play()
        }
        onClosed: {
            previewPlayer.stop()
            previewPlayer.source = ""
            root.previewId = ""
        }
        onAccepted: {
            if (root.previewId.length === 0)
                return
            if (root.previewItem.downloadable === false)
                return
            Market.download(root.previewId)
        }
    }
}
