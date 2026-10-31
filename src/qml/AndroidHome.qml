import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Drift

// The home screen when no project is open: a top bar with the app-level actions, three tabs
// (Home, Projects, My videos) and the tab bar below them.
Item {
    id: root

    signal enterEditor()
    signal newProjectRequested()
    signal quickActionRequested(string actionId)
    signal openProjectRequested()
    signal openRecentRequested(string path)

    property int currentTab: 0

    Rectangle {
        anchors.fill: parent
        color: Theme.c.androidToolPage.background
    }

    Item {
        id: topBar
        width: parent.width
        height: Theme.androidTopBarHeight + SafeArea.margins.top

        Rectangle {
            anchors.fill: parent
            color: Theme.c.androidTopBar.background
        }

        Rectangle {
            anchors.bottom: parent.bottom
            width: parent.width
            height: Theme.c.androidTopBar.borderWidth
            color: Theme.c.androidTopBar.border
        }

        Item {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.leftMargin: SafeArea.margins.left
            anchors.rightMargin: SafeArea.margins.right
            height: Theme.androidTopBarHeight

            Text {
                anchors.left: parent.left
                anchors.leftMargin: Theme.androidPagePadding
                anchors.verticalCenter: parent.verticalCenter
                text: "Drift"
                color: Theme.c.androidHomeWordmark.foreground
                font.family: Theme.fontFamily
                font.pixelSize: 22
                font.weight: Font.Bold
            }

            Row {
                anchors.right: parent.right
                anchors.rightMargin: Theme.spacingSm
                anchors.verticalCenter: parent.verticalCenter
                spacing: Theme.spacingXs

                IconButton {
                    visible: Updates.updateAvailable
                    buttonSize: Theme.androidIconButtonSize
                    iconSize: Theme.iconSizeLg
                    glyph: Theme.icons.download
                    variant: "text"
                    tooltip: qsTr("Update available")
                    onClicked: ShellBridge.main.openUpdateDialog()

                    AndroidAttentionDot {
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: Theme.spacingSm
                    }
                }

                IconButton {
                    buttonSize: Theme.androidIconButtonSize
                    iconSize: Theme.iconSizeLg
                    glyph: Theme.icons.store
                    variant: "text"
                    tooltip: qsTr("Marketplace")
                    onClicked: ShellBridge.main.openMarketplace()
                }

                IconButton {
                    buttonSize: Theme.androidIconButtonSize
                    iconSize: Theme.iconSizeLg
                    glyph: Theme.icons.puzzle
                    variant: "text"
                    tooltip: qsTr("Extras")
                    onClicked: ShellBridge.main.openExtras()

                    AndroidAttentionDot {
                        visible: ShellBridge.main?.addonAttentionNeeded
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: Theme.spacingSm
                    }
                }

                IconButton {
                    buttonSize: Theme.androidIconButtonSize
                    iconSize: Theme.iconSizeLg
                    glyph: Theme.icons.settings
                    variant: "text"
                    tooltip: qsTr("Settings")
                    onClicked: ShellBridge.main.openSettings()
                }
            }
        }
    }

    StackLayout {
        anchors.top: topBar.bottom
        anchors.bottom: tabBar.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: SafeArea.margins.left
        anchors.rightMargin: SafeArea.margins.right
        currentIndex: root.currentTab

        AndroidHomeTab {
            onNewProjectRequested: root.newProjectRequested()
            onQuickActionRequested: (actionId) => root.quickActionRequested(actionId)
            onOpenRecentRequested: (path) => root.openRecentRequested(path)
            onSeeAllRequested: root.currentTab = 1
        }

        AndroidProjectsPage {
            onNewProjectRequested: root.newProjectRequested()
            onOpenProjectRequested: root.openProjectRequested()
            onOpenRecentRequested: (path) => root.openRecentRequested(path)
        }

        AndroidMyVideosTab { }
    }

    Item {
        id: tabBar
        anchors.bottom: parent.bottom
        width: parent.width
        height: Theme.androidTopBarHeight + SafeArea.margins.bottom

        Rectangle {
            anchors.fill: parent
            color: Theme.c.androidHomeTabBar.background
        }

        Rectangle {
            width: parent.width
            height: Theme.c.androidHomeTabBar.borderWidth
            color: Theme.c.androidHomeTabBar.border
        }

        Row {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.leftMargin: SafeArea.margins.left
            anchors.rightMargin: SafeArea.margins.right
            height: Theme.androidTopBarHeight

            Repeater {
                model: [
                    { label: qsTr("Home"), icon: Theme.icons.layoutGrid },
                    { label: qsTr("Projects"), icon: Theme.icons.folder },
                    { label: qsTr("My videos"), icon: Theme.icons.video }
                ]

                delegate: AbstractButton {
                    id: tab
                    required property var modelData
                    required property int index

                    readonly property bool active: root.currentTab === tab.index
                    readonly property color tint: tab.active ? Theme.c.androidHomeTabBar.active
                                                             : Theme.c.androidHomeTabBar.inactive

                    width: parent.width / 3
                    height: parent.height

                    Accessible.role: Accessible.PageTab
                    Accessible.name: tab.modelData.label

                    onClicked: {
                        Haptics.select()
                        root.currentTab = tab.index
                    }

                    contentItem: Item {
                        Column {
                            anchors.centerIn: parent
                            spacing: 2

                            IconGlyph {
                                anchors.horizontalCenter: parent.horizontalCenter
                                glyph: tab.modelData.icon
                                iconSize: Theme.iconSizeLg
                                iconColor: tab.tint
                            }

                            Text {
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: tab.modelData.label
                                color: tab.tint
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontSizeXs
                                font.weight: tab.active ? Font.Medium : Font.Normal
                            }
                        }
                    }
                }
            }
        }
    }
}
