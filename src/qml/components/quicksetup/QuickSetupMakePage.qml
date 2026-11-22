import QtQuick
import Drift

QuickSetupPage {
    id: root

    readonly property var prefs: EditorState.preferences
    readonly property var options: [
        { id: "youtube", glyph: Theme.icons.brandYoutube, title: qsTr("YouTube videos"), detail: qsTr("Long videos in widescreen") },
        { id: "shorts", glyph: Theme.icons.smartphone, title: qsTr("Shorts and Reels"), detail: qsTr("Vertical videos for phones") },
        { id: "films", glyph: Theme.icons.film, title: qsTr("Films"), detail: qsTr("Cinematic edits and color grading") },
        { id: "podcasts", glyph: Theme.icons.mic, title: qsTr("Podcasts"), detail: qsTr("Talking heads, captions and clean audio") },
        { id: "tutorials", glyph: Theme.icons.bookOpen, title: qsTr("Tutorials"), detail: qsTr("Screen recordings and walkthroughs") }
    ]

    // Illustration slot "illustration": a camera frame cycling between 16:9 and 9:16 outlines
    // (one idea: the canvas follows what you make).
    illustration: Component { OnboardingMakeIllustration { } }
    title: qsTr("What do you make?")
    subtitle: qsTr("Drift sets up new projects, the workspace and suggested add-ons to match. You can skip this and change it later in Settings.")
    glyph: Theme.icons.film
    nextLabel: prefs.whatYouMake === "" ? qsTr("Skip") : ""
    contentMaxWidth: 780

    function firstAssetsTab(make) {
        switch (make) {
        case "shorts": return "templates"
        case "podcasts": return "subtitles"
        case "tutorials": return "text"
        default: return "media"
        }
    }

    function choose(id) {
        const previous = prefs.whatYouMake
        if (previous === id)
            return
        prefs.whatYouMake = id
        if (mobile)
            return
        if (id === "shorts")
            prefs.setWorkspaceLayoutPreference("portrait")
        else if (previous === "shorts")
            prefs.clearWorkspaceLayoutPreference()
        LayoutMemory.setLayoutString("assets/tab", firstAssetsTab(id))
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
                selected: root.prefs.whatYouMake === modelData.id
                onClicked: root.choose(modelData.id)
            }
        }
    }
}
