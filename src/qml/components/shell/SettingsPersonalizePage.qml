import QtQuick
import Drift

SettingsPage {
    id: page

    pageId: "personalize"
    title: qsTr("Personalize")

    function setWorkspaceHidden(id, hidden) {
        const list = EditorState.preferences.hiddenWorkspaces.filter(w => w !== id)
        if (hidden)
            list.push(id)
        EditorState.preferences.hiddenWorkspaces = list
    }

    SettingsGroupHeader {
        text: qsTr("About you")
        query: page.query
    }

    SettingsRow {
        title: qsTr("What you make")
        searchId: "personalize-what-you-make"
        description: qsTr("Used to suggest sensible defaults for new projects.")
        keywords: "youtube shorts films podcasts tutorials creator content type"
        query: page.query

        ThemedComboBox {
            width: Theme.c.settingsRow.controlWidth
            textRole: "label"
            valueRole: "id"
            model: [
                { id: "", label: qsTr("Not set") },
                { id: "youtube", label: qsTr("YouTube videos") },
                { id: "shorts", label: qsTr("Shorts and Reels") },
                { id: "films", label: qsTr("Films") },
                { id: "podcasts", label: qsTr("Podcasts") },
                { id: "tutorials", label: qsTr("Tutorials") }
            ]
            currentIndex: {
                for (var i = 0; i < model.length; ++i) {
                    if (model[i].id === EditorState.preferences.whatYouMake)
                        return i
                }
                return 0
            }
            onActivated: EditorState.preferences.whatYouMake = model[currentIndex].id
        }
    }

    SettingsRow {
        title: qsTr("Experience with video editing")
        searchId: "personalize-experience-level"
        description: qsTr("First time hides the Studio workspaces. You can show any of them again below.")
        keywords: "beginner expert level pro studio workspaces simple"
        query: page.query

        ThemedComboBox {
            width: Theme.c.settingsRow.controlWidth
            textRole: "label"
            valueRole: "id"
            model: [
                { id: "first", label: qsTr("First time") },
                { id: "some", label: qsTr("Some experience") },
                { id: "pro", label: qsTr("Pro") }
            ]
            currentIndex: {
                for (var i = 0; i < model.length; ++i) {
                    if (model[i].id === EditorState.preferences.experienceLevel)
                        return i
                }
                return 1
            }
            onActivated: EditorState.preferences.experienceLevel = model[currentIndex].id
        }
    }

    SettingsRow {
        title: qsTr("Working with AI agents")
        searchId: "personalize-agentic-use"
        description: qsTr("None hides the AI and Procedural workspaces and the Ask button in the header.")
        keywords: "agentic assistant ai ask mcp claude automation"
        query: page.query

        ThemedComboBox {
            width: Theme.c.settingsRow.controlWidth
            textRole: "label"
            valueRole: "id"
            model: [
                { id: "none", label: qsTr("None") },
                { id: "some", label: qsTr("Some") },
                { id: "lots", label: qsTr("Lots") }
            ]
            currentIndex: {
                for (var i = 0; i < model.length; ++i) {
                    if (model[i].id === EditorState.preferences.agenticUse)
                        return i
                }
                return 1
            }
            onActivated: EditorState.preferences.agenticUse = model[currentIndex].id
        }
    }

    SettingsGroupHeader {
        text: qsTr("Look")
        query: page.query
    }

    SettingsRow {
        stacked: true
        title: qsTr("Accent colour")
        searchId: "personalize-accent-colour"
        description: qsTr("Colours buttons, selections and highlights on top of the current theme.")
        keywords: "accent color colour highlight brand primary swatch"
        query: page.query

        AccentSwatches {
            width: parent.width
        }
    }

    SettingsRow {
        title: qsTr("Labels under panel tabs")
        searchId: "personalize-rail-labels"
        description: qsTr("Show a name under each icon on the side tab rails.")
        keywords: "rail labels icons text tabs sidebar"
        query: page.query

        ThemedSwitch {
            checked: EditorState.preferences.showRailLabels
            onToggled: EditorState.preferences.showRailLabels = checked
        }
    }

    SettingsGroupHeader {
        text: qsTr("Hints")
        query: page.query
    }

    SettingsRow {
        title: qsTr("Show hints")
        searchId: "personalize-show-hints"
        description: qsTr("Short tips and suggestions that appear as you work.")
        keywords: "tips suggestions tooltips help onboarding"
        query: page.query

        ThemedSwitch {
            checked: EditorState.preferences.showTips
            onToggled: EditorState.preferences.showTips = checked
        }
    }

    SettingsRow {
        title: qsTr("Reset dismissed tips")
        searchId: "personalize-reset-tips"
        description: qsTr("Bring back every tip and suggestion you closed. They reappear the next time you open the panel they belong to.")
        keywords: "tips suggestions hints dismissed restore again"
        query: page.query

        ThemedButton {
            variant: "secondary"
            text: qsTr("Reset tips")
            onClicked: {
                EditorState.preferences.resetDismissedTips()
                Toasts.info(qsTr("Tips will show again"))
            }
        }
    }

    SettingsGroupHeader {
        text: qsTr("Workspaces")
        query: page.query
    }

    SettingsRow {
        stacked: true
        title: qsTr("Workspaces in the switcher")
        searchId: "personalize-workspace-visibility"
        description: qsTr("Hidden workspaces stay reachable from search (Ctrl+K) and their Alt+number shortcuts. Edit is always shown.")
        keywords: "workspace hide show studio switcher visibility edit color audio 3d forge procedural"
        query: page.query

        Column {
            width: parent.width
            spacing: Theme.spacingSm

            Repeater {
                model: Workspaces.workspaces

                ThemedSwitch {
                    required property var modelData

                    width: parent.width
                    text: modelData.name
                    enabled: modelData.id !== "edit"
                    checked: modelData.id === "edit"
                             || EditorState.preferences.hiddenWorkspaces.indexOf(modelData.id) < 0
                    onToggled: page.setWorkspaceHidden(modelData.id, !checked)
                }
            }
        }
    }
}
