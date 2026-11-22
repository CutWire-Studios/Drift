import QtQuick
import Drift
// Picking a sticker adds it at the playhead; picking another replaces it.
AndroidAddPage {
    id: page
    pickerHostsTabs: true

    propertiesComponent: Component {
        TransformInspector { }
    }

    StickersTab {
        anchors.fill: parent
        pickerMode: true
        headerTabs: page.tabList
        headerTabId: page.currentTab
        onHeaderTabSelected: tabId => page.selectTab(tabId)
        onAdded: page.adopt()
    }
}
