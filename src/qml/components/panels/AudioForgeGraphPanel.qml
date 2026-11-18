pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Layouts
import Drift

// The Audio Forge graph: document actions and the generic node editor bound to the AudioForge
// controller. Undo here is the Audio Forge's own history, never the project's.
PanelFrame {
    id: root
    SearchAnchor {
        searchId: "audio-forge-graph"
        title: qsTr("Audio Forge graph")
        keywords: "audio forge pedalboard graph pedal reverb delay filter split modulator build custom effect"
    }

    property bool shown: true

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

            Text {
                Layout.fillWidth: true
                text: AudioForge.docName + (AudioForge.dirty ? " •" : "")
                elide: Text.ElideRight
                color: Theme.c.forgePanel.foreground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeSm
            }

            IconButton {
                glyph: Theme.icons.plus
                tooltip: qsTr("New audio effect")
                onClicked: AudioForge.newDoc()
            }
            IconButton {
                id: openButton
                glyph: Theme.icons.folder
                tooltip: qsTr("Open a saved audio effect")
                onClicked: {
                    AudioForge.refreshRecent()
                    openMenu.popup(openButton, 0, openButton.height)
                }
            }
            IconButton {
                glyph: Theme.icons.undo
                tooltip: qsTr("Undo")
                enabled: AudioForge.canUndo
                onClicked: AudioForge.undo()
            }
            IconButton {
                glyph: Theme.icons.redo
                tooltip: qsTr("Redo")
                enabled: AudioForge.canRedo
                onClicked: AudioForge.redo()
            }
            ThemedButton {
                text: qsTr("Install")
                tooltip: qsTr("Install it into the audio effects browser")
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
        const r = AudioForge.install(overwrite)
        if (r.ok)
            Toasts.info(r.warning ? r.warning : qsTr("Installed “%1”").arg(AudioForge.docName))
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
            text: qsTr("“%1” is already installed. Replace it with this version?").arg(AudioForge.docName)
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
                                                 AudioForge.docId, "driftfx")
                if (url.toString().length > 0)
                    root.report(AudioForge.exportDriftfx(url), qsTr("Exported to %1"))
            })
        }
        ThemedMenuItem {
            visible: FileDialogs.supportsDirectoryPicker()
            text: qsTr("Export folder…")
            icon.name: Theme.icons.folderOutput
            onTriggered: Qt.callLater(() => {
                const url = FileDialogs.openDirectory(qsTr("Export folder"))
                if (url.toString().length > 0)
                    root.report(AudioForge.exportFolder(url), qsTr("Exported to %1"))
            })
        }
    }

    ThemedContextMenu {
        id: openMenu

        ThemedMenuItem {
            enabled: false
            visible: AudioForge.recentProjects.length === 0
            text: qsTr("No saved projects yet")
        }
        Instantiator {
            model: AudioForge.recentProjects
            delegate: ThemedMenuItem {
                required property var modelData
                text: modelData.name
                onTriggered: AudioForge.open(modelData.id)
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
        controller: AudioForge

        onNodeDoubleClicked: (id) => openInspector(id)
        onRequestInspector: (id) => openInspector(id)

        function openInspector(id) {
            AudioForge.select(id, false)
            ShellBridge.panelRequest = { "panel": "audioForgeInspector", "tab": "node" }
        }

        Connections {
            target: AudioForge

            function onAddNodeRequested(type) {
                const c = editor.canvas.toWorld(editor.canvas.width / 2, editor.canvas.height / 2)
                const id = AudioForge.addNode(type, Qt.point(c.x - 110, c.y - 40))
                if (id.length > 0)
                    AudioForge.select(id, false)
            }
        }
    }
}
