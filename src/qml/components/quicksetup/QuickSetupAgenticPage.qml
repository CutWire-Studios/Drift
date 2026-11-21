import QtQuick
import Drift

QuickSetupPage {
    id: root

    readonly property var prefs: EditorState.preferences
    readonly property var options: [
        { id: "none", glyph: Theme.icons.eyeOff, title: qsTr("None"), detail: qsTr("Hide the AI workspaces and the Ask button") },
        { id: "some", glyph: Theme.icons.wand, title: qsTr("Some"), detail: qsTr("Keep AI tools and Ask within reach") },
        { id: "lots", glyph: Theme.icons.bot, title: qsTr("Lots"), detail: qsTr("AI agents are part of how I edit") }
    ]

    // Illustration slot "illustration": an assistant bubble handing a result to the timeline,
    // one idea: AI suggests, you decide.
    illustration: Component { OnboardingAgenticIllustration { } }
    title: qsTr("Will you work with AI agents?")
    subtitle: qsTr("Drift can be driven by assistants such as Claude through MCP. Choose how much of that to show.")
    glyph: Theme.icons.bot
    contentMaxWidth: 780

    Flow {
        id: flow

        width: parent.width
        spacing: Theme.spacingMd

        Repeater {
            model: root.options

            QuickSetupChoiceCard {
                required property var modelData

                width: root.mobile ? flow.width : (flow.width - flow.spacing * 2) / 3
                glyph: modelData.glyph
                title: modelData.title
                detail: modelData.detail
                selected: root.prefs.agenticUse === modelData.id
                onClicked: root.prefs.agenticUse = modelData.id
            }
        }
    }
}
