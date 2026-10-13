import QtQuick
import QtQuick.Controls.Basic
import Drift

// The app-level Settings menu: workspace layout, theme, language, shell, debug info and the full
// Settings dialog. None of it needs a project, so the editor header and the start screen both
// offer it; each wires the signals to its own window.
ThemedContextMenu {
    id: root
    implicitWidth: 248

    // Multicam acts on the open timeline; the start screen has none.
    property bool projectTools: true

    signal languageRequested()
    signal mobileLayoutRequested()
    signal multicamRequested()
    signal debugInfoRequested()
    signal moreSettingsRequested()

    ThemedMenuItem {
        sectionHeader: true
        text: qsTr("Workspace")
    }

    // The active entry swaps its own icon for a tick rather than
    // adding a trailing column — every row keeps a glyph, so the
    // labels stay aligned.
    ThemedMenuItem {
        text: qsTr("Auto (follow canvas)")
        icon.name: EditorState.preferences.workspaceLayoutOverridden ? Theme.icons.grid
                                                                     : Theme.icons.check
        onTriggered: EditorState.preferences.clearWorkspaceLayoutPreference()
    }

    ThemedMenuItem {
        text: qsTr("Landscape")
        icon.name: EditorState.preferences.workspaceLayoutOverridden
                   && EditorState.preferences.workspaceLayoutPreferred === "landscape"
                   ? Theme.icons.check : Theme.icons.monitor
        onTriggered: EditorState.preferences.setWorkspaceLayoutPreference("landscape")
    }

    ThemedMenuItem {
        text: qsTr("Portrait")
        icon.name: EditorState.preferences.workspaceLayoutOverridden
                   && EditorState.preferences.workspaceLayoutPreferred === "portrait"
                   ? Theme.icons.check : Theme.icons.smartphone
        onTriggered: EditorState.preferences.setWorkspaceLayoutPreference("portrait")
    }

    ThemedMenuSeparator { }

    ThemedMenuItem {
        sectionHeader: true
        text: qsTr("Theme")
    }

    ThemedMenuItem {
        text: qsTr("Light")
        icon.name: Theme.darkMode ? Theme.icons.sun : Theme.icons.check
        onTriggered: Theme.setDarkMode(false)
    }

    ThemedMenuItem {
        text: qsTr("Dark")
        icon.name: Theme.darkMode ? Theme.icons.check : Theme.icons.moon
        onTriggered: Theme.setDarkMode(true)
    }

    ThemedMenuSeparator { }

    ThemedMenuItem {
        text: qsTr("Language…")
        icon.name: Theme.icons.languages
        onTriggered: root.languageRequested()
    }

    ThemedMenuItem {
        text: qsTr("Switch to mobile layout…")
        icon.name: Theme.icons.smartphone
        onTriggered: root.mobileLayoutRequested()
    }

    ThemedMenuSeparator { }

    // Infrequent tools. They were icon-only header buttons of their own,
    // unlabelled and easy to hit by accident next to Export.
    ThemedMenuItem {
        visible: root.projectTools
        text: qsTr("Multicam")
        icon.name: Theme.icons.shuffle
        onTriggered: root.multicamRequested()
    }

    ThemedMenuItem {
        text: qsTr("Debug info…")
        icon.name: Theme.icons.bug
        onTriggered: root.debugInfoRequested()
    }

    ThemedMenuSeparator { }

    ThemedMenuItem {
        text: qsTr("More settings…")
        icon.name: Theme.icons.sliders
        onTriggered: root.moreSettingsRequested()
    }
}
