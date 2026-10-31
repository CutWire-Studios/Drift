import QtQuick
import Drift
// More import options hanging off the header's split button.
ThemedContextMenu {
    id: root

    property bool collecting: false
    property bool showCollect: true

    signal importFilesRequested()
    signal importFolderRequested()
    signal collectMediaRequested()

    implicitWidth: 220

    ThemedMenuItem {
        text: qsTr("Import Files…")
        icon.name: Theme.icons.upload
        onTriggered: root.importFilesRequested()
    }

    ThemedMenuItem {
        visible: FileDialogs.supportsDirectoryPicker()
        height: visible ? implicitHeight : 0
        text: qsTr("Import Folder…")
        icon.name: Theme.icons.folderInput
        onTriggered: root.importFolderRequested()
    }

    ThemedMenuItem {
        visible: !!ShellBridge.main && !!ShellBridge.main.openMarketplace
        height: visible ? implicitHeight : 0
        text: qsTr("Browse Marketplace…")
        icon.name: Theme.icons.store
        onTriggered: ShellBridge.main.openMarketplace("")
    }

    ThemedMenuSeparator {
        visible: root.showCollect && FileDialogs.supportsDirectoryPicker()
        height: visible ? implicitHeight : 0
    }

    ThemedMenuItem {
        visible: root.showCollect && FileDialogs.supportsDirectoryPicker()
        height: visible ? implicitHeight : 0
        text: qsTr("Collect Media to Folder…")
        icon.name: Theme.icons.folderOutput
        enabled: !root.collecting
        onTriggered: root.collectMediaRequested()
    }
}
