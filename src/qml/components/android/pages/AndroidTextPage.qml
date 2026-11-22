import QtQuick
import Drift
// Picking a text style adds a styled text clip at the playhead; picking another replaces it.
AndroidAddPage {
    id: page
    pickerHostsTabs: true

    propertiesComponent: Component {
        AndroidTextTabs {
            embedded: true
        }
    }

    TextAssetsTab {
        anchors.fill: parent
        pickerMode: true
        headerTabs: page.tabList
        headerTabId: page.currentTab
        onHeaderTabSelected: tabId => page.selectTab(tabId)
        onAdded: page.adopt()
    }
}
