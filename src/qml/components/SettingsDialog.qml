import QtQuick
import QtQuick.Controls.Basic
import Drift

// Host for SettingsPane. Opened from the header's Settings menu ("More settings…")
// and from the phone's overflow menu; both used to reach a tab in the assets rail.
ThemedDialog {
    id: root

    title: qsTr("Settings")
    preferredWidth: Theme.dialogWidthLg
    acceptText: qsTr("Done")
    showReject: false

    contentItem: SettingsPane {
        id: pane
        implicitWidth: root.preferredWidth
        // Sizes to the settings themselves until they outgrow the screen, at which
        // point the pane's own Flickable takes over the remainder.
        implicitHeight: Math.min(pane.contentHeight, root.availableContentHeight)
    }
}
