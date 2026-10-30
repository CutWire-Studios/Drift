import QtQuick
import Drift

// Opens the Marketplace window: on the Downloads tab while something is downloading, on the
// page last shown otherwise.
Item {
    id: downloadsButton
    // The app update also lands in the Downloads tab, so the button shows while
    // one is running even without the marketplace.
    readonly property int activeCount: Downloads.activeCount
    visible: StockInstaller.configured || activeCount > 0
    implicitWidth: downloadsBtn.implicitWidth
    implicitHeight: downloadsBtn.implicitHeight
    width: visible ? implicitWidth : 0
    height: implicitHeight
    anchors.verticalCenter: parent.verticalCenter

    IconButton {
        id: downloadsBtn
        anchors.fill: parent
        glyph: Theme.icons.store
        variant: "ghost"
        active: downloadsButton.activeCount > 0
        tooltip: downloadsButton.activeCount > 0
                 ? qsTr("Marketplace — %n download(s) running", "", downloadsButton.activeCount)
                 : qsTr("Marketplace")
        onClicked: ShellBridge.main.openMarketplace(downloadsButton.activeCount > 0 ? "downloads" : "")
    }

    // Count rather than a plain dot: with a queue behind a three-at-a-time cap,
    // how many are outstanding is the thing worth knowing at a glance.
    Rectangle {
        visible: downloadsButton.activeCount > 0
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: Theme.spacingXs
        width: Math.max(Theme.spacingXl, downloadCount.implicitWidth + Theme.spacingSm)
        height: Theme.spacingXl
        radius: Theme.c.editorHeaderDownloadsButton.badgeRadius
        color: Theme.c.editorHeaderDownloadsButton.badge

        Text {
            id: downloadCount
            anchors.centerIn: parent
            text: String(downloadsButton.activeCount)
            color: Theme.c.editorHeaderDownloadsButton.badgeForeground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeXs
        }
    }
}
