pragma ComponentBehavior: Bound
import QtQuick
import Drift

// Corner overview of the whole graph with the visible area outlined. Press or drag to pan.
Item {
    id: root

    required property var canvas

    implicitWidth: 148
    implicitHeight: 96

    readonly property real margin: 80
    readonly property rect content: canvas.contentRect
    readonly property bool empty: content.width <= 0 || content.height <= 0
    readonly property rect view: canvas.viewRect
    readonly property rect bounds: {
        if (empty)
            return Qt.rect(0, 0, 1, 1)
        const x0 = Math.min(content.x, view.x) - margin
        const y0 = Math.min(content.y, view.y) - margin
        const x1 = Math.max(content.x + content.width, view.x + view.width) + margin
        const y1 = Math.max(content.y + content.height, view.y + view.height) + margin
        return Qt.rect(x0, y0, x1 - x0, y1 - y0)
    }
    readonly property real inset: 4
    readonly property real mapScale: Math.min((width - 2 * inset) / bounds.width,
                                              (height - 2 * inset) / bounds.height)
    readonly property real offX: (width - bounds.width * mapScale) / 2
    readonly property real offY: (height - bounds.height * mapScale) / 2

    Rectangle {
        anchors.fill: parent
        radius: Theme.c.nodeMinimap.radius
        color: Theme.c.nodeMinimap.background
        border.width: 1
        border.color: Theme.c.nodeMinimap.border
    }

    Text {
        anchors.centerIn: parent
        visible: root.empty
        text: qsTr("Empty")
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSizeXs
        color: Theme.c.nodeMinimap.foreground
    }

    Item {
        anchors.fill: parent
        visible: !root.empty
        clip: true

        Repeater {
            model: root.canvas.controller ? root.canvas.controller.nodes : null
            delegate: Rectangle {
                required property var model
                readonly property var item: {
                    void root.canvas.nodesRevision
                    return root.canvas.nodeItem(model.nodeId)
                }
                x: root.offX + (model.x - root.bounds.x) * root.mapScale
                y: root.offY + (model.y - root.bounds.y) * root.mapScale
                width: Math.max(2, model.width * root.mapScale)
                height: Math.max(2, (item ? item.height : 40) * root.mapScale)
                radius: 1
                color: Theme.c.nodeMinimap.node
            }
        }

        Rectangle {
            x: root.offX + (root.view.x - root.bounds.x) * root.mapScale
            y: root.offY + (root.view.y - root.bounds.y) * root.mapScale
            width: root.view.width * root.mapScale
            height: root.view.height * root.mapScale
            color: Qt.alpha(Theme.c.nodeMinimap.viewport, 0.16)
            border.width: 1.5
            border.color: Theme.c.nodeMinimap.viewport
        }
    }

    MouseArea {
        anchors.fill: parent
        enabled: !root.empty
        preventStealing: true

        function panTo(m) {
            root.canvas.centerOn(root.bounds.x + (m.x - root.offX) / root.mapScale,
                                 root.bounds.y + (m.y - root.offY) / root.mapScale)
        }

        onPressed: (m) => panTo(m)
        onPositionChanged: (m) => {
            if (pressed)
                panTo(m)
        }
    }
}
