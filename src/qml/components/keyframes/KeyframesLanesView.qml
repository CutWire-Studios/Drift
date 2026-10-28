pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import Drift

Item {
    id: root

    required property var panel

    property int hoverRow: -1
    property real hoverSeconds: 0
    property real contentY: 0

    readonly property alias scrollBar: hBar
    readonly property real rowHeight: Theme.controlHeightSm + Theme.spacingMd
    readonly property var timeView: panel.timeView
    readonly property real scrollPadding: width * 0.25
    readonly property real scrollMin: timeView.clipStart * timeView.pxPerSecond - scrollPadding
    readonly property real scrollTotal: Math.max(1, timeView.clipDuration * timeView.pxPerSecond + 2 * scrollPadding)

    onContentYChanged: {
        if (panel.treeList.contentY !== contentY)
            panel.treeList.contentY = contentY
    }

    function maxContentY() {
        return Math.max(0, panel.rowsModel.count * rowHeight - height)
    }

    function scrollBy(dy) {
        contentY = Math.max(0, Math.min(maxContentY(), contentY + dy))
    }

    function panBy(dx) {
        timeView.contentX += dx
    }

    KeyframeLanesItem {
        anchors.fill: parent
        rows: root.panel.rowsModel
        selection: root.panel.selection
        timeView: root.timeView
        controller: EditorState.keyframes
        contentY: root.contentY
        rowHeight: root.rowHeight
        hoverRow: root.panel.hoverRow
        dotHoverRow: root.hoverRow
        dotHoverSeconds: root.hoverSeconds
        borderWidth: Theme.c.panelFrame.borderWidth
        animationDuration: Theme.durationFast
        laneStyle: Theme.plainMap(Theme.c.keyframesLane)
        dotStyle: Theme.plainMap(Theme.c.keyframesKeyDot)
    }

    Connections {
        target: root.panel.treeList

        function onContentYChanged() {
            if (root.contentY !== root.panel.treeList.contentY)
                root.contentY = root.panel.treeList.contentY
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
