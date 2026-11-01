import QtQuick
import Drift
// One icon in the assets panel's vertical tab rail, plus the group divider under it.
Column {
    id: root

    required property int index
    required property var model

    property int activeTab: 0
    property var tabLabels: ({})
    property var tabIcons: []
    property bool showSeparator: true

    signal tabSelected(int index)

    width: parent ? parent.width : 0
    spacing: 0

    ThemeSlot {
        width: root.width
        slot: "AssetsPanelTabButton"
        control: root
        fallback: Component {
            Column {
                width: root.width
                spacing: 0

                IconButton {
                    anchors.horizontalCenter: parent.horizontalCenter
                    glyph: root.tabIcons[root.model.icon]
                    variant: "ghost"
                    tooltip: root.tabLabels[root.model.tabId]
                    active: root.activeTab === root.index
                    onClicked: root.tabSelected(root.index)

                    Accessible.role: Accessible.PageTab
                    Accessible.name: root.tabLabels[root.model.tabId]
                    Accessible.checked: root.activeTab === root.index
                }

                // Group divider — sits in the rail gap so related tabs
                // cluster and prefs stay visually apart from content.
                Item {
                    visible: root.showSeparator && root.model.separatorAfter
                    width: parent.width
                    height: visible ? Theme.spacingLg + Theme.c.assetsPanelTabButton.separatorThickness : 0

                    Rectangle {
                        anchors.horizontalCenter: parent.horizontalCenter
                        anchors.verticalCenter: parent.verticalCenter
                        width: Theme.iconSizeSm
                        height: Theme.c.assetsPanelTabButton.separatorThickness
                        radius: Theme.c.assetsPanelTabButton.separatorRadius
                        color: Theme.c.assetsPanelTabButton.separator
                        opacity: Theme.c.assetsPanelTabButton.separatorOpacity
                    }
                }
            }
        }
    }
}
