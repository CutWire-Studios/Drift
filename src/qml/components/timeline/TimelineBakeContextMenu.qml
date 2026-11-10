import QtQuick
import Drift

ThemedContextMenu {
    id: menu

    property string bakeId: ""
    property string bakeState: ""
    readonly property bool real: bakeId !== "" && bakeId !== "pending"

    signal manageRequested()

    ThemedMenuItem {
        text: qsTr("Re-bake")
        icon.name: Theme.icons.refresh
        enabled: menu.real && !EditorState.bakes.busy
        onTriggered: EditorState.bakes.rebake(menu.bakeId)
    }
    ThemedMenuItem {
        text: qsTr("Set work area to bake")
        enabled: menu.real
        onTriggered: EditorState.bakes.setWorkAreaToBake(menu.bakeId)
    }
    ThemedMenuItem {
        text: qsTr("Delete bake")
        icon.name: Theme.icons.trash
        enabled: menu.real
        onTriggered: EditorState.bakes.deleteBake(menu.bakeId)
    }
    ThemedMenuSeparator { }
    ThemedMenuItem {
        text: qsTr("Manage bakes…")
        icon.name: Theme.icons.layers
        onTriggered: menu.manageRequested()
    }
}
