import QtQuick
import QtQuick.Controls.Basic
import Drift

ThemedContextMenu {
    id: root
    implicitWidth: 248

    signal bakesRequested()
    signal cropRequested()
    signal layoutRequested()
    signal multicamRequested()

    ThemedMenuItem {
        text: qsTr("Size, aspect & frame rate…")
        icon.name: Theme.icons.ratio
        onTriggered: root.layoutRequested()
    }

    ThemedMenuItem {
        text: EditorState.preview.canvasCropMode ? qsTr("Cancel crop") : qsTr("Crop canvas")
        icon.name: Theme.icons.crop
        onTriggered: root.cropRequested()
    }

    ThemedMenuItem {
        text: qsTr("Multicam…")
        icon.name: Theme.icons.shuffle
        onTriggered: root.multicamRequested()
    }

    ThemedMenuSeparator { }

    ThemedMenuItem {
        text: qsTr("Pre-rendered sections…")
        icon.name: Theme.icons.layers
        onTriggered: root.bakesRequested()
    }
}
