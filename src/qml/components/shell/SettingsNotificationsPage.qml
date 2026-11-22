import QtQuick
import Drift

SettingsPage {
    id: page

    pageId: "notifications"
    title: qsTr("Notifications")

    SettingsGroupHeader {
        text: qsTr("Extras")
        query: page.query
    }

    SettingsRow {
        title: qsTr("Remind about essential packs")
        searchId: "general-remind-essential-packs"
        description: qsTr("Pulse the Extras icon when the video, transitions, and audio packs are not installed.")
        query: page.query

        ThemedSwitch {
            checked: Addons.remindEssential
            onToggled: Addons.remindEssential = checked
        }
    }

    SettingsRow {
        title: qsTr("Remind about pack updates")
        searchId: "general-remind-pack-updates"
        description: qsTr("Pulse the Extras icon when updates are available for packs you already have installed.")
        query: page.query

        ThemedSwitch {
            checked: Addons.remindUpdates
            onToggled: Addons.remindUpdates = checked
        }
    }

    SettingsGroupHeader {
        text: qsTr("Agents")
        query: page.query
    }

    SettingsRow {
        title: qsTr("Notifications for agent actions")
        searchId: "ai-agent-notifications"
        description: qsTr("Which pop-ups an agent's own edits show you. Errors always show.")
        keywords: "mcp agent toast notification quiet silence"
        query: page.query

        ThemedComboBox {
            width: Theme.c.settingsRow.controlWidth
            textRole: "label"
            valueRole: "id"
            model: [
                { id: "all", label: qsTr("All") },
                { id: "problems", label: qsTr("Problems only") },
                { id: "none", label: qsTr("None (errors still show)") }
            ]
            currentIndex: {
                for (var i = 0; i < model.length; ++i) {
                    if (model[i].id === EditorState.preferences.agentNotifications)
                        return i
                }
                return 1
            }
            onActivated: {
                if (currentIndex >= 0 && currentIndex < model.length)
                    EditorState.preferences.agentNotifications = model[currentIndex].id
            }
        }
    }
}
