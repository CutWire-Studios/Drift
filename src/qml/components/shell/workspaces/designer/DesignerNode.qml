pragma ComponentBehavior: Bound
import QtQuick
import Drift

// A NODE of the draft tree: a slot block, or a split laying its children out by `sizes`
// with draggable gaps between them. Sizes are only committed when a gap drag ends, so the
// tree is not rebuilt under the pointer.
Item {
    id: root

    required property var designer
    required property var node
    property var path: []

    readonly property bool isSplit: !!node && node.split !== undefined
    readonly property bool horizontal: isSplit && node.split === "h"
    readonly property int count: isSplit ? node.children.length : 0
    readonly property real gap: 6
    readonly property real extent: Math.max(1, (horizontal ? width : height) - gap * Math.max(0, count - 1))
    property var live: null
    readonly property var sizes: live ?? (isSplit ? node.sizes : [])

    function childOffset(i) {
        let offset = 0
        for (let k = 0; k < i; ++k)
            offset += sizes[k] * extent + gap
        return offset
    }

    Loader {
        anchors.fill: parent
        active: !root.isSplit && !!root.node
        sourceComponent: DesignerSlot {
            designer: root.designer
            node: root.node
            path: root.path
        }
    }

    Repeater {
        model: root.isSplit ? root.node.children : []

        // Loaded by URL: a component naming itself inline is rejected as recursive. The
        // initial values never change for a delegate, since a new children array rebuilds them.
        Loader {
            required property var modelData
            required property int index

            Component.onCompleted: setSource("DesignerNode.qml", {
                designer: root.designer,
                node: modelData,
                path: root.path.concat([index])
            })
            x: root.horizontal ? root.childOffset(index) : 0
            y: root.horizontal ? 0 : root.childOffset(index)
            width: root.horizontal ? root.sizes[index] * root.extent : root.width
            height: root.horizontal ? root.height : root.sizes[index] * root.extent
        }
    }

    Repeater {
        model: Math.max(0, root.count - 1)

        Item {
            id: handle

            required property int index
            property var startSizes: []

            x: root.horizontal ? root.childOffset(index + 1) - root.gap : 0
            y: root.horizontal ? 0 : root.childOffset(index + 1) - root.gap
            width: root.horizontal ? root.gap : root.width
            height: root.horizontal ? root.height : root.gap

            Rectangle {
                anchors.centerIn: parent
                width: root.horizontal ? 2 : Math.min(32, parent.width)
                height: root.horizontal ? Math.min(32, parent.height) : 2
                radius: Theme.radiusPill
                color: grab.pressed || grab.containsMouse ? Theme.accent : Theme.panelBorder
            }

            MouseArea {
                id: grab
                anchors.fill: parent
                anchors.margins: -2
                preventStealing: true
                hoverEnabled: true
                cursorShape: root.horizontal ? Qt.SplitHCursor : Qt.SplitVCursor
                onPressed: handle.startSizes = root.sizes.slice()
                onPositionChanged: (mouse) => {
                    if (!pressed)
                        return
                    const p = mapToItem(root, mouse.x, mouse.y)
                    const pos = root.horizontal ? p.x : p.y
                    const before = root.childOffset(handle.index)
                    const total = handle.startSizes[handle.index] + handle.startSizes[handle.index + 1]
                    const min = root.designer.minSize
                    const first = Math.max(min, Math.min(total - min, (pos - before) / root.extent))
                    const next = handle.startSizes.slice()
                    next[handle.index] = first
                    next[handle.index + 1] = total - first
                    root.live = next
                }
                onReleased: {
                    if (root.live)
                        root.designer.commitSizes(root.path, root.live)
                    root.live = null
                }
                onCanceled: root.live = null
            }
        }
    }
}
