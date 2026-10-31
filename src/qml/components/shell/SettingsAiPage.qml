import QtQuick
import Drift

SettingsPage {
    id: page

    pageId: "ai"
    title: qsTr("AI & Cloud")

    // One-shot answers about what is on disk, re-read when an engine is installed or removed,
    // as in the Addon Manager's copy of this picker.
    property var accelerationOptions: Addons.accelerationOptions()
    property bool runtimeReady: Addons.runtimeAvailable()
    property bool restartRequired: Addons.runtimeRestartRequired()
    property string accelerationProblem: Addons.accelerationProblem()

    Connections {
        target: Addons
        function onKindChanged(kind) {
            if (kind !== "onnxruntime" && kind !== "onnxruntime-ep")
                return
            page.accelerationOptions = Addons.accelerationOptions()
            page.runtimeReady = Addons.runtimeAvailable()
            page.restartRequired = Addons.runtimeRestartRequired()
            page.accelerationProblem = Addons.accelerationProblem()
        }
    }

    SettingsGroupHeader {
        text: qsTr("On this device")
        query: page.query
    }

    SettingsRow {
        title: qsTr("How AI runs")
        searchId: "ai-acceleration"
        description: {
            if (!page.runtimeReady)
                return qsTr("Install an AI Engine from Extras to unlock auto captions, subject cutout, funny face effects, and noise removal.")
            if (page.restartRequired)
                return qsTr("Restart Drift for this to take effect.")
            if (page.accelerationProblem.length > 0)
                return page.accelerationProblem
            return qsTr("Automatic picks the fastest option you have installed, and uses this computer if the graphics card can't help.")
        }
        keywords: "acceleration gpu cuda webgpu amd rocm migraphx cpu onnx engine graphics"
        query: page.query

        ThemedComboBox {
            width: Theme.c.settingsRow.controlWidth
            textRole: "label"
            valueRole: "value"
            model: page.accelerationOptions
            currentIndex: {
                var current = Addons.acceleration()
                for (var i = 0; i < page.accelerationOptions.length; ++i) {
                    if (page.accelerationOptions[i].value === current)
                        return i
                }
                return 0
            }
            onActivated: index => {
                Addons.setAcceleration(page.accelerationOptions[index].value)
                page.restartRequired = Addons.runtimeRestartRequired()
                page.accelerationProblem = Addons.accelerationProblem()
            }
        }
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

    SettingsGroupHeader {
        text: qsTr("Agents")
        query: page.query
    }

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

    SettingsGroupHeader {
        text: qsTr("Cloud")
        query: page.query
    }

    SettingsRow {
        title: qsTr("Use cloud services")
        searchId: "ai-cloud-services"
        description: qsTr("Connect your own ElevenLabs or Fish Audio account for transcription, voiceover and sound effects.")
        keywords: "api key elevenlabs fish online"
        query: page.query

        ThemedSwitch {
            checked: EditorState.cloudProviders.shown
            onToggled: EditorState.cloudProviders.shown = checked
        }
    }

    SettingsRow {
        title: qsTr("Cloud providers")
        searchId: "ai-cloud-providers"
        keywords: "api key elevenlabs openai deepgram transcription voice"
        stacked: true
        // Searching reaches the fields even with the switch off.
        available: EditorState.cloudProviders.shown || page.query.length > 0
        query: page.query

        SettingsCloudProvidersSection { }
    }
}
