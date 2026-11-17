pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Drift

// The Forge node graph: mode switch, document actions and the generic node editor bound to the
// Forge controller. Undo here is the Forge's own history, never the project's.
PanelFrame {
    id: root
    SearchAnchor {
        searchId: "forge-graph"
        title: qsTr("Forge node graph")
        keywords: "forge effect transition animation node graph shader glsl build custom"
    }

    property bool shown: true

    readonly property var kinds: [
        { "id": "effect", "label": qsTr("Effect") },
        { "id": "transition", "label": qsTr("Transition") },
        { "id": "animation", "label": qsTr("Animation") }
    ]

    Rectangle {
        id: header
        width: parent.width
        height: Theme.panelHeaderHeight
        color: Theme.c.forgePanel.headerBackground

        Rectangle {
            anchors.bottom: parent.bottom
            width: parent.width
            height: Theme.c.forgePanel.borderWidth
            color: Theme.c.forgePanel.border
        }

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: Theme.spacingMd
            anchors.rightMargin: Theme.spacingMd
            spacing: Theme.spacingSm

            Repeater {
                model: root.kinds

                ThemedChip {
                    required property var modelData
                    text: modelData.label
                    selected: Forge.kind === modelData.id
                    variant: "outline"
                    onClicked: Forge.kind = modelData.id
                }
            }

            Text {
                Layout.fillWidth: true
                Layout.leftMargin: Theme.spacingMd
                text: Forge.docName + (Forge.dirty ? " •" : "")
                elide: Text.ElideRight
                color: Theme.c.forgePanel.foreground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeSm
            }

            IconButton {
                glyph: Theme.icons.plus
                tooltip: qsTr("New %1").arg(Forge.kind)
                onClicked: Forge.newDoc(Forge.kind)
            }
            IconButton {
                id: openButton
                glyph: Theme.icons.folder
                tooltip: qsTr("Open a saved Forge project")
                onClicked: {
                    Forge.refreshRecent()
                    openMenu.popup(openButton, 0, openButton.height)
                }
            }
            IconButton {
                glyph: Theme.icons.undo
                tooltip: qsTr("Undo")
                enabled: Forge.canUndo
                onClicked: Forge.undo()
            }
            IconButton {
                glyph: Theme.icons.redo
                tooltip: qsTr("Redo")
                enabled: Forge.canRedo
                onClicked: Forge.redo()
            }
        }
    }

    ThemedContextMenu {
        id: openMenu

        ThemedMenuItem {
            enabled: false
            visible: Forge.recentProjects.length === 0
            text: qsTr("No saved projects yet")
        }
        Instantiator {
            model: Forge.recentProjects
            delegate: ThemedMenuItem {
                required property var modelData
                text: modelData.name + "  (" + modelData.kind + ")"
                onTriggered: Forge.open(modelData.id)
            }
            onObjectAdded: (i, object) => openMenu.insertItem(i + 1, object)
            onObjectRemoved: (i, object) => openMenu.removeItem(object)
        }
    }

    NodeGraphEditor {
        id: editor
        anchors.top: header.bottom
        anchors.bottom: parent.bottom
        width: parent.width
        controller: Forge

        onNodeDoubleClicked: (id) => openInspector(id)
        onRequestInspector: (id) => openInspector(id)

        function openInspector(id) {
            Forge.select(id, false)
            ShellBridge.panelRequest = { "panel": "forgeInspector", "tab": "node" }
        }

        Connections {
            target: Forge

            function onAddNodeRequested(type) {
                const c = editor.canvas.toWorld(editor.canvas.width / 2, editor.canvas.height / 2)
                const id = Forge.addNode(type, Qt.point(c.x - 110, c.y - 40))
                if (id.length > 0)
                    Forge.select(id, false)
            }
        }
    }
}
