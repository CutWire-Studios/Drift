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
        { id: "about", title: qsTr("About"), keywords: "version updates debug info diagnostics" }
    ]
    readonly property var visiblePages: pages.filter(
        (page) => !(mobile && mobileHiddenPages.includes(page.id)))

    readonly property bool searchHasMatches: {
        for (const slot of [generalSlot, appearanceSlot, workspacesSlot, timelineSlot, toolbarSlot, keymapSlot,
                            previewSlot, performanceSlot, audioSlot, aiSlot, networkSlot, aboutSlot]) {
            if (slot.hasMatches)
                return true
        }
        return false
    }

    // A page exists only while it is the open one, or while a search needs every page's rows to
    // test against the query. Building all twelve up front dominated the dialog's open time.
    component PageSlot: Loader {
        id: slot

        required property string pageId
        property bool allowed: true

        readonly property bool hasMatches: item !== null && item.hasMatches
        readonly property bool shown: item !== null && (root.query.length > 0 ? item.hasMatches : true)

        width: parent.width
        // implicitHeight, not height: the Loader writes its own height back onto the item, so a
        // page collapsed to 0 during a search would stay 0.
        height: shown ? item.implicitHeight : 0
        // Clipped rather than hidden: a hidden page would hide its rows too, and the rows' own
        // visibility is what hasMatches reads.
        clip: !shown
        active: allowed && (root.query.length > 0 || root.currentPage === pageId)
    }

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

            PageSlot {
                id: generalSlot
                pageId: "general"
                sourceComponent: SettingsGeneralPage { query: root.query }
            }

            PageSlot {
                id: appearanceSlot
                pageId: "appearance"
                sourceComponent: SettingsAppearancePage { query: root.query }
            }

            PageSlot {
                id: workspacesSlot
                pageId: "workspaces"
                allowed: !root.mobile
                sourceComponent: SettingsWorkspacesPage { query: root.query }
            }

            PageSlot {
                id: timelineSlot
                pageId: "timeline"
                sourceComponent: SettingsTimelinePage { query: root.query }
            }

            PageSlot {
                id: toolbarSlot
                pageId: "toolbar"
                allowed: !root.mobile
                sourceComponent: SettingsToolbarPage { query: root.query }
            }

            PageSlot {
                id: keymapSlot
                pageId: "keymap"
                allowed: !root.mobile
                sourceComponent: SettingsKeymapPage { query: root.query }
            }

            PageSlot {
                id: previewSlot
                pageId: "preview"
                sourceComponent: SettingsPreviewPage { query: root.query }
            }

            PageSlot {
                id: performanceSlot
                pageId: "performance"
                sourceComponent: SettingsPerformancePage { query: root.query }
            }

            PageSlot {
                id: audioSlot
                pageId: "audio"
                sourceComponent: SettingsAudioPage { query: root.query }
            }

            PageSlot {
                id: aiSlot
                pageId: "ai"
                sourceComponent: SettingsAiPage { query: root.query }
            }

            PageSlot {
                id: networkSlot
                pageId: "network"
                sourceComponent: SettingsNetworkPage { query: root.query }
            }

            PageSlot {
                id: aboutSlot
                pageId: "about"
                sourceComponent: SettingsAboutPage { query: root.query }
            }

            EmptyState {
                visible: root.query.length > 0 && !root.searchHasMatches
                compact: true
                hint: qsTr("No settings match “%1”").arg(searchField.text.trim())
            }
        }
    }
}
