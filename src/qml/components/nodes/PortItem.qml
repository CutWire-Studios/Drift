pragma ComponentBehavior: Bound
import QtQuick
import Drift

// One connection port, centred on (x + radius, y + radius) of its node. The canvas owns the
// connect state machine; this item reports presses and drags and draws the compatibility hints.
Item {
    id: root

    required property var canvas
    required property var node
    required property bool isOutput
    required property string portId
    required property string portType
    property bool connected: false
    property string label: ""

    readonly property real radius: 4.5
    readonly property string key: node.nodeId + (isOutput ? ":o:" : ":i:") + portId
    readonly property bool hovered: canvas.hoverPortKey === key
    readonly property bool pending: canvas.pending !== null
    readonly property bool compatible: {
        void canvas.pendingRevision
        return pending && canvas.canFinishAt(node.nodeId, isOutput, portId)
    }
    readonly property bool incompatible: pending && !compatible && !isSource
    readonly property bool isSource: pending && canvas.pending.node === node.nodeId
                                     && canvas.pending.isOutput === isOutput && canvas.pending.port === portId
    readonly property color hue: Theme.c.portItem[portType] ?? Theme.c.portItem.other

    width: radius * 2
    height: radius * 2

    Rectangle {
        anchors.centerIn: parent
        width: root.hovered && root.compatible ? 14 : (root.compatible ? 12 : parent.width)
        height: width
        radius: width / 2
        color: "transparent"
        border.width: 2
        border.color: root.hue
        opacity: root.compatible ? 0.8 : 0
        Behavior on width { NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing } }
    }

    Rectangle {
        anchors.fill: parent
        radius: width / 2
        color: root.connected || root.hovered || root.isSource ? root.hue : Theme.c.portItem.fill
        border.width: 1.5
        border.color: root.hue
        opacity: root.incompatible ? 0.3 : 1
        Behavior on opacity { NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing } }
    }

    MouseArea {
        id: area
        anchors.centerIn: parent
        width: Theme.touchInput ? 32 : 20
        height: width
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton
        cursorShape: Qt.CrossCursor
        preventStealing: true

        property bool moved: false
        property point pressWorld: Qt.point(0, 0)

        function worldPos(m) {
            return mapToItem(root.canvas.world, m.x, m.y)
        }

        onPressed: (m) => {
            root.canvas.forceActiveFocus()
            moved = false
            pressWorld = worldPos(m)
        }
        onPositionChanged: (m) => {
            if (!pressed)
                return
            const w = worldPos(m)
            if (!moved) {
                if (Math.hypot(w.x - pressWorld.x, w.y - pressWorld.y) * root.canvas.zoom < 6)
                    return
                moved = true
                root.canvas.portDragBegan(root.node.nodeId, root.isOutput, root.portId, root.portType, pressWorld)
            }
            root.canvas.portDragMoved(w)
        }
        onReleased: (m) => {
            if (moved)
                root.canvas.portDragEnded(worldPos(m))
            else
                root.canvas.portTapped(root.node.nodeId, root.isOutput, root.portId, root.portType)
            moved = false
        }
        onCanceled: {
            if (moved)
                root.canvas.cancelPending()
            moved = false
        }

        ThemedToolTip {
            visible: area.containsMouse && !area.pressed && !root.pending && !Theme.touchInput
            text: (root.label.length > 0 ? root.label + " - " : "") + root.portType
        }
    }
}
