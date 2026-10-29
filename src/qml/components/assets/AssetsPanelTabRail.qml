import QtQuick
import Drift
// Vertical tab rail. Up/Down move between tabs once it has focus.
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

    width: sheetMode ? 0 : Theme.tabRailWidth
    height: parent ? parent.height : 0
    visible: !sheetMode
    vertical: true

    Accessible.role: Accessible.PageTabList

    Keys.onUpPressed: function(event) {
        root.tabSelected((root.activeTab - 1 + root.tabCount) % root.tabCount)
        event.accepted = true
    }
    Keys.onDownPressed: function(event) {
        root.tabSelected((root.activeTab + 1) % root.tabCount)
        event.accepted = true
    }

    Column {
        id: tabRailColumn
        width: parent.width
        topPadding: Theme.spacingSm
        spacing: Theme.spacingXs

        Repeater {
            model: root.tabs
            delegate: AssetsPanelTabButton {
                activeTab: root.activeTab
                tabLabels: root.tabLabels
                tabIcons: root.tabIcons
                onTabSelected: (index) => root.tabSelected(index)
            }
        }
    }
}
