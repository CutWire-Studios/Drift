import QtQuick
import Drift
// Vertical tab rail that reflows into more columns as it widens. Arrows move between tabs once it has focus.
// Scrolls so short panel heights can still reach lower icons.
// Hidden in sheetMode — AndroidBottomRail drives showTab() instead.
ScrollStrip {
    id: root

    property bool sheetMode: false
    property int activeTab: 0
    property int tabCount: 0
    property var tabLabels: ({})
    property var tabIcons: []
    property var tabs: null

    signal tabSelected(int index)

    property real railWidth: Theme.tabRailWidth
    readonly property int columns: Math.max(1, Math.floor(railWidth / Theme.tabRailWidth))

    width: sheetMode ? 0 : railWidth
    height: parent ? parent.height : 0
    visible: !sheetMode
    vertical: true

    Accessible.role: Accessible.PageTabList

    function step(delta) {
        const next = root.activeTab + delta
        if (next >= 0 && next < root.tabCount)
            root.tabSelected(next)
        else if (root.columns === 1 && Math.abs(delta) === 1)
            root.tabSelected((next + root.tabCount) % root.tabCount)
    }

    function revealActive() {
        const item = tabRepeater.itemAt(root.activeTab)
        if (!item)
            return
        const top = tabRailFlow.y + item.y
        const bottom = top + item.height
        if (top < root._pos)
            root.scrollTo(top - Theme.spacingSm)
        else if (bottom > root._pos + root.height)
            root.scrollTo(bottom - root.height + Theme.spacingSm)
    }

    onActiveTabChanged: Qt.callLater(revealActive)

    Keys.onLeftPressed: function(event) {
        root.step(-1)
        event.accepted = true
    }
    Keys.onRightPressed: function(event) {
        root.step(1)
        event.accepted = true
    }
    Keys.onUpPressed: function(event) {
        root.step(root.columns === 1 ? -1 : -root.columns)
        event.accepted = true
    }
    Keys.onDownPressed: function(event) {
        root.step(root.columns === 1 ? 1 : root.columns)
        event.accepted = true
    }

    Flow {
        id: tabRailFlow
        y: Theme.spacingSm
        width: parent.width
        spacing: 0

        Repeater {
            id: tabRepeater
            model: root.tabs
            delegate: AssetsPanelTabButton {
                width: tabRailFlow.width / root.columns
                bottomPadding: Theme.spacingXs
                showSeparator: root.columns === 1
                activeTab: root.activeTab
                tabLabels: root.tabLabels
                tabIcons: root.tabIcons
                onTabSelected: (index) => root.tabSelected(index)
            }
        }
    }
}
