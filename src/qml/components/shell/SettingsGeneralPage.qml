import QtQuick
import Drift

SettingsPage {
    id: page

    pageId: "general"
    title: qsTr("General")

    function cacheSizeText(bytes) {
        const gib = 1024 * 1024 * 1024
        if (bytes >= gib)
            return qsTr("%1 GB").arg((bytes / gib).toFixed(1))
        return qsTr("%1 MB").arg((bytes / (1024 * 1024)).toFixed(1))
    }

    SettingsGroupHeader {
        text: qsTr("Language & region")
        query: page.query
    }

    SettingsRow {
        title: qsTr("Language")
        searchId: "general-language"
        description: qsTr("Language for menus and labels. Takes effect immediately.")
        keywords: "locale translation"
        query: page.query

        ThemedComboBox {
            width: Theme.c.settingsRow.controlWidth
            textRole: "label"
            valueRole: "id"
            model: EditorState.preferences.uiLanguages
            currentIndex: {
                const langs = EditorState.preferences.uiLanguages
                for (var i = 0; i < langs.length; ++i) {
                    if (langs[i].id === EditorState.preferences.uiLanguage)
                        return i
                }
                return 0
            }
            onActivated: {
                const langs = EditorState.preferences.uiLanguages
                if (currentIndex >= 0 && currentIndex < langs.length)
                    EditorState.preferences.uiLanguage = langs[currentIndex].id
            }
        }
    }

    SettingsGroupHeader {
        text: qsTr("Startup")
        query: page.query
    }

    SettingsRow {
        title: qsTr("Reopen last project on startup")
        searchId: "general-reopen-last-project"
        description: qsTr("Restores the last open project. Closing still asks you to save.")
        query: page.query

        ThemedSwitch {
            checked: EditorState.preferences.reopenLastProject
            onToggled: EditorState.preferences.reopenLastProject = checked
        }
    }

    SettingsGroupHeader {
        text: qsTr("Saving")
        query: page.query
    }

    SettingsRow {
        title: qsTr("Autosave interval")
        searchId: "general-autosave-interval"
        description: qsTr("How often unsaved work is snapshotted for crash recovery.")
        keywords: "recovery backup"
        query: page.query

        ThemedComboBox {
            width: Theme.c.settingsRow.controlWidth
            textRole: "label"
            valueRole: "id"
            model: [
                { id: 0,   label: qsTr("Off") },
                { id: 15,  label: qsTr("Every 15 seconds") },
                { id: 30,  label: qsTr("Every 30 seconds") },
                { id: 60,  label: qsTr("Every minute") },
                { id: 300, label: qsTr("Every 5 minutes") }
            ]
            currentIndex: {
                const opts = model
                for (var i = 0; i < opts.length; ++i) {
                    if (opts[i].id === EditorState.preferences.autosaveIntervalSeconds)
                        return i
                }
                return 0
            }
            onActivated: {
                if (currentIndex >= 0 && currentIndex < model.length)
                    EditorState.preferences.autosaveIntervalSeconds = model[currentIndex].id
            }
        }
    }

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
        text: qsTr("Feedback")
        query: page.query
    }

    SettingsRow {
        title: qsTr("Haptic feedback")
        searchId: "general-haptic-feedback"
        description: qsTr("Vibrate on taps, snaps, and edits. Uses this device’s own haptic effects when it has them.")
        available: Haptics.supported
        query: page.query

        ThemedSwitch {
            checked: Haptics.enabled
            onToggled: Haptics.enabled = checked
        }
    }

    SettingsGroupHeader {
        text: qsTr("Storage")
        query: page.query
    }

    SettingsRow {
        title: qsTr("Cache")
        searchId: "general-cache"
        description: qsTr("%1 used, stored in %2")
                         .arg(page.cacheSizeText(EditorState.preferences.cacheSizeBytes))
                         .arg(EditorState.preferences.cacheLocation)
        keywords: "disk space thumbnails proxies"
        query: page.query
        Component.onCompleted: EditorState.preferences.refreshCacheSize()

        ThemedButton {
            variant: "secondary"
            text: qsTr("Clear cache")
            enabled: !EditorState.preferences.cacheBusy
            onClicked: EditorState.preferences.clearCache()
        }
    }

    SettingsGroupHeader {
        text: qsTr("Settings file")
        query: page.query
    }

    SettingsRow {
        title: qsTr("Export and import")
        searchId: "general-export-import"
        description: qsTr("Save your preferences to a JSON file, or load them from one.")
        keywords: "backup restore"
        query: page.query

        Row {
            spacing: Theme.spacingMd

            ThemedButton {
                variant: "secondary"
                text: qsTr("Export settings…")
                onClicked: {
                    const url = FileDialogs.saveFile(qsTr("Export Settings"), [qsTr("JSON document (*.json)")],
                                                     "drift-settings", "json", "", ["application/json"])
                    if (url == "")
                        return
                    const error = EditorState.preferences.exportSettings(url)
                    EditorState.setLastMessage(error || qsTr("Settings exported"), error ? "error" : "success")
                }
            }

            ThemedButton {
                variant: "secondary"
                text: qsTr("Import settings…")
                onClicked: {
                    const url = FileDialogs.openFile(qsTr("Import Settings"), [qsTr("JSON document (*.json)")],
                                                     ["application/json"])
                    if (url == "")
                        return
                    const error = EditorState.preferences.importSettings(url)
                    if (error)
                        EditorState.setLastMessage(error, "error")
                }
            }
        }
    }
}
