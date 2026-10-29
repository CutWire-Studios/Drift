import QtQuick
import Drift

SettingsPage {
    id: page

    pageId: "ai"
    title: qsTr("AI & Cloud")

    SettingsRow {
        title: qsTr("Agent access")
        searchId: "ai-agent-access"
        description: qsTr("Let Cursor or Claude edit this project for you. Only programs on this device can connect.")
        keywords: "mcp claude cursor agent server"
        stacked: true
        query: page.query

        AgentAccessControls {
            width: parent.width
            showIntro: false
        }
    }

    SettingsRow {
        title: qsTr("Start agent access on launch")
        searchId: "ai-agent-access-on-launch"
        description: qsTr("Turn agent access on automatically the next time you open Drift. Turning access off resets this.")
        keywords: "mcp startup autostart agent"
        query: page.query

        ThemedSwitch {
            checked: EditorState.mcp.startOnLaunch
            onToggled: EditorState.mcp.startOnLaunch = checked
        }
    }

    SettingsRow {
        title: qsTr("Cloud providers")
        searchId: "ai-cloud-providers"
        keywords: "api key elevenlabs openai deepgram transcription voice"
        stacked: true
        query: page.query

        SettingsCloudProvidersSection { }
    }

    SettingsRow {
        title: qsTr("Scene detection sensitivity")
        searchId: "ai-scene-detection-sensitivity"
        description: qsTr("Higher finds more cuts. When a clip is flat or heavily graded, Drift still falls back to an adaptive threshold.")
        keywords: "shot cut detect"
        query: page.query

        ThemedSlider {
            width: Theme.c.settingsRow.controlWidth
            label: qsTr("Scene detection sensitivity")
            // Inverted: dragging right should mean "find more", but a lower threshold
            // is what does that.
            from: 4
            to: 60
            value: 64 - EditorState.scenes.threshold()
            valueFormatter: function (v) { return Number(64 - v).toFixed(0) }
            onMoved: EditorState.scenes.setThreshold(64 - value)
        }
    }
}
