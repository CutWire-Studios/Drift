import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Window
import Drift
// A link shared into Drift, resolved through the marketplace.
//
// Resolve is a polled job: StockInstaller.resolveUrl() returns at once and resolveChanged fires
// as the job progresses, ending in resolvedItem or resolveError. The server picks the source, so
// there is nothing to choose before resolving; the user only picks a variant afterwards.
AndroidBottomSheet {
    id: root

    // "" | "resolving" | "choosing" | "downloading" | "done" | "failed"
    property string state: ""
    property string sourceUrl: ""
    property var item: ({})
    property string jobId: ""
    property string importedAssetId: ""
    property string message: ""
    property string failureCode: ""
    property bool canRetry: false

    readonly property var variants: item.variants || []

    title: qsTr("Open link")

    // Fixed, NOT derived from the content's implicitHeight: a Popup builds its content lazily on
    // first open, so a column measures near nothing at the moment the sheet takes its resting
    // height, and the sheet then stays that tall with everything laid out below the screen.
    // AndroidAddMenu carries the same note for the same reason.
    sheetHeightFraction: Theme.androidSheetHeightFraction
    sheetExpandedFraction: Theme.androidSheetExpandedFraction

    function openFor(url) {
        root.sourceUrl = url
        root.open()
        root.resolve()
    }

    function resolve() {
        root.item = ({})
        root.message = ""
        root.failureCode = ""
        root.canRetry = false
        root.state = "resolving"
        StockInstaller.resolveUrl(root.sourceUrl)
    }

    // resolving is cleared before the item or error is applied in the same turn, so the outcome
    // is read after the emitting turn has finished.
    function evaluateResolve() {
        if (root.state !== "resolving" || StockInstaller.resolving)
            return
        if (StockInstaller.resolveError.length > 0) {
            root.state = "failed"
            root.message = StockInstaller.resolveError
            root.failureCode = StockInstaller.resolveErrorCode
            root.canRetry = StockInstaller.resolveErrorRetryable
            return
        }
        const found = StockInstaller.resolvedItem
        if (!found || !found.id) {
            root.state = "failed"
            root.message = qsTr("The marketplace didn’t recognise this link.")
            return
        }
        root.item = found
        root.state = "choosing"
    }

    // Same folder rule as the marketplace detail view: ask once where to save, then reuse it.
    function download(variantId) {
        let dir = StockInstaller.lastDownloadDir
        if (FileDialogs.supportsDirectoryPicker() && (!dir || dir.toString() === "")) {
            dir = FileDialogs.openDirectory(qsTr("Save downloads to"), "")
            if (!dir || dir.toString() === "")
                return
            StockInstaller.lastDownloadDir = dir
        }
        root.jobId = root.item.source + "/" + root.item.id
        root.state = "downloading"
        StockInstaller.download(root.item.source, root.item.id, variantId,
                                FileDialogs.supportsDirectoryPicker() ? dir : "",
                                root.item.title || "", root.item.media_kind || root.item.mediaKind || "")
    }

    onClosed: if (root.state === "resolving") StockInstaller.cancelResolve()

    Connections {
        target: StockInstaller
        enabled: root.opened

        function onResolveChanged() { Qt.callLater(root.evaluateResolve) }

        function onDownloadFailed(id, code, message) {
            if (id !== root.jobId)
                return
            root.state = "failed"
            root.message = message
            root.failureCode = code
            root.canRetry = false
        }

        function onDownloadImported(id, name) {
            if (id !== root.jobId)
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

        // --- Choosing a variant -----------------------------------------------
        Column {
            anchors.fill: parent
            visible: root.state === "choosing"

            Item {
                width: parent.width
                height: itemLabel.implicitHeight + Theme.spacingXl * 2

                ThemedLabel {
                    id: itemLabel
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.leftMargin: Theme.androidPagePadding + root.safeLeft
                    anchors.rightMargin: Theme.androidPagePadding + root.safeRight
                    anchors.verticalCenter: parent.verticalCenter
                    wrapMode: Text.WordWrap
                    text: root.item.title || root.sourceUrl
                }
            }

            ListView {
                width: parent.width
                height: Math.max(0, parent.height - itemLabel.implicitHeight - Theme.spacingXl * 2)
                clip: true
                model: root.variants.length > 0 ? root.variants : [{ id: "", label: qsTr("Download") }]
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: AppScrollBar { }

                delegate: SheetActionRow {
                    required property var modelData
                    width: ListView.view.width
                    label: modelData.label || modelData.id
                    glyph: Theme.icons.download
                    sideInset: root.safeLeft
                    sideInsetRight: root.safeRight
                    onClicked: {
                        Haptics.select()
                        root.download(modelData.id)
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

            AndroidLinkBusyStatus {
                width: parent.width
                visible: root.state === "resolving" || root.state === "downloading"
                statusText: root.state === "resolving" ? qsTr("Looking up the link…")
                            : qsTr("Downloading…")
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
                    onClicked: root.resolve()
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
                        ShellBridge.main.openDownloadedAsset(assetId)
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
