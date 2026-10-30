import QtQuick
import QtQuick.Controls.Basic
import Drift

// Browse, install and remove addons. Content that used to be baked into the build — fonts,
// stickers, the Whisper model — is downloaded from here instead.
ThemedDialog {
    id: root

    title: qsTr("Extras")
    preferredWidth: Theme.dialogWidthLg
    showAccept: false
    rejectText: qsTr("Close")

    function importUserPackage(url) { panel.importUserPackage(url) }
    function openForKind(kind) { panel.openForKind(kind) }

    onOpened: panel.refresh()

    contentItem: AddonManagerPanel {
        id: panel
        onShowRequested: if (!root.visible) root.open()
    }
}
