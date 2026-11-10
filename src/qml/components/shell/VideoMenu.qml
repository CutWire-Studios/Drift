import QtQuick
import QtQuick.Controls.Basic
import Drift

ThemedContextMenu {
    id: root
    implicitWidth: 248

    signal bakesRequested()
    signal cropRequested()
    signal layoutRequested()

    ThemedMenuItem {
        text: qsTr("Bakes…")
        icon.name: Theme.icons.layers
        onTriggered: root.bakesRequested()
    }

    ThemedMenuItem {
        text: EditorState.preview.canvasCropMode ? qsTr("Cancel crop") : qsTr("Crop video")
        icon.name: Theme.icons.crop
        onTriggered: root.cropRequested()
    }

    ThemedMenuSeparator { }

    ThemedMenuItem {
        text: qsTr("Adjust video layout…")
        icon.name: Theme.icons.ratio
        onTriggered: root.layoutRequested()
    }
}
