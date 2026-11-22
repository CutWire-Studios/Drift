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
        radius: Theme.radiusPill
        color: "transparent"
        border.width: Theme.c.portItem.ringBorderWidth
        border.color: root.hue
        opacity: root.compatible ? 0.8 : 0
        Behavior on width { NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing } }
    }

    Rectangle {
        anchors.fill: parent
        radius: Theme.radiusPill
        color: root.connected || root.hovered || root.isSource ? root.hue : Theme.c.portItem.fill
        border.width: Theme.c.portItem.borderWidth
        border.color: root.hue
        opacity: root.incompatible ? 0.3 : 1
        Behavior on opacity { NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing } }
    }

    HandleKnob {
        id: knob
        anchors.centerIn: parent
        shape: "none"
        cursor: Qt.CrossCursor
        space: root.canvas.world

        property bool travelled: false
        property point pressWorld: Qt.point(0, 0)
        releaseOnCancel: false

        onCanceled: {
            if (travelled)
                root.canvas.cancelPending()
            travelled = false
        }

        onPressed: (p) => {
            root.canvas.forceActiveFocus()
            travelled = false
            pressWorld = p
        }
        onMoved: (p) => {
            if (!travelled) {
                if (Math.hypot(p.x - pressWorld.x, p.y - pressWorld.y) * root.canvas.zoom < 6)
                    return
                travelled = true
                root.canvas.portDragBegan(root.node.nodeId, root.isOutput, root.portId, root.portType, pressWorld)
            }
            root.canvas.portDragMoved(p)
        }
        onReleased: (p) => {
            if (travelled)
                root.canvas.portDragEnded(p)
            else
                root.canvas.portTapped(root.node.nodeId, root.isOutput, root.portId, root.portType)
            travelled = false
        }
    }

    ThemedToolTip {
        visible: knob.hovered && !knob.active && !root.pending && !Theme.touchInput
        text: (root.label.length > 0 ? root.label + " - " : "") + root.portType
    }
}
