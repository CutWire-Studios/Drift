import QtQuick
import QtQuick.Controls.Basic
import Drift

// Workspace switcher: a dropdown chip naming the active workspace, with the "back" pill that
// appears after a jump into another workspace. Studio workspaces sit in a submenu so the
// everyday ones stay short.
Row {
    id: root
    spacing: Theme.spacingMd

    // Workspaces that go under "Studio"; everything else (including user-made and preset
    // layouts) is listed under "Everyday".
    readonly property var studioIds: EditorState.preferences.studioWorkspaceIds

    readonly property string activeName: {
        const list = Workspaces.workspaces
        for (let i = 0; i < list.length; ++i) {
            if (list[i].id === Workspaces.activeId)
                return list[i].name
        }
        return ""
    }

    // Alt+N follows the position in the full workspace list, not in a group.
    function entries(studio) {
        const list = Workspaces.workspaces
        const out = []
        for (let i = 0; i < list.length; ++i) {
            const hidden = EditorState.preferences.hiddenWorkspaces.indexOf(list[i].id) >= 0
            if (hidden && list[i].id !== Workspaces.activeId)
                continue
            if ((root.studioIds.indexOf(list[i].id) >= 0) === studio)
                out.push({ id: list[i].id, name: list[i].name, shortcutId: i < 9 ? "workspace." + (i + 1) : "" })
        }
        return out
    }

    Rectangle {
        id: chip
        width: chipRow.implicitWidth + Theme.spacingXl * 2
        height: 32
        radius: Theme.c.editorHeaderWorkspaceButton.radius
        color: chipArea.containsMouse || workspaceMenu.opened ? Theme.c.editorHeaderWorkspaceButton.backgroundHover
                                                              : Theme.c.editorHeaderWorkspaceButton.background
        anchors.verticalCenter: parent.verticalCenter

        Behavior on color {
            ColorAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
        }

        Accessible.role: Accessible.Button
        Accessible.name: qsTr("Workspace: %1").arg(root.activeName)
        Accessible.onPressAction: workspaceMenu.popup(0, chip.height + Theme.spacingMd)

        Row {
            id: chipRow
            anchors.left: parent.left
            anchors.leftMargin: Theme.spacingXl
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.spacingMd

            Text {
                text: root.activeName
                color: Theme.c.editorHeaderWorkspaceButton.foreground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeSm
                font.weight: Font.Medium
                anchors.verticalCenter: parent.verticalCenter
            }

            IconGlyph {
                glyph: Theme.icons.chevronDown
                iconSize: Theme.iconSizeSm
                iconColor: Theme.c.editorHeaderWorkspaceButton.icon
                anchors.verticalCenter: parent.verticalCenter
            }
        }

        RichToolTip {
            visible: chipArea.containsMouse && !workspaceMenu.opened
            title: qsTr("Switch workspace")
            body: qsTr("Each workspace arranges the panels for one kind of task")
        }

        MouseArea {
            id: chipArea
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: workspaceMenu.popup(0, chip.height + Theme.spacingMd)
        }

        ThemedContextMenu {
            id: workspaceMenu
            implicitWidth: 248

            ThemedMenuItem {
                sectionHeader: true
                text: qsTr("Everyday")
            }

            Instantiator {
                model: root.entries(false)

                ThemedMenuItem {
                    required property var modelData
                    text: modelData.name
                    shortcutId: modelData.shortcutId
                    icon.name: modelData.id === Workspaces.activeId ? Theme.icons.check : ""
                    onTriggered: Workspaces.activeId = modelData.id
                }

                onObjectAdded: (index, object) => workspaceMenu.insertItem(index + 1, object)
                onObjectRemoved: (index, object) => workspaceMenu.removeItem(object)
            }

            ThemedMenuSeparator { }

            ThemedContextMenu {
                id: studioMenu
                title: qsTr("Studio")
                enabled: studioMenu.count > 0
                implicitWidth: 248

                Instantiator {
                    model: root.entries(true)

                    ThemedMenuItem {
                        required property var modelData
                        text: modelData.name
                        shortcutId: modelData.shortcutId
                        icon.name: modelData.id === Workspaces.activeId ? Theme.icons.check : ""
                        onTriggered: Workspaces.activeId = modelData.id
                    }

                    onObjectAdded: (index, object) => studioMenu.insertItem(index, object)
                    onObjectRemoved: (index, object) => studioMenu.removeItem(object)
                }
            }

            ThemedMenuSeparator { }

            ThemedMenuItem {
                text: qsTr("Customize layouts in Settings…")
                icon.name: Theme.icons.sliders
                onTriggered: Qt.callLater(() => ShellBridge.main.openSettings("workspaces"))
            }
        }
    }

    WorkspaceBackPill {
        anchors.verticalCenter: parent.verticalCenter
    }
}
