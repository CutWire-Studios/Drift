import QtQuick
import QtQuick.Controls.Basic
import Drift
import ".."

// The Market's first page: CutWire's own animations, face props and 3D objects, one shelf per
// category in the order editors reach for them. Typing in the Market field filters these
// locally and offers the stock sources underneath, so a search never dead-ends here.
Item {
    id: root

    property string query: ""
    property bool compact: Theme.compact
    // Stock types to offer under a search, [{id, label}]. Empty hides those rows.
    property var stockTypes: []

    signal searchStockRequested(string typeId)

    property string openCategory: ""
    property var detailAsset: ({})
    property bool detailShown: false
    // Actions waiting on an install, keyed by installKey(): {key: {mode, slots, assetId}}.
    property var pending: ({})
    property string justAddedId: ""

    readonly property real previewHeight: compact ? 96 : 80
    readonly property real sidePadding: compact ? Theme.androidPagePadding : Theme.pagePadding
    readonly property var searchResults: {
        void DriftAssets.assets
        return query.length > 0 ? DriftAssets.search(query) : []
    }

    // Android Back, forwarded through MarketTab.
    function handleBack() {
        if (detailShown) {
            detailShown = false
            return true
        }
        if (openCategory.length > 0) {
            openCategory = ""
            return true
        }
        return false
    }

    function categoryLabel(id) {
        const cats = DriftAssets.categories
        for (let i = 0; i < cats.length; ++i) {
            if (cats[i].id === id)
                return cats[i].label
        }
        return ""
    }

    function actionLabelFor(asset) {
        if (asset.kind === "face-prop")
            return AppController.selectedClip >= 0 ? qsTr("Apply") : qsTr("Add")
        return qsTr("Add")
    }

    function openDetail(asset) {
        detailAsset = asset
        detailShown = true
    }

    // mode "use": onto the timeline (Lottie, object) or the selected clip (face prop).
    // mode "keep": into the media bin or the Face Props library only.
    function act(asset, mode, slots, variantId) {
        const key = DriftAssets.installKey(asset.id, variantId || "")
        const next = Object.assign({}, pending)
        next[key] = { mode: mode, slots: slots || {}, assetId: asset.id }
        pending = next
        DriftAssets.installVariant(asset.id, variantId || "")
    }

    function cardAction(asset) {
        const faceWithoutClip = asset.kind === "face-prop" && AppController.selectedClip < 0
        act(asset, faceWithoutClip ? "keep" : "use", {})
    }

    function flashAdded(id) {
        justAddedId = id
        addedTimer.restart()
    }

    Timer {
        id: addedTimer
        interval: 1500
        onTriggered: root.justAddedId = ""
    }

    Connections {
        target: DriftAssets

        function onReady(id, binAssetId) {
            const p = root.pending[id]
            if (!p)
                return
            const next = Object.assign({}, root.pending)
            delete next[id]
            root.pending = next

            const assetId = p.assetId || id
            const asset = DriftAssets.assetById(assetId)
            if (asset.kind === "face-prop") {
                if (p.mode === "keep") {
                    AppController.setLastMessage(qsTr("Added to Face props"), "success")
                } else if (!AppController.addFaceProp(AppController.selectedTrack, AppController.selectedClip, id)) {
                    AppController.setLastMessage(qsTr("Select a video or image clip to apply a face prop"), "warning")
                    return
                }
                root.flashAdded(assetId)
                return
            }
            if (p.mode === "keep") {
                AppController.setLastMessage(qsTr("Added to the media bin"), "success")
                root.flashAdded(assetId)
                return
            }
            AppController.addClipsFromAssets([binAssetId])
            // addClipsFromAssets leaves the new clip selected.
            const slots = p.slots || {}
            for (const key in slots)
                AppController.setVectorSlot(AppController.selectedTrack, AppController.selectedClip, key, slots[key])
            root.flashAdded(assetId)
        }

        function onFailed(id, message) {
            const next = Object.assign({}, root.pending)
            delete next[id]
            root.pending = next
            AppController.setLastMessage(message, "error")
        }
    }

    onVisibleChanged: ensureLoaded()
    Component.onCompleted: ensureLoaded()
    function ensureLoaded() {
        if (visible && DriftAssets.assets.length === 0 && !DriftAssets.loading)
            DriftAssets.refresh()
    }

    // --- Loading, error and empty -------------------------------------------------------------

    Column {
        anchors.fill: parent
        anchors.leftMargin: root.sidePadding
        anchors.topMargin: Theme.spacingXl
        spacing: Theme.spacing3xl
        visible: DriftAssets.loading && DriftAssets.assets.length === 0

        Repeater {
            model: 3
            delegate: Column {
                spacing: Theme.spacingLg
                SkeletonBox { width: 96; height: Theme.fontSizeBase }
                Row {
                    spacing: Theme.spacingXl
                    Repeater {
                        model: 4
                        delegate: SkeletonBox { width: root.previewHeight * 1.6; height: root.previewHeight; radius: Theme.radiusMd }
                    }
                }
            }
        }
    }

    EmptyState {
        anchors.centerIn: parent
        width: parent.width
        visible: !DriftAssets.loading && DriftAssets.error.length > 0 && DriftAssets.assets.length === 0
        glyph: Theme.icons.error
        title: qsTr("Couldn’t load Drift Assets")
        hint: DriftAssets.error
        actionText: qsTr("Try again")
        onActionTriggered: DriftAssets.refresh()
    }

    EmptyState {
        anchors.centerIn: parent
        width: parent.width
        visible: !DriftAssets.loading && DriftAssets.error.length === 0 && DriftAssets.assets.length === 0
        glyph: Theme.icons.sparkles
        title: qsTr("No assets here yet")
        hint: qsTr("Drift Assets are still being published. Check back soon.")
        actionText: qsTr("Refresh")
        onActionTriggered: DriftAssets.refresh()
    }

    // --- Shelves ------------------------------------------------------------------------------

    ListView {
        id: shelves
        anchors.fill: parent
        visible: root.query.length === 0 && root.openCategory.length === 0 && DriftAssets.assets.length > 0
        model: DriftAssets.categories
        spacing: Theme.spacing3xl
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        cacheBuffer: Math.max(0, Math.round(height))
        ScrollBar.vertical: AppScrollBar { }
        header: Item { width: 1; height: Theme.spacingXl }
        footer: Item { width: 1; height: Theme.spacing3xl }

        delegate: Column {
            id: shelf
            required property var modelData
            width: shelves.width
            spacing: Theme.spacingLg

            readonly property var items: {
                void DriftAssets.assets
                return DriftAssets.assetsIn(modelData.id)
            }
            // On touch, cards play while their shelf is on screen.
            readonly property bool onScreen: shelves.visible
                                             && y + height > shelves.contentY
                                             && y < shelves.contentY + shelves.height

            Item {
                x: root.sidePadding
                width: parent.width - root.sidePadding * 2
                height: shelfTitle.implicitHeight

                Text {
                    id: shelfTitle
                    text: shelf.modelData.label
                    color: Theme.panelForeground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeBase
                    font.weight: Font.DemiBold
                }

                Text {
                    anchors.right: parent.right
                    anchors.verticalCenter: shelfTitle.verticalCenter
                    visible: shelfRow.contentWidth > shelfRow.width
                    text: qsTr("See all")
                    color: seeAllHover.hovered ? Theme.panelForeground : Theme.mutedForeground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeXs
                    HoverHandler { id: seeAllHover; cursorShape: Qt.PointingHandCursor }
                    TapHandler { onTapped: root.openCategory = shelf.modelData.id }
                }
            }

            ListView {
                id: shelfRow
                width: parent.width
                height: root.previewHeight + Theme.fontSizeSm * 1.4 + Theme.fontSizeXs * 1.4 + Theme.spacingSm * 2
                orientation: ListView.Horizontal
                spacing: Theme.spacingXl
                leftMargin: root.sidePadding
                rightMargin: root.sidePadding
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                model: shelf.items
                delegate: DriftAssetCard {
                    required property var modelData
                    asset: modelData
                    previewHeight: root.previewHeight
                    compact: root.compact
                    autoplay: root.compact && shelf.onScreen
                              && x + width > shelfRow.contentX && x < shelfRow.contentX + shelfRow.width
                    actionLabel: root.actionLabelFor(modelData)
                    justAdded: root.justAddedId === modelData.id
                    onOpenRequested: root.openDetail(modelData)
                    onActionRequested: root.cardAction(modelData)
                }
            }
        }
    }

    // --- One category, all of it --------------------------------------------------------------

    // The back row stays put above the grid rather than scrolling away with it.
    Item {
        id: categoryPage
        anchors.fill: parent
        visible: root.query.length === 0 && root.openCategory.length > 0

        Row {
            id: categoryHeader
            x: root.sidePadding
            topPadding: Theme.spacingSm
            bottomPadding: Theme.spacingSm
            spacing: Theme.spacingSm
            IconButton {
                glyph: Theme.icons.chevronLeft
                tooltip: qsTr("All assets")
                onClicked: root.openCategory = ""
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: root.categoryLabel(root.openCategory)
                color: Theme.panelForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeBase
                font.weight: Font.DemiBold
            }
        }

        Flickable {
            id: categoryFlick
            anchors.top: categoryHeader.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            contentHeight: categoryFlow.implicitHeight + Theme.spacingSm + Theme.spacing3xl
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: AppScrollBar { }

            Flow {
                id: categoryFlow
                x: root.sidePadding
                y: Theme.spacingSm
                width: categoryFlick.width - root.sidePadding * 2
                spacing: Theme.spacingXl

                Repeater {
                    model: {
                        void DriftAssets.assets
                        return categoryPage.visible ? DriftAssets.assetsIn(root.openCategory) : []
                    }
                    delegate: DriftAssetCard {
                        required property var modelData
                        asset: modelData
                        previewHeight: root.previewHeight
                        compact: root.compact
                        autoplay: root.compact && categoryPage.visible
                        actionLabel: root.actionLabelFor(modelData)
                        justAdded: root.justAddedId === modelData.id
                        onOpenRequested: root.openDetail(modelData)
                        onActionRequested: root.cardAction(modelData)
                    }
                }
            }
        }
    }

    // --- Search -------------------------------------------------------------------------------

    Flickable {
        id: searchPage
        anchors.fill: parent
        visible: root.query.length > 0 && DriftAssets.assets.length > 0
        contentHeight: searchColumn.implicitHeight + Theme.spacing3xl
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: AppScrollBar { }

        Column {
            id: searchColumn
            x: root.sidePadding
            width: searchPage.width - root.sidePadding * 2
            spacing: Theme.spacing2xl
            topPadding: Theme.spacingXl

            Text {
                width: parent.width
                visible: root.searchResults.length === 0
                text: qsTr("No Drift Assets match “%1”.").arg(root.query)
                color: Theme.mutedForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeSm
                wrapMode: Text.WordWrap
            }

            Flow {
                width: parent.width
                spacing: Theme.spacingXl
                visible: root.searchResults.length > 0

                Repeater {
                    model: root.searchResults
                    delegate: DriftAssetCard {
                        required property var modelData
                        asset: modelData
                        previewHeight: root.previewHeight
                        compact: root.compact
                        autoplay: root.compact && searchPage.visible
                        actionLabel: root.actionLabelFor(modelData)
                        justAdded: root.justAddedId === modelData.id
                        onOpenRequested: root.openDetail(modelData)
                        onActionRequested: root.cardAction(modelData)
                    }
                }
            }

            // Where to go when the pack has nothing: the same words against stock footage.
            Column {
                width: parent.width
                spacing: 0
                visible: root.stockTypes.length > 0

                Repeater {
                    model: root.stockTypes
                    delegate: Rectangle {
                        required property var modelData
                        width: parent.width
                        height: Theme.touchUi ? Theme.androidMinTouchTarget : Theme.controlHeight + Theme.spacingSm
                        radius: Theme.radiusSm
                        color: rowHover.hovered ? Theme.popoverHover : "transparent"

                        IconGlyph {
                            id: rowGlyph
                            anchors.left: parent.left
                            anchors.leftMargin: Theme.spacingMd
                            anchors.verticalCenter: parent.verticalCenter
                            glyph: Theme.icons.search
                            iconSize: Theme.iconSizeMd
                            iconColor: Theme.mutedForeground
                        }
                        Text {
                            anchors.left: rowGlyph.right
                            anchors.right: rowChevron.left
                            anchors.leftMargin: Theme.spacingLg
                            anchors.verticalCenter: parent.verticalCenter
                            text: qsTr("Search %1 for “%2”").arg(String(modelData.label).toLowerCase()).arg(root.query)
                            color: Theme.panelForeground
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSizeSm
                            elide: Text.ElideRight
                        }
                        IconGlyph {
                            id: rowChevron
                            anchors.right: parent.right
                            anchors.rightMargin: Theme.spacingMd
                            anchors.verticalCenter: parent.verticalCenter
                            glyph: Theme.icons.chevronRight
                            iconSize: Theme.iconSizeMd
                            iconColor: Theme.mutedForeground
                        }
                        HoverHandler { id: rowHover; cursorShape: Qt.PointingHandCursor }
                        TapHandler { onTapped: root.searchStockRequested(modelData.id) }
                    }
                }
            }
        }
    }

    // --- Detail -------------------------------------------------------------------------------

    // Desktop: slides in from the right over the shelves, which stay where they were for Back.
    // Phone: a bottom sheet over a scrim.
    Rectangle {
        anchors.fill: parent
        color: Theme.scrimColor
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

    DriftAssetDetail {
        id: detail
        width: parent.width
        height: root.compact ? parent.height * 0.85 : parent.height
        x: root.compact ? 0 : (root.detailShown ? 0 : parent.width)
        y: root.compact ? (root.detailShown ? parent.height - height : parent.height) : 0
        visible: root.compact ? y < parent.height : x < parent.width
        radius: root.compact ? Theme.radiusLg : 0
        compact: root.compact
        asset: root.detailAsset
        categoryLabel: root.categoryLabel(root.detailAsset.category || "")

        Behavior on x { NumberAnimation { duration: Theme.durationSlow; easing.type: Theme.easing } }
        Behavior on y { NumberAnimation { duration: Theme.durationSlow; easing.type: Theme.easing } }

        onBackRequested: root.detailShown = false
        onUseRequested: (slots) => root.act(root.detailAsset, "use", slots, detail.selectedVariantId)
        onKeepRequested: root.act(root.detailAsset, "keep", {}, detail.selectedVariantId)
    }
}
