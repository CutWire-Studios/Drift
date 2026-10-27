pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import Drift
import "KeyframesModel.js" as Model

Item {
    id: root

    required property var panel

    property int hoverRow: -1
    property real hoverSeconds: 0

    readonly property alias list: laneList
    readonly property alias scrollBar: hBar
    readonly property real rowHeight: Theme.controlHeightSm + Theme.spacingMd
    readonly property var timeView: panel.timeView
    readonly property real scrollPadding: width * 0.25
    readonly property real scrollMin: timeView.clipStart * timeView.pxPerSecond - scrollPadding
    readonly property real scrollTotal: Math.max(1, timeView.clipDuration * timeView.pxPerSecond + 2 * scrollPadding)

    function maxContentY() {
        return Math.max(0, laneList.contentHeight - laneList.height)
    }

    function scrollBy(dy) {
        laneList.contentY = Math.max(0, Math.min(maxContentY(), laneList.contentY + dy))
    }

    function panBy(dx) {
        timeView.contentX += dx
    }

    ListView {
        id: laneList
        anchors.fill: parent
        clip: true
        interactive: false
        model: root.panel.rowKeys
        boundsBehavior: Flickable.StopAtBounds

        onContentYChanged: {
            if (root.panel.treeList.contentY !== contentY)
                root.panel.treeList.contentY = contentY
        }

        delegate: Item {
            id: lane

            required property int index

            readonly property var modelData: root.panel.rows[index] ?? ({})
            readonly property bool isGroup: modelData.type === "group"
            readonly property var dots: Model.laneDots(root.panel.rows, index)
            readonly property color dotColor: isGroup ? Theme.c.keyframesKeyDot.summary
                                                      : Theme.keyframeCurveColor(modelData.keyProp)

            width: laneList.width
            height: root.rowHeight

            Rectangle {
                anchors.fill: parent
                color: lane.isGroup ? Theme.c.keyframesLane.groupBackground
                                    : (lane.index % 2 === 1
                                       ? Qt.rgba(Theme.c.keyframesLane.backgroundAlt.r,
                                                 Theme.c.keyframesLane.backgroundAlt.g,
                                                 Theme.c.keyframesLane.backgroundAlt.b,
                                                 Theme.c.keyframesLane.opacityAlt)
                                       : "transparent")
            }

            Rectangle {
                readonly property real x0: root.timeView.xFor(root.timeView.clipStart)
                readonly property real x1: root.timeView.xFor(root.timeView.clipStart + root.timeView.clipDuration)
                x: Math.max(0, x0)
                width: Math.max(0, Math.min(lane.width, x1) - x)
                height: parent.height
                visible: !lane.isGroup
                color: Qt.rgba(Theme.c.keyframesLane.clipSpan.r, Theme.c.keyframesLane.clipSpan.g,
                               Theme.c.keyframesLane.clipSpan.b, Theme.c.keyframesLane.opacityClipSpan)
            }

            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: Theme.c.panelFrame.borderWidth
                color: Theme.c.keyframesLane.border
            }

            Repeater {
                model: lane.dots
                delegate: KeyframesKeyDot {
                    id: dot

                    required property var modelData

                    readonly property real cx: root.timeView.xFor(modelData.seconds)

                    visible: cx > -width && cx < lane.width + width
                    x: cx - width / 2
                    y: (lane.height - height) / 2
                    color: lane.dotColor
                    hold: modelData.hold
                    disabled: !lane.isGroup && !lane.modelData.enabled
                    selected: Model.refsSelected(root.panel.selection, modelData.refs)
                    hovered: root.hoverRow === lane.index
                             && Math.abs(root.hoverSeconds - modelData.seconds) < 1e-4
                }
            }
        }
    }

    Connections {
        target: root.panel.treeList

        function onContentYChanged() {
            if (laneList.contentY !== root.panel.treeList.contentY)
                laneList.contentY = root.panel.treeList.contentY
        }
    }

    AppScrollBar {
        id: hBar
        orientation: Qt.Horizontal
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        policy: ScrollBar.AsNeeded
        size: Math.min(1, root.width / root.scrollTotal)
        position: Math.max(0, Math.min(1 - size, (root.timeView.contentX - root.scrollMin) / root.scrollTotal))
        onPositionChanged: {
            if (pressed)
                root.timeView.contentX = root.scrollMin + position * root.scrollTotal
        }
    }
}
