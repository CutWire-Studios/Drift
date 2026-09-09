import QtQuick
import Drift
import "components"
import "components/assets"

// The home screen's Market destination.
//
// A thin host, not a second store: MarketTab is the browser on both shells and gains a `compact`
// layout rather than being forked, so a fix to search, paging or the download tiles lands in one
// place. What this adds is what a destination owns and a tab inside a sheet does not — the page
// title, the safe-area insets, and a Back that unwinds the preview before Home's own chain takes
// the press.
Item {
    id: root

    // Asked first by AndroidHome.handleBack(), so Back closes the preview rather than skipping
    // straight back to Projects with it still on screen.
    readonly property int downloadCount: {
        void Market.downloadsRevision
        return Market.downloads.length
    }
    readonly property int activeCount: {
        void Market.downloadsRevision
        return Market.activeDownloadCount
    }

    function handleBack() {
        if (linkImportSheet.opened) {
            linkImportSheet.dismiss()
            return true
        }
        if (downloadsSheet.opened) {
            downloadsSheet.dismiss()
            return true
        }
        return marketTab.handleBack()
    }

    // A shared link, routed here by AndroidHome once this destination is on screen. It belongs to
    // the store rather than to the shell: it ends in a marketplace download, and the source it
    // asks the user to choose is a catalog provider.
    function startLinkImport(url) {
        linkImportSheet.openFor(url)
    }

    Item {
        id: header
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.topMargin: root.SafeArea.margins.top
        anchors.leftMargin: root.SafeArea.margins.left
        anchors.rightMargin: root.SafeArea.margins.right
        height: titleLabel.implicitHeight + Theme.spacing2xl + Theme.spacingLg

        ThemedLabel {
            id: titleLabel
            anchors.left: parent.left
            anchors.leftMargin: Theme.androidPagePadding
            anchors.bottom: parent.bottom
            anchors.bottomMargin: Theme.spacingLg
            text: qsTr("Market")
            size: "lg"
        }

        // The way into the downloads sheet, and the only one: the sheet never raises itself,
        // because the per-tile ring is what lets browsing carry on while things download.
        // Present only when there is something to show — an always-on badge reading zero is
        // a control that does nothing most of the time.
        IconButton {
            id: downloadsBadge
            anchors.right: parent.right
            anchors.rightMargin: Theme.androidPagePadding
            anchors.verticalCenter: titleLabel.verticalCenter
            visible: root.downloadCount > 0
            buttonSize: Theme.androidMinTouchTarget
            iconSize: Theme.iconSizeLg
            glyph: Theme.icons.download
            variant: "text"
            tooltip: qsTr("Downloads")
            onClicked: downloadsSheet.open()

            Rectangle {
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: Theme.spacingXs
                visible: root.activeCount > 0
                width: Theme.spacingXl
                height: width
                radius: width / 2
                color: Theme.primary

                Text {
                    anchors.centerIn: parent
                    text: String(root.activeCount)
                    color: Theme.primaryForeground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeXs
                }
            }
        }
    }

    AndroidDownloadsSheet {
        id: downloadsSheet
    }

    AndroidLinkImport {
        id: linkImportSheet
    }

    MarketTab {
        id: marketTab
        anchors.top: header.bottom
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: root.SafeArea.margins.left
        anchors.rightMargin: root.SafeArea.margins.right
        compact: true
    }
}
