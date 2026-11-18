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
            ThemedButton {
                text: qsTr("Install")
                tooltip: qsTr("Install it into the effect, transition or animation browsers")
                onClicked: root.install(false)
            }
            IconButton {
                id: moreButton
                glyph: Theme.icons.ellipsis
                tooltip: qsTr("Export")
                onClicked: moreMenu.popup(moreButton, 0, moreButton.height)
            }
        }
    }

    function install(overwrite) {
        const r = Forge.install(overwrite)
        if (r.ok)
            Toasts.info(r.warning ? r.warning : qsTr("Installed “%1”").arg(Forge.docName))
        else if (r.code === "exists")
            replaceDialog.open()
        else
            Toasts.error(r.error)
    }

    function report(r, okText) {
        if (r.ok)
            Toasts.info(okText.arg(r.path))
        else
            Toasts.error(r.error)
    }

    ThemedDialog {
        id: replaceDialog
        title: qsTr("Replace existing?")
        acceptText: qsTr("Replace")
        preferredWidth: Theme.dialogWidthSm
        onAccepted: root.install(true)

        contentItem: Text {
            width: parent ? parent.width : 320
            wrapMode: Text.WordWrap
            text: qsTr("“%1” is already installed. Replace it with this version?").arg(Forge.docName)
            color: Theme.c.forgePanel.foreground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeSm
        }
    }

    ThemedContextMenu {
        id: moreMenu

        ThemedMenuItem {
            text: qsTr("Export .driftfx…")
            icon.name: Theme.icons.upload
            onTriggered: Qt.callLater(() => {
                const url = FileDialogs.saveFile(qsTr("Export .driftfx"), [qsTr("Drift package (*.driftfx)")],
                                                 Forge.docId, "driftfx")
                if (url.toString().length > 0)
                    root.report(Forge.exportDriftfx(url), qsTr("Exported to %1"))
            })
        }
        ThemedMenuItem {
            visible: FileDialogs.supportsDirectoryPicker()
            text: qsTr("Export folder…")
            icon.name: Theme.icons.folderOutput
            onTriggered: Qt.callLater(() => {
                const url = FileDialogs.openDirectory(qsTr("Export folder"))
                if (url.toString().length > 0)
                    root.report(Forge.exportFolder(url), qsTr("Exported to %1"))
            })
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
