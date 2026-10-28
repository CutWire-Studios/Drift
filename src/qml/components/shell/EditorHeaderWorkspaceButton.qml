import QtQuick
import Drift

// Switches between workspaces. The list and the edit entry live in the menu; the layouts
// themselves are edited in Settings.
Item {
    id: workspaceButton
    implicitWidth: workspaceBtn.implicitWidth
    implicitHeight: workspaceBtn.implicitHeight
    width: implicitWidth
    height: implicitHeight
    anchors.verticalCenter: parent.verticalCenter

    Rectangle {
        id: workspaceBtn
        implicitWidth: row.implicitWidth + Theme.spacingXl * 2
        implicitHeight: 32
        radius: Theme.c.editorHeaderProjectsButton.radius
        color: area.containsMouse || workspaceMenu.opened
               ? Theme.c.editorHeaderProjectsButton.backgroundHover
               : Theme.c.editorHeaderProjectsButton.background

        Accessible.role: Accessible.Button
        Accessible.name: qsTr("Workspaces")

        Row {
            id: row
            anchors.left: parent.left
            anchors.leftMargin: Theme.spacingXl
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.spacingMd

            Text {
                text: qsTr("Workspaces")
                color: Theme.c.editorHeaderProjectsButton.foreground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeSm
                font.weight: Font.Medium
                anchors.verticalCenter: parent.verticalCenter
            }
        }

        ThemedToolTip {
            visible: area.containsMouse && !workspaceMenu.opened
            text: qsTr("Switch workspace")
        }

        MouseArea {
            id: area
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: workspaceMenu.popup(0, workspaceButton.height + Theme.spacingMd)
        }
    }

    ThemedContextMenu {
        id: workspaceMenu
        implicitWidth: 248

        Instantiator {
            model: Workspaces.workspaces

            ThemedMenuItem {
                required property var modelData
                text: modelData.name
                icon.name: modelData.id === Workspaces.activeId ? Theme.icons.check : ""
                onTriggered: Workspaces.activeId = modelData.id
            }

            onObjectAdded: (index, object) => workspaceMenu.insertItem(index, object)
            onObjectRemoved: (index, object) => workspaceMenu.removeItem(object)
        }

        ThemedMenuSeparator { }

        ThemedMenuItem {
            text: qsTr("Customize layouts in Settings…")
            icon.name: Theme.icons.sliders
            onTriggered: Qt.callLater(() => ShellBridge.main.openSettings("workspaces"))
        }
    }
}
