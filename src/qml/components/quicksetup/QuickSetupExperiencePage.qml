import QtQuick
import Drift

QuickSetupPage {
    id: root

    readonly property var prefs: EditorState.preferences
    readonly property var options: [
        { id: "first", glyph: Theme.icons.sparkles, title: qsTr("First time"), detail: qsTr("Fewer workspaces, labelled tabs and hints as you go") },
        { id: "some", glyph: Theme.icons.gauge, title: qsTr("Some experience"), detail: qsTr("All the usual tools, labels and hints") },
        { id: "pro", glyph: Theme.icons.star, title: qsTr("Pro"), detail: qsTr("Every workspace, compact tabs and no hints") }
    ]

    // Illustration slot "illustration": a timeline growing from one clip (first time) to a
    // dense multi-track stack (pro), one idea: how much is on screen.
    illustration: Component { OnboardingExperienceIllustration { } }
    title: qsTr("How much editing have you done?")
    subtitle: qsTr("This decides how much Drift shows at first. Anything hidden stays reachable from search (Ctrl+K) and in Settings.")
    glyph: Theme.icons.gauge
    contentMaxWidth: 780

    function choose(id) {
        if (prefs.experienceLevel === id)
            return
        prefs.experienceLevel = id
        prefs.showRailLabels = id !== "pro"
        prefs.showTips = id !== "pro"
    }

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
                selected: root.prefs.experienceLevel === modelData.id
                onClicked: root.choose(modelData.id)
            }
        }
    }
}
