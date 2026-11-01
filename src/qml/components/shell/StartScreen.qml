import QtQuick
import Drift

// Shown at startup in place of a fresh empty project whenever "Reopen last project
// on startup" is off: what you were working on, and how to start something. The
// mobile equivalent is AndroidProjectsPage; this mirrors its structure (new project,
// open project, recent projects) with desktop-sized controls and ProjectRow's
// styling (RecentProjectsPopup) for the recents list.
Item {
    id: root

    signal newProjectRequested()
    signal openProjectRequested()
    signal openRecentRequested(string path)
    // The header's app-level entries, which do not need a project: without them here,
    // turning on agent access or changing a setting meant creating an empty project first.
    signal helpRequested()
    signal moreSettingsRequested()
    signal debugInfoRequested()
    signal agentAccessRequested()

    ThemeSlot {
        anchors.fill: parent
        slot: "StartScreenBackground"
        control: root
        fallback: Component {
            Rectangle {
                anchors.fill: parent
                color: Theme.c.startScreen.background
            }
        }
    }

    readonly property var items: EditorState.projectFile.recentProjects

    // Same order, icons and labels as the editor header, so they read as the same controls.
    Row {
        id: appActions
        z: 1
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.margins: Theme.spacingXl
        spacing: Theme.spacingSm

        // The header's own Settings menu, minus Multicam, which needs a timeline.
        IconButton {
            id: settingsButton
            glyph: Theme.icons.settings
            variant: "ghost"
            text: qsTr("Settings")
            active: settingsMenu.opened
            tooltip: qsTr("Settings")
            onClicked: settingsMenu.popup(0, settingsButton.height + Theme.spacingMd)

            AppSettingsMenu {
                id: settingsMenu
                projectTools: false
                onMoreSettingsRequested: root.moreSettingsRequested()
                onHelpRequested: root.helpRequested()
            }
        }

        IconButton {
            glyph: Theme.icons.bug
            variant: "ghost"
            text: qsTr("Debug info")
            tooltip: qsTr("System, playback and codec details for bug reports")
            onClicked: root.debugInfoRequested()
        }

        IconButton {
            glyph: Theme.icons.bot
            variant: "ghost"
            text: qsTr("Agent")
            active: EditorState.mcp.running
            tooltip: EditorState.mcp.running
                     ? qsTr("Agent access is on")
                     : qsTr("Agent access")
            onClicked: root.agentAccessRequested()
        }
    }

    Flickable {
        id: flick
        anchors.fill: parent
        contentWidth: width
        contentHeight: Math.max(height, column.implicitHeight + Theme.spacing3xl * 2)
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        flickableDirection: Flickable.VerticalFlick

        Column {
            id: column
            width: Math.min(560, flick.width - Theme.spacing3xl * 2)
            anchors.horizontalCenter: parent.horizontalCenter
            y: Math.max(Theme.spacing3xl, (flick.height - implicitHeight) / 2)
            spacing: Theme.spacing3xl

            StartScreenTitle {
                width: parent.width
            }

            StartScreenActions {
                width: parent.width
                onNewProjectRequested: root.newProjectRequested()
                onOpenProjectRequested: root.openProjectRequested()
            }

            StartScreenRecents {
                width: parent.width
                items: root.items
                onOpenRecentRequested: (path) => root.openRecentRequested(path)
            }
        }
    }
}
