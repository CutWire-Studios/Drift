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
    signal recordScreenRequested()
    signal recordCameraRequested()

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

    ThemedMenuSeparator {
        visible: recordScreenItem.visible || recordCameraItem.visible
        height: visible ? implicitHeight : 0
    }

    // Recording is an import: what comes out is source footage and it lands in the bin, so it
    // belongs with the other ways media gets in rather than in a tool panel of its own.
    ThemedMenuItem {
        id: recordScreenItem
        // Hidden rather than disabled where there is no capture backend — a Linux build without
        // PipeWire, a session with no portal — because there is nothing the user could do about it
        // from here.
        visible: EditorState.capture.screenAvailable()
        height: visible ? implicitHeight : 0
        text: qsTr("Record Screen…")
        icon.name: Theme.icons.monitor
        enabled: !EditorState.capture.recording
        onTriggered: root.recordScreenRequested()
    }

    ThemedMenuItem {
        id: recordCameraItem
        visible: EditorState.capture.cameraAvailable()
        height: visible ? implicitHeight : 0
        text: qsTr("Record Camera…")
        icon.name: Theme.icons.video
        enabled: !EditorState.capture.recording
        onTriggered: root.recordCameraRequested()
    }

    ThemedMenuSeparator {
        visible: recordScreenItem.visible || recordCameraItem.visible
        height: visible ? implicitHeight : 0
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
