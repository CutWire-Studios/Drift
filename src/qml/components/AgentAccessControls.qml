import QtQuick
import QtQuick.Controls.Basic
import Drift

// Localhost MCP, written for the person connecting an assistant rather than for someone
// reading a protocol spec. Access is per session; the key persists so a pasted setup keeps
// working across launches.
//
// One surface, two hosts: AgentAccessDialog (the desktop header button) and the Agent
// access section of SettingsPane, which is where the phone reaches it. Phase 0 added a
// cut-down copy of this to SettingsPane, and the two immediately said different things
// about the same switch — the mobile copy offered only the Claude command, so an "Allow
// for this session" toggled there could not be connected from Cursor at all.
Column {
    id: root

    property bool detailsOpen: false
    // The dialog leads with its own explanation; the settings section has a heading above
    // it and does not need a second one.
    property bool showIntro: true

    spacing: Theme.spacingXl

    ThemedLabel {
        width: parent.width
        size: "sm"
        wrapMode: Text.WordWrap
        visible: root.showIntro
        text: qsTr("Let Cursor or Claude edit this project for you — add clips, change the timeline, and check how it looks. Only programs on this device. Off by default each time you open Drift, unless you turn on “Start agent on startup” below; turn it off here when you finish. The key stays the same between sessions, so a setup you pasted once keeps working.")
    }

    ThemedSwitch {
        checked: EditorState.mcpEnabled
        text: qsTr("Allow for this session")
        tooltip: qsTr("Let an assistant on this device edit this project until you turn it off or quit.")
        onToggled: EditorState.mcpEnabled = checked
    }

    // Visible whenever there is something to act on: normally that means access is
    // currently on, but a start-on-launch attempt that failed leaves mcpStartOnLaunch
    // set and mcpRunning false — the switch has to stay reachable then too, or turning
    // it back off (to stop the next launch from trying again) has nowhere to happen.
    ThemedSwitch {
        visible: EditorState.mcpRunning || EditorState.mcpStartOnLaunch
        checked: EditorState.mcpStartOnLaunch
        text: qsTr("Start agent on startup")
        tooltip: qsTr("Skip the manual toggle next time you open Drift. Turning access off resets this.")
        onToggled: EditorState.mcpStartOnLaunch = checked
    }

    ThemedLabel {
        width: parent.width
        visible: EditorState.mcpError.length > 0
        text: EditorState.mcpError
        color: Theme.destructive
    }

    ThemedLabel {
        width: parent.width
        visible: !EditorState.mcpRunning
        wrapMode: Text.WordWrap
        text: qsTr("Turn this on, then copy the setup for Cursor or Claude and paste it into that app.")
    }

    Column {
        width: parent.width
        spacing: Theme.spacingLg
        visible: EditorState.mcpRunning

        Row {
            spacing: Theme.spacingMd

            IconGlyph {
                glyph: Theme.icons.success
                iconSize: Theme.iconSizeMd
                iconColor: Theme.constructive
                anchors.verticalCenter: parent.verticalCenter
            }

            Text {
                text: qsTr("Access is on")
                color: Theme.constructive
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeSm
                font.weight: Font.Medium
                anchors.verticalCenter: parent.verticalCenter
            }
        }

        // The address alone is not enough to connect — the server rejects any request
        // without the session token — so the copyable setup has to sit beside the switch.
        ThemedLabel {
            width: parent.width
            wrapMode: Text.WordWrap
            visible: EditorState.mcpUrl.length > 0
            size: "sm"
            text: qsTr("Listening on %1").arg(EditorState.mcpUrl)
        }

        ThemedButton {
            variant: "ghost"
            glyph: Theme.icons.refresh
            text: qsTr("New key")
            tooltip: qsTr("Replace the key. Every assistant set up with the old one stops working until you copy the setup again.")
            onClicked: {
                EditorState.rotateMcpToken()
                Toasts.success(qsTr("New key made — copy the setup again"))
            }
        }

        ThemedLabel {
            width: parent.width
            wrapMode: Text.WordWrap
            text: qsTr("Copy the setup for the assistant you use. You only need one.")
        }

        ThemedButton {
            width: parent.width
            variant: "secondary"
            glyph: Theme.icons.copy
            text: qsTr("Copy for Cursor")
            tooltip: qsTr("Copy a setup snippet to paste into Cursor")
            onClicked: {
                EditorState.copyMcpCursorSnippet()
                Toasts.success(qsTr("Copied for Cursor"))
            }
        }

        ThemedButton {
            width: parent.width
            variant: "secondary"
            glyph: Theme.icons.copy
            text: qsTr("Copy for Claude")
            tooltip: qsTr("Copy a command to paste into Claude Code")
            onClicked: {
                EditorState.copyMcpClaudeCommand()
                Toasts.success(qsTr("Copied for Claude"))
            }
        }

        ThemedLabel {
            width: parent.width
            wrapMode: Text.WordWrap
            text: qsTr("Paste that into the assistant. To help it use this editor, copy the how-to next and paste it into the chat.")
        }

        ThemedButton {
            width: parent.width
            variant: "ghost"
            glyph: Theme.icons.copy
            text: qsTr("Copy a how-to for the agent")
            tooltip: qsTr("A short list of what the agent can do here — paste it into the chat")
            onClicked: {
                EditorState.copyMcpAgentGuide()
                Toasts.success(qsTr("Copied how-to"))
            }
        }

        ThemedButton {
            variant: "ghost"
            glyph: root.detailsOpen ? Theme.icons.chevronDown : Theme.icons.chevronRight
            text: qsTr("More options")
            onClicked: root.detailsOpen = !root.detailsOpen
        }

        Column {
            width: parent.width
            spacing: Theme.spacingMd
            visible: root.detailsOpen

            ThemedLabel {
                width: parent.width
                wrapMode: Text.WordWrap
                text: qsTr("For a different assistant, copy a one-time setup. The address and key are already in the Cursor and Claude copies above.")
            }

            ThemedButton {
                width: parent.width
                variant: "ghost"
                glyph: Theme.icons.copy
                text: qsTr("Copy one-time setup")
                tooltip: qsTr("Add this once to the assistant’s config. Access still has to be turned on here.")
                onClicked: {
                    EditorState.copyMcpStdioSnippet()
                    Toasts.success(qsTr("Copied one-time setup"))
                }
            }
        }
    }
}
