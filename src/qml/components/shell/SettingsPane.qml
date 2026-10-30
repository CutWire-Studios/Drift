pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import Drift

// Every editor and app preference: a category list beside the selected page. On
// narrow widths the list becomes a scrolling strip of chips above the page.
// Hosted by SettingsDialog; it used to be a tab in the assets panel's rail.
//
// Canvas size, aspect and frame rate are not here. On the phone they belong to the
// project rather than to preferences, and the project title's sheet already offers
// them under Canvas & layout and Project properties; on desktop they are the
// header's Video dialog. Both reach VideoSizeControls directly.
Item {
    id: root

    property string currentPage: "general"
    // The phone shell has no workspaces, timeline toolbar or keyboard to configure.
    property bool mobile: false
    readonly property var mobileHiddenPages: ["workspaces", "toolbar", "keymap"]
    readonly property string query: searchField.text.trim().toLowerCase()
    readonly property bool narrow: width < 640
    readonly property real sidebarWidth: 220
    readonly property real pageMargin: narrow ? Theme.pagePadding : Theme.spacing3xl

    readonly property var pages: [
        { id: "general", title: qsTr("General"), keywords: "language locale startup autosave recovery cache backup" },
        { id: "appearance", title: qsTr("Appearance"), keywords: "theme dark light color workspace layout translucency" },
        { id: "workspaces", title: qsTr("Workspaces"), keywords: "workspace layout panel window monitor screen dock preset" },
        { id: "timeline", title: qsTr("Timeline"), keywords: "scroll wheel horizontal vertical crossfade transition overview minimap" },
        { id: "toolbar", title: qsTr("Timeline toolbar"), keywords: "toolbar buttons customize" },
        { id: "keymap", title: qsTr("Keyboard shortcuts"), keywords: "keymap keys hotkeys bindings" },
        { id: "preview", title: qsTr("Preview"), keywords: "quality resolution decode proxies guides background graphics gpu" },
        { id: "performance", title: qsTr("Performance"), keywords: "advanced tuning cache memory ram vram read-ahead buffer drop frames hardware encoder export pool" },
        { id: "audio", title: qsTr("Audio"), keywords: "sound speakers microphone output input" },
        { id: "ai", title: qsTr("AI & Cloud"), keywords: "agent mcp claude cursor api key elevenlabs voice scene detection" },
        { id: "network", title: qsTr("Network"), keywords: "proxy http socks internet firewall" },
        { id: "marketplace", title: qsTr("Marketplace"), keywords: "market store account sign in" },
        { id: "about", title: qsTr("About"), keywords: "version updates debug info diagnostics" }
    ]
    readonly property var visiblePages: pages.filter(
        (page) => (page.id !== "marketplace" || (Market.configured && Market.authenticated))
            && !(mobile && mobileHiddenPages.includes(page.id)))

    readonly property bool searchHasMatches: generalPage.hasMatches || appearancePage.hasMatches
        || (!mobile && (workspacesPage.hasMatches || toolbarPage.hasMatches || keymapPage.hasMatches))
        || timelinePage.hasMatches
        || previewPage.hasMatches || performancePage.hasMatches || audioPage.hasMatches || aiPage.hasMatches
        || networkPage.hasMatches || marketplacePage.hasMatches || aboutPage.hasMatches

    onCurrentPageChanged: flick.contentY = 0

    function selectPage(pageId) {
        currentPage = pageId
        searchField.text = ""
    }

    Rectangle {
        visible: !root.narrow
        width: root.sidebarWidth
        height: root.height
        color: Theme.c.settingsPane.sidebarBackground

        Rectangle {
            anchors.right: parent.right
            width: Theme.c.settingsPane.borderWidth
            height: parent.height
            color: Theme.c.settingsPane.divider
        }
    }

    ThemedTextField {
        id: searchField
        x: root.narrow ? Theme.pagePadding : Theme.spacingLg
        y: Theme.spacingLg
        width: root.narrow ? root.width - Theme.pagePadding * 2 : root.sidebarWidth - Theme.spacingLg * 2
        placeholderText: qsTr("Search settings…")
    }

    ListView {
        id: sideNav
        visible: !root.narrow
        x: Theme.spacingMd
        y: searchField.y + searchField.height + Theme.spacingMd
        width: root.sidebarWidth - Theme.spacingMd * 2
        height: root.height - y - Theme.spacingLg
        clip: true
        spacing: Theme.spacingXs
        model: root.visiblePages
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: AppScrollBar { }

        delegate: Rectangle {
            id: navItem

            required property var modelData

            readonly property bool selected: modelData.id === root.currentPage

            width: sideNav.width
            height: Theme.controlHeight
            radius: Theme.c.settingsPane.radius
            color: selected ? Theme.c.settingsPane.selectedBackground
                            : (navMouse.containsMouse ? Theme.c.settingsPane.hoverBackground : "transparent")

            Text {
                anchors.left: parent.left
                anchors.leftMargin: Theme.spacingLg
                anchors.right: parent.right
                anchors.rightMargin: Theme.spacingLg
                anchors.verticalCenter: parent.verticalCenter
                text: navItem.modelData.title
                color: Theme.c.settingsPane.foreground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeBase
                font.weight: navItem.selected ? Font.Medium : Font.Normal
                elide: Text.ElideRight
            }

            MouseArea {
                id: navMouse
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: root.selectPage(navItem.modelData.id)
            }
        }
    }

    ListView {
        id: tabStrip
        visible: root.narrow
        x: Theme.pagePadding
        y: searchField.y + searchField.height + Theme.spacingMd
        width: root.width - Theme.pagePadding * 2
        height: Theme.controlHeightSm
        clip: true
        spacing: Theme.spacingSm
        orientation: ListView.Horizontal
        model: root.visiblePages
        boundsBehavior: Flickable.StopAtBounds

        delegate: ThemedChip {
            required property var modelData

            text: modelData.title
            selected: modelData.id === root.currentPage
            onClicked: root.selectPage(modelData.id)
        }
    }

    Flickable {
        id: flick
        x: root.narrow ? 0 : root.sidebarWidth
        y: root.narrow ? tabStrip.y + tabStrip.height + Theme.spacingMd : 0
        width: root.width - x
        height: root.height - y
        contentWidth: width
        contentHeight: pageColumn.height
        clip: true
        property int dragLocks: 0
        interactive: dragLocks === 0
        ScrollBar.vertical: AppScrollBar { }

        Column {
            id: pageColumn
            x: root.pageMargin
            width: flick.width - root.pageMargin * 2
            topPadding: Theme.spacingXl
            bottomPadding: Theme.spacing3xl

            SettingsGeneralPage {
                id: generalPage
                query: root.query
                visible: root.query.length > 0 ? hasMatches : pageId === root.currentPage
            }

            SettingsAppearancePage {
                id: appearancePage
                query: root.query
                visible: root.query.length > 0 ? hasMatches : pageId === root.currentPage
            }

            SettingsWorkspacesPage {
                id: workspacesPage
                query: root.query
                visible: !root.mobile && (root.query.length > 0 ? hasMatches : pageId === root.currentPage)
            }

            SettingsTimelinePage {
                id: timelinePage
                query: root.query
                visible: root.query.length > 0 ? hasMatches : pageId === root.currentPage
            }

            SettingsToolbarPage {
                id: toolbarPage
                query: root.query
                visible: !root.mobile && (root.query.length > 0 ? hasMatches : pageId === root.currentPage)
            }

            SettingsKeymapPage {
                id: keymapPage
                query: root.query
                visible: !root.mobile && (root.query.length > 0 ? hasMatches : pageId === root.currentPage)
            }

            SettingsPreviewPage {
                id: previewPage
                query: root.query
                visible: root.query.length > 0 ? hasMatches : pageId === root.currentPage
            }

            SettingsPerformancePage {
                id: performancePage
                query: root.query
                visible: root.query.length > 0 ? hasMatches : pageId === root.currentPage
            }

            SettingsAudioPage {
                id: audioPage
                query: root.query
                visible: root.query.length > 0 ? hasMatches : pageId === root.currentPage
            }

            SettingsAiPage {
                id: aiPage
                query: root.query
                visible: root.query.length > 0 ? hasMatches : pageId === root.currentPage
            }

            SettingsNetworkPage {
                id: networkPage
                query: root.query
                visible: root.query.length > 0 ? hasMatches : pageId === root.currentPage
            }

            SettingsMarketplacePage {
                id: marketplacePage
                query: root.query
                visible: root.query.length > 0 ? hasMatches : pageId === root.currentPage
            }

            SettingsAboutPage {
                id: aboutPage
                query: root.query
                visible: root.query.length > 0 ? hasMatches : pageId === root.currentPage
            }

            EmptyState {
                visible: root.query.length > 0 && !root.searchHasMatches
                compact: true
                hint: qsTr("No settings match “%1”").arg(searchField.text.trim())
            }
        }
    }
}
