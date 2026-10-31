import QtQuick
import QtQuick.Controls.Basic
import QtMultimedia
import Drift
// Stock footage, photos or audio for one stock.<type> source. Search runs on Enter. Providers
// are a quiet line of names led by "All sources" (the server fans the query out). A pasted
// link is resolved to one item instead of searched. Results take the shape of the medium: 16:9
// tiles for video, justified rows at each photo's own aspect, and a list with inline playback
// for audio and music.
Item {
    id: root

    required property var section
    required property var host

    readonly property var source: root.section.source || ({})
    readonly property var info: Marketplace.source(root.source.id || "")
    readonly property var providers: root.info.providers || []
    readonly property string mediaKind: root.info.mediaKind || ""
    readonly property bool isAudio: mediaKind === "audio" || mediaKind === "music"
    readonly property bool isPhoto: mediaKind === "image"
    readonly property bool canResolve: !!root.info.canResolve && Marketplace.feature("resolve", true) !== false
    readonly property real sidePadding: root.host.sidePadding
    readonly property bool compact: root.host.compact

    property string providerId: root.source.provider
                                || (root.providers.length > 1 ? "*" : (root.providers.length > 0 ? root.providers[0].id : ""))
    property var filterValues: ({})
    property bool filtersExpanded: false
    property string submittedQuery: ""
    property bool resolveRequested: false

    readonly property string typedQuery: searchField.text.trim()
    readonly property bool queryIsLink: root.canResolve && /^(https?:\/\/|www\.)\S+$/i.test(typedQuery)
    readonly property var providerInfo: {
        for (let i = 0; i < root.providers.length; ++i) {
            if (root.providers[i].id === root.providerId)
                return root.providers[i]
        }
        return null
    }
    readonly property var filters: root.providerInfo ? (root.providerInfo.filters || []) : []
    readonly property int activeFilterCount: Object.keys(root.filterValues).length
    readonly property var linkSource: {
        const q = typedQuery.toLowerCase()
        if (/youtu\.?be/.test(q)) return { label: "YouTube", glyph: Theme.icons.brandYoutube }
        if (q.indexOf("instagram.") >= 0) return { label: "Instagram", glyph: Theme.icons.brandInstagram }
        if (q.indexOf("facebook.") >= 0 || q.indexOf("fb.watch") >= 0) return { label: "Facebook", glyph: Theme.icons.brandFacebook }
        if (q.indexOf("tiktok.") >= 0) return { label: "TikTok", glyph: Theme.icons.brandTiktok }
        if (/(^|\/\/|\.)(x|twitter)\.com/.test(q)) return { label: "X", glyph: Theme.icons.brandX }
        return { label: "", glyph: Theme.icons.linkTwo }
    }
    readonly property string warningText: {
        const names = []
        for (const warning of browser.warnings) {
            const id = String(warning).split(":")[0]
            let label = id
            for (let i = 0; i < root.providers.length; ++i) {
                if (root.providers[i].id === id)
                    label = root.providers[i].label
            }
            names.push(label)
        }
        return names.length > 0 ? qsTr("%1 didn’t respond.").arg(names.join(", ")) : ""
    }
    readonly property string quotaText: {
        const q = browser.quota
        const remaining = q["remaining"]
        if (remaining === undefined || remaining === null)
            return ""
        return q["window"] === "day" ? qsTr("%n download(s) left today", "", remaining)
                                     : qsTr("%n download(s) left", "", remaining)
    }

    function submit() {
        if (root.queryIsLink) {
            root.resolveRequested = true
            StockInstaller.resolveUrl(root.typedQuery)
        } else {
            root.resolveRequested = false
            root.submittedQuery = root.typedQuery
        }
    }

    function selectProvider(id) {
        root.providerId = id
        root.filterValues = ({})
        root.filtersExpanded = false
    }

    function setFilter(id, value) {
        const next = Object.assign({}, root.filterValues)
        if (value === undefined || value === null || value === "" || value === false)
            delete next[id]
        else
            next[id] = value === true ? "1" : String(value)
        root.filterValues = next
    }

    onVisibleChanged: if (!visible) audioPlayer.stop()

    MarketSourceBrowser {
        id: browser
        source: root.source.id || ""
        provider: root.providerId
        query: root.submittedQuery
        filters: root.filterValues
        limit: 30
    }

    Connections {
        target: StockInstaller
        // A link resolves to one item; open it straight away rather than showing a grid of one.
        function onResolveChanged() {
            if (!root.resolveRequested || StockInstaller.resolving)
                return
            if (StockInstaller.resolveError.length === 0 && Object.keys(StockInstaller.resolvedItem).length > 0) {
                root.resolveRequested = false
                root.host.openItem(StockInstaller.resolvedItem)
            }
        }
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

        MarketSearchField {
            id: searchField
            x: root.sidePadding
            width: parent.width - root.sidePadding * 2
            busy: browser.loading || StockInstaller.resolving
            placeholderText: root.canResolve ? qsTr("Search, or paste a link") : qsTr("Search")
            onSubmitted: root.submit()
            onCancelRequested: {
                browser.cancel()
                StockInstaller.cancelResolve()
            }
            onTextChanged: {
                if (text.trim().length === 0) {
                    root.submittedQuery = ""
                    root.resolveRequested = false
                }
            }
        }

        Item {
            x: root.sidePadding
            width: parent.width - root.sidePadding * 2
            height: Math.max(providerStrip.height, filterButton.height)
            visible: root.providers.length > 0

            ScrollStrip {
                id: providerStrip
                anchors.left: parent.left
                anchors.right: filterButton.visible ? filterButton.left : parent.right
                anchors.verticalCenter: parent.verticalCenter
                height: providerRow.height

                Row {
                    id: providerRow
                    spacing: Theme.spacingXl

                    Repeater {
                        model: (root.providers.length > 1 ? [{ id: "*", label: qsTr("All sources") }] : []).concat(root.providers)
                        delegate: Text {
                            required property var modelData
                            readonly property bool active: root.providerId === modelData.id
                            readonly property var quota: modelData.quota
                            height: Theme.touchInput ? Theme.androidMinTouchTarget : Theme.controlHeightSm
                            verticalAlignment: Text.AlignVCenter
                            text: quota && quota.remaining !== undefined && quota.remaining !== null
                                  ? qsTr("%1 (%2 left)").arg(modelData.label).arg(quota.remaining)
                                  : modelData.label
                            color: active || providerHover.hovered ? Theme.c.marketplace.foreground : Theme.c.marketplace.foregroundMuted
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSizeSm
                            font.weight: active ? Font.DemiBold : Font.Normal
                            Accessible.role: Accessible.RadioButton
                            Accessible.checked: active
                            Accessible.name: text
                            HoverHandler { id: providerHover; cursorShape: Qt.PointingHandCursor }
                            TapHandler { onTapped: root.selectProvider(modelData.id) }
                        }
                    }
                }
            }

            IconButton {
                id: filterButton
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                visible: root.filters.length > 0
                glyph: Theme.icons.sliders
                active: root.filtersExpanded
                tooltip: root.activeFilterCount > 0
                         ? qsTr("Filters — %n applied", "", root.activeFilterCount)
                         : qsTr("Filters")
                onClicked: root.filtersExpanded = !root.filtersExpanded

                Rectangle {
                    visible: root.activeFilterCount > 0
                    anchors.right: parent.right
                    anchors.top: parent.top
                    width: Theme.spacingXl
                    height: width
                    radius: Theme.c.marketplace.badgeRadius
                    color: Theme.c.marketplace.badge
                    Text {
                        anchors.centerIn: parent
                        text: String(root.activeFilterCount)
                        color: Theme.c.marketplace.badgeForeground
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
            visible: root.filters.length > 0 && root.filtersExpanded

            Repeater {
                model: root.filters
                delegate: Item {
                    id: filterRoot
                    required property var modelData
                    width: filterLoader.width
                    height: filterLoader.height

                    Loader {
                        id: filterLoader
                        sourceComponent: filterRoot.modelData.kind === "toggle" ? toggleFilter
                                         : (filterRoot.modelData.kind === "text" ? textFilter : enumFilter)
                    }
                    Component {
                        id: enumFilter
                        ThemedComboBox {
                            readonly property var filter: filterRoot.modelData
                            textRole: "label"
                            valueRole: "id"
                            model: [{ id: "", label: filter.label }].concat(filter.options || [])
                            onActivated: root.setFilter(filter.id, currentValue)
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
                            onEditingFinished: root.setFilter(filter.id, text.trim())
                        }
                    }
                }
            }
        }

        Text {
            x: root.sidePadding
            width: parent.width - root.sidePadding * 2
            visible: text.length > 0
            text: [root.quotaText, root.warningText].filter(t => t.length > 0).join("  ")
            color: root.warningText.length > 0 ? Theme.c.marketplace.warning : Theme.c.marketplace.foregroundMuted
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeXs
            wrapMode: Text.WordWrap
        }

        Rectangle {
            x: root.sidePadding
            width: parent.width - root.sidePadding * 2
            height: Theme.touchInput ? Theme.androidMinTouchTarget + Theme.spacingLg : Theme.controlHeight * 1.6
            radius: Theme.c.marketplace.cardRadius
            color: linkHover.hovered ? Theme.c.marketplace.cardBackgroundHover : Theme.c.marketplace.cardBackground
            visible: root.queryIsLink && !StockInstaller.resolving

            IconGlyph {
                id: linkGlyph
                anchors.left: parent.left
                anchors.leftMargin: Theme.spacingXl
                anchors.verticalCenter: parent.verticalCenter
                glyph: root.linkSource.glyph
                iconSize: Theme.iconSizeLg
                iconColor: Theme.c.marketplace.foreground
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
                    color: Theme.c.marketplace.foreground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeSm
                    font.weight: Font.Medium
                    elide: Text.ElideRight
                }
                Text {
                    width: parent.width
                    text: root.typedQuery
                    color: Theme.c.marketplace.foregroundMuted
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeXs
                    elide: Text.ElideMiddle
                }
            }
            HoverHandler { id: linkHover; cursorShape: Qt.PointingHandCursor }
            TapHandler { onTapped: root.submit() }
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
            visible: StockInstaller.resolving
            glyph: Theme.icons.spinner
            glyphSpinning: true
            compact: true
            title: qsTr("Looking up that link…")
        }

        EmptyState {
            anchors.centerIn: parent
            width: parent.width
            visible: !StockInstaller.resolving && root.resolveRequested && StockInstaller.resolveError.length > 0
            glyph: Theme.icons.error
            compact: true
            title: qsTr("Couldn’t open that link")
            hint: StockInstaller.resolveError
            actionText: StockInstaller.resolveErrorRetryable ? qsTr("Try again") : ""
            onActionTriggered: root.submit()
        }

        Grid {
            anchors.fill: parent
            anchors.leftMargin: root.sidePadding
            visible: browser.loading && browser.items.length === 0 && !StockInstaller.resolving
            columns: Math.max(1, Math.floor((width + Theme.assetCardGap) / (Theme.assetCardWidth + Theme.assetCardGap)))
            spacing: Theme.assetCardGap
            Repeater {
                model: 9
                delegate: SkeletonBox {
                    width: Theme.assetCardWidth
                    height: root.isAudio ? Theme.controlHeight : Theme.assetCardWidth * 9 / 16
                    animated: parent.visible
                }
            }
        }

        EmptyState {
            anchors.centerIn: parent
            width: parent.width
            visible: !browser.loading && browser.error.length > 0 && browser.items.length === 0
            glyph: Theme.icons.error
            compact: true
            title: qsTr("Search failed")
            hint: browser.error
            actionText: qsTr("Try again")
            onActionTriggered: browser.reload()
        }

        EmptyState {
            anchors.centerIn: parent
            width: parent.width
            visible: !browser.loading && browser.error.length === 0 && browser.items.length === 0
                     && !root.queryIsLink && !StockInstaller.resolving && !root.resolveRequested
            glyph: Theme.icons.search
            compact: true
            title: root.submittedQuery.length > 0 ? qsTr("No results for “%1”").arg(root.submittedQuery)
                                                  : qsTr("Search this source")
            hint: root.submittedQuery.length > 0
                  ? qsTr("Try different words, another source, or clear a filter.")
                  : qsTr("Type above and press Enter.")
        }

        GridView {
            id: tiles
            readonly property int columnCount: Math.max(1, Math.round(width / (Theme.assetCardWidth * 1.3 + Theme.assetCardGap)))
            readonly property real tileWidth: Math.floor(width / columnCount) - Theme.assetCardGap
            anchors.fill: parent
            anchors.leftMargin: root.sidePadding
            anchors.rightMargin: root.sidePadding - Theme.assetCardGap
            visible: browser.items.length > 0 && !root.isAudio && !root.isPhoto
            cellWidth: Math.floor(width / columnCount)
            cellHeight: tileWidth * 9 / 16 + Theme.fontSizeXs * 2.6 + Theme.assetCardGap
            cacheBuffer: Math.max(0, Math.round(height))
            clip: true
            model: visible ? browser.items : []
            ScrollBar.vertical: AppScrollBar { }
            onAtYEndChanged: if (atYEnd && browser.hasMore && !browser.loading) browser.loadMore()

            delegate: Column {
                required property var modelData
                width: tiles.tileWidth
                spacing: Theme.spacingXs

                StockThumb {
                    width: parent.width
                    height: width * 9 / 16
                    item: modelData
                    compact: root.compact
                    onOpenRequested: root.host.openItem(modelData)
                }
                Text {
                    width: parent.width
                    text: modelData.title || ""
                    color: Theme.c.marketplace.foreground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeXs
                    elide: Text.ElideRight
                    maximumLineCount: 2
                    wrapMode: Text.WordWrap
                }
            }
        }

        ListView {
            id: justified
            readonly property real targetHeight: root.compact ? 120 : 96
            readonly property real gap: Theme.spacingSm
            readonly property var rows: {
                const items = browser.items
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
                if (row.length > 0)
                    out.push({ h: targetHeight, cells: row.map(c => ({ index: c.index, w: c.ar * targetHeight })) })
                return out
            }
            anchors.fill: parent
            anchors.leftMargin: root.sidePadding
            anchors.rightMargin: root.sidePadding
            visible: browser.items.length > 0 && root.isPhoto
            model: rows
            spacing: gap
            clip: true
            cacheBuffer: Math.max(0, Math.round(height))
            ScrollBar.vertical: AppScrollBar { }
            onAtYEndChanged: if (atYEnd && browser.hasMore && !browser.loading) browser.loadMore()

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
                        item: browser.items[modelData.index] || ({})
                        compact: root.compact
                        onOpenRequested: root.host.openItem(item)
                    }
                }
            }
        }

        ListView {
            id: audioList
            anchors.fill: parent
            anchors.leftMargin: root.sidePadding
            anchors.rightMargin: root.sidePadding
            visible: browser.items.length > 0 && root.isAudio
            model: visible ? browser.items : []
            clip: true
            ScrollBar.vertical: AppScrollBar { }
            onAtYEndChanged: if (atYEnd && browser.hasMore && !browser.loading) browser.loadMore()

            delegate: StockAudioRow {
                previewPlayer: audioPlayer
                onOpenRequested: (item) => root.host.openItem(item)
            }
        }
    }
}
