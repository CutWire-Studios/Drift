pragma ComponentBehavior: Bound
import QtQuick
import Drift

// One node. Instantiated once per row of controller.nodes and kept alive through drags and value
// edits: position, inputs and selection arrive as per-role updates, and the row repeaters key on
// counts so editing a value never rebuilds the control being edited. Port rows sit at fixed
// offsets, so wires can compute endpoints from the model alone (portY).
Item {
    id: root

    required property var model
    required property var canvas

    readonly property var controller: canvas.controller
    readonly property string nodeId: model.nodeId
    readonly property var inputs: model.inputs
    readonly property var outputs: model.outputs
    readonly property bool selected: model.selected
    readonly property string error: model.error
    readonly property string thumbnail: model.thumbnail
    readonly property color accentColor: Theme.c.nodeDelegate.accent[model.accent] ?? Theme.c.nodeDelegate.accent.neutral

    readonly property real rowH: Theme.controlHeightSm
    readonly property real headerH: rowH + 2
    readonly property real pad: Theme.spacingLg
    readonly property real thumbH: thumbnail.length > 0 ? Math.round((width - 2 * Theme.spacingMd) * 0.5625) + Theme.spacingMd : 0
    readonly property real bodyTop: headerH + thumbH + Theme.spacingSm

    readonly property var portY: {
        const out = {}
        const ins = inputs
        const outs = outputs
        for (let i = 0; i < outs.length; ++i)
            out["o:" + outs[i].id] = bodyTop + (i + 0.5) * rowH
        for (let j = 0; j < ins.length; ++j)
            out["i:" + ins[j].id] = bodyTop + (outs.length + j + 0.5) * rowH
        return out
    }

    function portType(isOutput, id) {
        const list = isOutput ? outputs : inputs
        for (const p of list)
            if (p.id === id)
                return p.type
        return ""
    }

    x: model.x
    y: model.y
    width: model.width > 0 ? model.width : 220
    height: bodyTop + (outputs.length + inputs.length) * rowH + Theme.spacingSm
    z: selected ? 2 : 1

    onHeightChanged: if (controller) controller.reportNodeHeight(nodeId, height)
    Component.onCompleted: {
        if (controller)
            controller.reportNodeHeight(nodeId, height)
        canvas.noteNodesChanged()
    }
    Component.onDestruction: if (canvas) canvas.noteNodesChanged()

    Rectangle {
        id: card
        anchors.fill: parent
        radius: Theme.c.nodeDelegate.radius
        color: Theme.c.nodeDelegate.background
        border.width: 1
        border.color: Theme.c.nodeDelegate.border
    }

    Item {
        id: header
        width: parent.width
        height: root.headerH

        Rectangle {
            anchors.fill: parent
            radius: card.radius
            color: Theme.c.nodeDelegate.headerBackground
        }
        Rectangle {
            anchors.bottom: parent.bottom
            width: parent.width
            height: card.radius
            color: Theme.c.nodeDelegate.headerBackground
        }
        Rectangle {
            anchors.bottom: parent.bottom
            width: parent.width
            height: 1
            color: Theme.c.nodeDelegate.border
        }
        Rectangle {
            x: card.radius
            width: parent.width - 2 * card.radius
            height: 3
            radius: 1.5
            color: root.accentColor
        }

        Text {
            anchors.left: parent.left
            anchors.leftMargin: root.pad
            anchors.right: errorBadge.visible ? errorBadge.left : parent.right
            anchors.rightMargin: Theme.spacingMd
            anchors.verticalCenter: parent.verticalCenter
            text: root.model.title
            elide: Text.ElideRight
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeSm
            font.weight: Font.DemiBold
            color: Theme.c.nodeDelegate.foreground
        }

        IconGlyph {
            id: errorBadge
            visible: root.error.length > 0
            x: parent.width - width - Theme.spacingMd
            anchors.verticalCenter: parent.verticalCenter
            glyph: "triangle-alert"
            iconSize: Theme.iconSizeMd
            iconColor: Theme.c.nodeDelegate.error

            HoverHandler {
                id: badgeHover
            }
            ThemedToolTip {
                text: root.error
                visible: badgeHover.hovered
            }
        }
    }

    Rectangle {
        visible: root.thumbnail.length > 0
        x: Theme.spacingMd
        y: root.headerH + Theme.spacingMd
        width: parent.width - 2 * Theme.spacingMd
        height: root.thumbH - Theme.spacingMd
        radius: Theme.radiusSm
        color: Theme.c.nodeDelegate.thumbnailBackground
        clip: true

        Image {
            anchors.fill: parent
            source: root.thumbnail
            fillMode: Image.PreserveAspectFit
            asynchronous: true
            cache: false
        }
    }

    TapHandler {
        id: tap
        acceptedButtons: Qt.LeftButton
        onTapped: (point, button) => {
            root.canvas.forceActiveFocus()
            root.canvas.clearEdgeSelection()
            const additive = (tap.point.modifiers & (Qt.ShiftModifier | Qt.ControlModifier)) !== 0
            if (additive || !root.selected || root.controller.selection.length > 1)
                root.controller.select(root.nodeId, additive)
        }
        onDoubleTapped: root.canvas.nodeDoubleClicked(root.nodeId)
        onLongPressed: {
            if (tap.point.device.type !== PointerDevice.TouchScreen)
                return
            if (!root.selected)
                root.controller.select(root.nodeId, false)
            root.canvas.nodeMenuRequested(root.nodeId, root.mapToItem(root.canvas, tap.point.position.x, tap.point.position.y))
        }
    }

    TapHandler {
        acceptedButtons: Qt.RightButton
        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
        onTapped: (point) => {
            root.canvas.forceActiveFocus()
            root.canvas.clearEdgeSelection()
            if (!root.selected)
                root.controller.select(root.nodeId, false)
            root.canvas.nodeMenuRequested(root.nodeId, root.mapToItem(root.canvas, point.position.x, point.position.y))
        }
    }

    DragHandler {
        id: drag
        target: null
        acceptedButtons: Qt.LeftButton
        property point last
        property var ids: []

        onActiveChanged: {
            if (active) {
                root.canvas.forceActiveFocus()
                root.canvas.clearEdgeSelection()
                if (!root.selected)
                    root.controller.select(root.nodeId, false)
                ids = root.controller.selection
                last = centroid.scenePosition
            } else {
                root.controller.endMove()
            }
        }
        onCentroidChanged: {
            if (!active)
                return
            const p = centroid.scenePosition
            const dx = (p.x - last.x) / root.canvas.zoom
            const dy = (p.y - last.y) / root.canvas.zoom
            last = p
            if (dx !== 0 || dy !== 0)
                root.controller.moveNodes(ids, dx, dy)
        }
    }

    Repeater {
        model: root.outputs.length

        Item {
            id: outRow
            required property int index
            readonly property var port: root.outputs[index]
            y: root.bodyTop + index * root.rowH
            width: root.width
            height: root.rowH

            Text {
                anchors.right: parent.right
                anchors.rightMargin: root.pad
                anchors.verticalCenter: parent.verticalCenter
                width: parent.width - 2 * root.pad
                horizontalAlignment: Text.AlignRight
                text: outRow.port.label ?? outRow.port.id
                elide: Text.ElideLeft
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
                color: Theme.c.nodeDelegate.foregroundMuted
            }
        }
    }

    Repeater {
        model: root.inputs.length

        Item {
            id: inRow
            required property int index
            readonly property var port: root.inputs[index]
            readonly property bool hasControl: !port.connected && !port.exposed && control.kind !== ""
            y: root.bodyTop + (root.outputs.length + index) * root.rowH
            width: root.width
            height: root.rowH

            Text {
                id: inLabel
                x: root.pad
                anchors.verticalCenter: parent.verticalCenter
                width: inRow.hasControl ? Math.min(implicitWidth, root.width * 0.34) : parent.width - 2 * root.pad
                text: inRow.port.label ?? inRow.port.id
                elide: Text.ElideRight
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
                color: Theme.c.nodeDelegate.foregroundMuted
            }

            Rectangle {
                visible: !inRow.port.connected && !!inRow.port.exposed
                x: inLabel.x + Math.min(inLabel.implicitWidth, inLabel.width) + Theme.spacingMd
                anchors.verticalCenter: parent.verticalCenter
                width: Math.min(chipText.implicitWidth + 2 * Theme.spacingMd, root.width - x - root.pad)
                height: root.rowH - 8
                radius: height / 2
                color: Theme.c.nodeDelegate.chipBackground

                Text {
                    id: chipText
                    anchors.centerIn: parent
                    width: parent.width - 2 * Theme.spacingMd
                    horizontalAlignment: Text.AlignHCenter
                    text: inRow.port.exposed ?? ""
                    elide: Text.ElideRight
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeXs
                    color: Theme.c.nodeDelegate.chipForeground
                }
            }

            NodeInputControl {
                id: control
                visible: inRow.hasControl
                enabled: inRow.hasControl
                x: inLabel.x + Math.min(inLabel.implicitWidth, root.width * 0.34) + Theme.spacingMd
                y: 3
                width: root.width - x - root.pad
                height: root.rowH - 6
                port: inRow.port
                nodeId: root.nodeId
                controller: root.controller
            }
        }
    }

    Repeater {
        model: root.outputs.length

        PortItem {
            required property int index
            readonly property var info: root.outputs[index]
            canvas: root.canvas
            node: root
            isOutput: true
            portId: info.id
            portType: info.type
            connected: !!info.connected
            label: info.label ?? ""
            x: root.width - radius
            y: root.bodyTop + (index + 0.5) * root.rowH - radius
        }
    }

    Repeater {
        model: root.inputs.length

        PortItem {
            required property int index
            readonly property var info: root.inputs[index]
            canvas: root.canvas
            node: root
            isOutput: false
            visible: !info.hidePort
            portId: info.id
            portType: info.type
            connected: !!info.connected
            label: info.label ?? ""
            x: -radius
            y: root.bodyTop + (root.outputs.length + index + 0.5) * root.rowH - radius
        }
    }

    Rectangle {
        anchors.fill: parent
        radius: card.radius
        color: "transparent"
        border.width: 2
        border.color: root.error.length > 0 ? Theme.c.nodeDelegate.error : Theme.c.nodeDelegate.selection
        visible: root.selected || root.error.length > 0
        opacity: root.selected ? 1 : 0.7
    }
}
