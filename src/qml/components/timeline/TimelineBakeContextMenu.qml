import QtQuick
import Drift

ThemedContextMenu {
    id: menu

    property string bakeId: ""
    property string bakeState: ""
    readonly property bool real: bakeId !== "" && bakeId !== "pending"

    signal manageRequested()

    ThemedMenuItem {
        text: qsTr("Re-render")
        icon.name: Theme.icons.refresh
        enabled: menu.real && !EditorState.bakes.busy
        onTriggered: EditorState.bakes.rebake(menu.bakeId)
    }
    ThemedMenuItem {
        text: qsTr("Set work area to pre-render")
        enabled: menu.real
        onTriggered: EditorState.bakes.setWorkAreaToBake(menu.bakeId)
    }
    ThemedMenuItem {
        text: qsTr("Delete pre-rendered section")
        icon.name: Theme.icons.trash
        enabled: menu.real
        onTriggered: EditorState.bakes.deleteBake(menu.bakeId)
    }
    ThemedMenuSeparator { }
    ThemedMenuItem {
        text: qsTr("Manage pre-rendered sections…")
        icon.name: Theme.icons.layers
        onTriggered: menu.manageRequested()
    }
}
