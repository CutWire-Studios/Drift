import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Window
import Drift
import "components"

// A link shared into Drift, resolved through the marketplace.
//
// The provider is chosen, not guessed. A link's host does not reliably say which source can
// extract it — several providers can advertise the same site, and the catalog is the only thing
// that knows what any of them accept — so this lists every resolve-capable source and lets the
// user say. It lives in the Market destination because that is what it is: a marketplace fetch
// that happens to start from a link instead of a search.
//
// Resolve is a polled job, not a request/response: POST /resolve returns a job id and the client
// polls until ready or failed, so the item does not exist when the request returns and a failure
// can arrive either on the POST or later on the job. That is why downloading is a third state
// rather than something that follows immediately.
AndroidBottomSheet {
    id: root

    // "" | "catalog" | "choosing" | "resolving" | "downloading" | "done" | "failed"
    property string state: ""
    property string sourceUrl: ""
    property string itemId: ""
    property string importedAssetId: ""
    property string message: ""
    property string failureCode: ""
    property bool canRetry: false

    title: qsTr("Open link")

    // Fixed, NOT derived from the content's implicitHeight: a Popup builds its content lazily on
    // first open, so a column measures near nothing at the moment the sheet takes its resting
    // height, and the sheet then stays that tall with everything laid out below the screen.
    // AndroidAddMenu carries the same note for the same reason.
    sheetHeightFraction: Theme.androidSheetHeightFraction
    sheetExpandedFraction: Theme.androidSheetExpandedFraction

    // Every (type, provider) pair whose capabilities include "resolve". Flattened across types
    // because resolveUrl() sends the *active type* in the POST body — the pair is the unit that
    // matters, not the provider alone.
    readonly property var resolveProviders: {
        void Market.types
        let out = []
        const types = Market.types
        for (let t = 0; t < types.length; ++t) {
            const providers = types[t].providers || []
            for (let p = 0; p < providers.length; ++p) {
                const caps = providers[p].capabilities || []
                if (caps.indexOf("resolve") < 0)
                    continue
                out.push({
                    typeId: types[t].id,
                    providerId: providers[p].id,
                    label: providers[p].label || providers[p].id,
                    typeLabel: types[t].label || types[t].id
                })
            }
        }
        return out
    }

    function openFor(url) {
        root.sourceUrl = url
        root.itemId = ""
        root.message = ""
        root.failureCode = ""
        root.canRetry = false
        root.open()
        root.loadProviders()
    }

    // The catalog is fetched when the Market destination is first shown, and a share that cold-
    // starts the app has never shown it — so Market.types is empty here until this asks.
    function loadProviders() {
        root.message = ""
        root.failureCode = ""
        root.canRetry = false
        if (Market.types.length === 0) {
            root.state = "catalog"
            if (!Market.catalogLoading)
                Market.refreshCatalog()
            return
        }
        if (root.resolveProviders.length === 0) {
            root.state = "failed"
            root.message = qsTr("No source in the marketplace can open links.")
            return
        }
        root.state = "choosing"
    }

    function resolveWith(pair) {
        Market.activeTypeId = pair.typeId
        Market.activeProviderId = pair.providerId
        root.state = "resolving"
        root.message = ""
        root.failureCode = ""
        root.canRetry = false
        Market.resolveUrl(root.sourceUrl)
    }

    // Both the catalog fetch and the resolve job clear their "loading" flag *before* applying
    // what they fetched — setCatalogLoading(false) then applyCatalog(), setSearching(false) then
    // applyResolvedItem(). Concluding on the flag therefore reads the state one statement too
    // early: the sheet said "couldn't reach the marketplace" while the catalog was landing behind
    // it, and "that source didn't recognise this link" over the video it had just resolved.
    //
    // So neither of these concludes from a signal directly. Every handler defers to one of these
    // through Qt.callLater, which runs after the whole emitting turn has finished and every
    // property involved has settled.
    function evaluateCatalog() {
        if (root.state !== "catalog" || Market.catalogLoading)
            return
        if (Market.types.length > 0) {
            root.loadProviders()
            return
        }
        root.state = "failed"
        root.message = Market.catalogError.length > 0
                       ? Market.catalogError
                       : qsTr("Couldn’t reach the marketplace.")
        root.canRetry = Market.catalogErrorRetryable
    }

    function evaluateResolve() {
        if (root.state !== "resolving" || Market.searching)
            return
        if (Market.searchError.length > 0) {
            root.fail()
            return
        }
        const items = Market.items
        if (items.length > 0) {
            root.itemId = items[0].id
            root.state = "downloading"
            Market.download(root.itemId, "", "", items[0].title || "",
                            items[0].media_kind || items[0].type || "")
            return
        }
        root.state = "failed"
        root.message = qsTr("That source didn’t recognise this link.")
        root.canRetry = false
    }

    function fail() {
        root.state = "failed"
        root.message = Market.searchError
        root.failureCode = Market.searchErrorCode
        // auth_required is a sentence and nothing else — the product rule is absolute: no sign-in
        // prompt, no account CTA, no marketplace URL anywhere in the app.
        root.canRetry = Market.searchErrorRetryable && root.failureCode !== "auth_required"
    }

    Connections {
        target: Market
        enabled: root.opened

        function onCatalogChanged() { Qt.callLater(root.evaluateCatalog) }
        function onCatalogLoadingChanged() { Qt.callLater(root.evaluateCatalog) }

        function onSearchingChanged() { Qt.callLater(root.evaluateResolve) }
        function onItemsChanged() { Qt.callLater(root.evaluateResolve) }
        function onSearchErrorChanged() { Qt.callLater(root.evaluateResolve) }

        function onDownloadFailed(itemId, code, message) {
            if (itemId !== root.itemId)
                return
            root.state = "failed"
            root.message = message
            root.failureCode = code
            root.canRetry = code !== "auth_required" && code !== "rate_limited"
                            && code !== "payment_required" && code !== "not_found"
        }

        function onDownloadImported(itemId, name) {
            if (itemId !== root.itemId)
                return
            // importLocalPaths appends one row, so the newest asset is this download. Captured by
            // id rather than index because a later import would shift the index out from under it.
            root.importedAssetId = AssetLibrary.assetIdAt(AssetLibrary.count - 1)
            root.state = "done"
            root.message = qsTr("“%1” is ready.").arg(name)
        }
    }

    Item {
        anchors.fill: parent

        // --- Choosing a source ------------------------------------------------
        Column {
            anchors.fill: parent
            visible: root.state === "choosing"

            Item {
                width: parent.width
                height: linkLabel.implicitHeight + Theme.spacingXl * 2

                ThemedLabel {
                    id: linkLabel
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.leftMargin: Theme.androidPagePadding + root.safeLeft
                    anchors.rightMargin: Theme.androidPagePadding + root.safeRight
                    anchors.verticalCenter: parent.verticalCenter
                    elide: Text.ElideMiddle
                    text: qsTr("Which source should open this link?")
                }
            }

            ListView {
                width: parent.width
                height: Math.max(0, parent.height - linkLabel.implicitHeight - Theme.spacingXl * 2)
                clip: true
                model: root.resolveProviders
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: AppScrollBar { }

                delegate: SheetActionRow {
                    required property var modelData
                    width: ListView.view.width
                    label: modelData.label
                    detail: modelData.typeLabel
                    glyph: Theme.icons.store
                    sideInset: root.safeLeft
                    sideInsetRight: root.safeRight
                    onClicked: {
                        Haptics.select()
                        root.resolveWith(modelData)
                    }
                }
            }
        }

        // --- Working, and the outcome ----------------------------------------
        Column {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.leftMargin: Theme.androidPagePadding + root.safeLeft
            anchors.rightMargin: Theme.androidPagePadding + root.safeRight
            topPadding: Theme.spacingXl
            spacing: Theme.spacingXl
            visible: root.state !== "choosing"

            Row {
                width: parent.width
                spacing: Theme.spacingLg
                visible: root.state === "catalog" || root.state === "resolving"
                         || root.state === "downloading"

                IconGlyph {
                    id: busyGlyph
                    anchors.verticalCenter: parent.verticalCenter
                    glyph: Theme.icons.spinner
                    iconSize: Theme.iconSizeLg
                    iconColor: Theme.mutedForeground

                    RotationAnimator on rotation {
                        running: busyGlyph.visible
                        loops: Animation.Infinite
                        from: 0
                        to: 360
                        duration: 900
                    }
                }

                ThemedLabel {
                    anchors.verticalCenter: parent.verticalCenter
                    text: root.state === "catalog" ? qsTr("Loading sources…")
                          : root.state === "resolving" ? qsTr("Asking that source…")
                          : qsTr("Downloading…")
                }
            }

            ThemedLabel {
                width: parent.width
                visible: root.state === "failed" || root.state === "done"
                wrapMode: Text.WordWrap
                // The backend's own sentence, not one invented here: it is the only thing that
                // knows which of the five source failures behind provider_unavailable happened.
                text: root.message
            }

            Row {
                width: parent.width
                spacing: Theme.androidTouchGap
                visible: root.state === "failed"

                ThemedButton {
                    visible: root.canRetry
                    variant: "primary"
                    text: qsTr("Try again")
                    onClicked: root.loadProviders()
                }

                // A different source may well accept a link this one refused, so the way back to
                // the list is the main thing to offer here.
                ThemedButton {
                    visible: root.resolveProviders.length > 1
                    variant: "secondary"
                    text: qsTr("Pick another source")
                    onClicked: root.state = "choosing"
                }

                // A 404 means the item is gone from the source's API, not that the page is.
                ThemedButton {
                    visible: root.failureCode === "not_found"
                    variant: "secondary"
                    text: qsTr("Open in browser")
                    onClicked: {
                        Qt.openUrlExternally(root.sourceUrl)
                        root.dismiss()
                    }
                }
            }

            Row {
                width: parent.width
                spacing: Theme.androidTouchGap
                visible: root.state === "done"

                // The download is in the bin, and the bin is in the editor — so without this the
                // flow ends on a sentence and no visible change anywhere.
                ThemedButton {
                    variant: "primary"
                    glyph: Theme.icons.plus
                    text: qsTr("Add to timeline")
                    onClicked: {
                        const assetId = root.importedAssetId
                        root.dismiss()
                        Window.window.openDownloadedAsset(assetId)
                    }
                }

                // Staying put is a real choice: several links in a row land in the same bin.
                ThemedButton {
                    variant: "secondary"
                    text: qsTr("Keep browsing")
                    onClicked: root.dismiss()
                }
            }
        }
    }
}
