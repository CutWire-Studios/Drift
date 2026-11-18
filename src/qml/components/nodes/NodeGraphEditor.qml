pragma ComponentBehavior: Bound
import QtQuick
import Drift

// The generic node graph editor: canvas, zoom controls, minimap and the menus. It knows nothing
// about what the nodes mean; everything goes through `controller`, a NodeGraphController
// subclass supplied by the workspace that hosts it.
Item {
    id: root

    property NodeGraphController controller: null
    property bool minimapVisible: true
    readonly property alias canvas: nodeCanvas

    // A node was double-clicked or double-tapped.
    signal nodeDoubleClicked(string id)
    // The user asked for the node's inspector (menu entry, or Enter on a single selected node).
    signal requestInspector(string id)

    function openQuickAdd(screenPos, worldPos, wire) {
        quickAdd.openAt(screenPos.x, screenPos.y, worldPos, wire)
    }

    NodeCanvas {
        id: nodeCanvas
        anchors.fill: parent
        controller: root.controller

        onNodeDoubleClicked: (id) => root.nodeDoubleClicked(id)
        onQuickAddRequested: (screenPos, worldPos, wire) => root.openQuickAdd(screenPos, worldPos, wire)
        onNodeMenuRequested: (id, screenPos) => {
            nodeMenu.nodeId = id
            nodeMenu.actions = root.controller.nodeContextActions(id)
            nodeMenu.deletable = root.controller.nodes.get(root.controller.nodes.rowOfNode(id)).deletable !== false
            nodeMenu.popup(nodeCanvas, screenPos.x, screenPos.y)
        }
        onContextMenuRequested: (screenPos, worldPos, edgeId) => {
            backgroundMenu.worldPos = worldPos
            backgroundMenu.screenPos = screenPos
            backgroundMenu.edgeId = edgeId
            backgroundMenu.popup(nodeCanvas, screenPos.x, screenPos.y)
        }
        Keys.onReturnPressed: (event) => {
            if (root.controller && root.controller.selection.length === 1)
                root.requestInspector(root.controller.selection[0])
        }
    }

    Row {
        id: controls
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        anchors.margins: Theme.spacingLg
        spacing: Theme.spacingXs

        Rectangle {
            width: controlsRow.width + 2 * Theme.spacingXs
            height: controlsRow.height + 2 * Theme.spacingXs
            radius: Theme.c.nodeGraphEditor.radius
            color: Theme.c.nodeGraphEditor.controlsBackground
            border.width: 1
            border.color: Theme.c.nodeGraphEditor.controlsBorder

            Row {
                id: controlsRow
                anchors.centerIn: parent

                IconButton {
                    glyph: Theme.icons.minus
                    tooltip: qsTr("Zoom out")
                    onClicked: nodeCanvas.zoomCentered(1 / 1.25)
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    width: 44
                    horizontalAlignment: Text.AlignHCenter
                    text: Math.round(nodeCanvas.zoom * 100) + "%"
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeXs
                    color: Theme.c.nodeGraphEditor.foreground
                }
                IconButton {
                    glyph: "plus"
                    tooltip: qsTr("Zoom in")
                    onClicked: nodeCanvas.zoomCentered(1.25)
                }
                IconButton {
                    glyph: "maximize"
                    tooltip: qsTr("Fit all")
                    onClicked: nodeCanvas.fitAll()
                }
                IconButton {
                    glyph: Theme.icons.map
                    tooltip: qsTr("Minimap")
                    active: root.minimapVisible
                    onClicked: root.minimapVisible = !root.minimapVisible
                }
            }
        }
    }

    NodeMinimap {
        visible: root.minimapVisible
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: Theme.spacingLg
        canvas: nodeCanvas
    }

    QuickAddPopup {
        id: quickAdd
        parent: root
        canvas: nodeCanvas
        controller: root.controller
    }

    ThemedContextMenu {
        id: backgroundMenu

        property point worldPos: Qt.point(0, 0)
        property point screenPos: Qt.point(0, 0)
        property string edgeId: ""

        ThemedMenuItem {
            text: qsTr("Add node…")
            onTriggered: root.openQuickAdd(backgroundMenu.screenPos, backgroundMenu.worldPos, null)
        }
        ThemedMenuItem {
            text: qsTr("Paste")
            onTriggered: root.controller.paste(backgroundMenu.worldPos)
        }
        ThemedMenuItem {
            visible: backgroundMenu.edgeId !== ""
            text: qsTr("Disconnect")
            onTriggered: nodeCanvas.deleteSelection()
        }
        ThemedMenuSeparator {}
        ThemedMenuItem {
            text: qsTr("Select all")
            onTriggered: root.controller.selectAll()
        }
        ThemedMenuItem {
            text: qsTr("Fit all")
            onTriggered: nodeCanvas.fitAll()
        }
        ThemedMenuItem {
            enabled: !!root.controller && root.controller.canUndo
            text: qsTr("Undo")
            onTriggered: root.controller.undo()
        }
        ThemedMenuItem {
            enabled: !!root.controller && root.controller.canRedo
            text: qsTr("Redo")
            onTriggered: root.controller.redo()
        }
    }

    ThemedContextMenu {
        id: nodeMenu

        property string nodeId: ""
        property var actions: []
        property bool deletable: true

        ThemedMenuItem {
            text: qsTr("Open inspector")
            onTriggered: root.requestInspector(nodeMenu.nodeId)
        }
        Instantiator {
            model: nodeMenu.actions
            delegate: ThemedMenuItem {
                required property var modelData
                text: modelData.label
                enabled: modelData.enabled !== false
                onTriggered: root.controller.triggerNodeAction(nodeMenu.nodeId, modelData.id)
            }
            onObjectAdded: (i, object) => nodeMenu.insertItem(i + 1, object)
            onObjectRemoved: (i, object) => nodeMenu.removeItem(object)
        }
        ThemedMenuSeparator {}
        ThemedMenuItem {
            text: qsTr("Copy")
            onTriggered: root.controller.copySelection()
        }
        ThemedMenuItem {
            text: qsTr("Duplicate")
            onTriggered: root.controller.duplicateSelection()
        }
        ThemedMenuItem {
            enabled: nodeMenu.deletable
            text: qsTr("Delete")
            onTriggered: root.controller.deleteSelection()
        }
    }
}
