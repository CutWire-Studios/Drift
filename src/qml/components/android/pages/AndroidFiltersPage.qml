import QtQuick
import QtQuick.Controls.Basic
import Drift
// Effects tab of the properties sheet: the clip's applied effects, or the effect browser to add more.
Item {
    id: root

    property var scaffold: null
    property int trackIndex: -1
    property int clipIndex: -1
    property bool showBrowse: false

    readonly property var tabList: [{ id: "applied", label: qsTr("Applied") }, { id: "browse", label: qsTr("Browse") }]

    AssetSearchBar {
        id: chips
        visible: !root.showBrowse
        width: parent.width
        tabs: root.tabList
        currentTabId: "applied"
        searchEnabled: false
        onTabSelected: tabId => root.showBrowse = tabId === "browse"
    }

    Flickable {
        id: appliedFlick
        anchors.top: root.showBrowse ? parent.top : chips.bottom
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        visible: !root.showBrowse
        clip: true
        contentWidth: width
        contentHeight: effectsInspector.height + Theme.pagePadding * 2
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: AppScrollBar { }

        EffectsInspector {
            id: effectsInspector
            x: Theme.pagePadding
            y: Theme.pagePadding
            width: appliedFlick.width - Theme.pagePadding * 2
            onBrowseEffectsRequested: root.showBrowse = true
        }
    }

    EffectBrowser {
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        visible: root.showBrowse
        headerTabs: root.tabList
        headerTabId: "browse"
        onHeaderTabSelected: tabId => root.showBrowse = tabId === "browse"
    }
}
