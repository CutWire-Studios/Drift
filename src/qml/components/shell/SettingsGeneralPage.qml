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

    SettingsRow {
        title: qsTr("Undo history")
        searchId: "general-undo-history"
        description: qsTr("Where undo steps are kept. Saved with the project, they come back when you reopen it; long sessions use a lot of memory either way.")
        keywords: "undo redo history memory steps"
        query: page.query

        ThemedComboBox {
            width: Theme.c.settingsRow.controlWidth
            textRole: "label"
            valueRole: "id"
            model: [
                { id: "project", label: qsTr("Saved with the project") },
                { id: "session", label: qsTr("This session only") },
                { id: "off",     label: qsTr("Off") }
            ]
            currentIndex: {
                for (var i = 0; i < model.length; ++i) {
                    if (model[i].id === EditorState.preferences.undoHistoryMode)
                        return i
                }
                return 0
            }
            onActivated: {
                if (currentIndex >= 0 && currentIndex < model.length)
                    EditorState.preferences.undoHistoryMode = model[currentIndex].id
            }
        }
    }

    SettingsRow {
        title: qsTr("Undo steps kept")
        searchId: "general-undo-steps"
        description: qsTr("Older steps are dropped, and media only they used is no longer kept for them.")
        keywords: "undo redo history memory limit"
        query: page.query
        enabled: EditorState.preferences.undoHistoryMode !== "off"

        ThemedComboBox {
            width: Theme.c.settingsRow.controlWidth
            textRole: "label"
            valueRole: "id"
            model: [
                { id: 0,   label: qsTr("Unlimited") },
                { id: 500, label: qsTr("500 steps") },
                { id: 200, label: qsTr("200 steps") },
                { id: 100, label: qsTr("100 steps") },
                { id: 50,  label: qsTr("50 steps") }
            ]
            currentIndex: {
                for (var i = 0; i < model.length; ++i) {
                    if (model[i].id === EditorState.preferences.undoHistoryLimit)
                        return i
                }
                return 0
            }
            onActivated: {
                if (currentIndex >= 0 && currentIndex < model.length)
                    EditorState.preferences.undoHistoryLimit = model[currentIndex].id
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
        description: qsTr("%1 used").arg(page.cacheSizeText(EditorState.preferences.cacheSizeBytes))
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

    SettingsRow {
        title: qsTr("Cache location")
        searchId: "general-cache-location"
        description: EditorState.preferences.cacheLocationPendingRestart
                         ? qsTr("%1. Takes effect after restart.").arg(EditorState.preferences.cacheLocation)
                         : EditorState.preferences.cacheLocation
        keywords: "cache folder directory path storage"
        available: Qt.platform.os !== "android" && Qt.platform.os !== "wasm"
        query: page.query

        ThemedButton {
            variant: "secondary"
            text: qsTr("Reset")
            visible: EditorState.preferences.cacheLocationCustom
            onClicked: EditorState.preferences.resetCacheLocation()
        }

        ThemedButton {
            variant: "secondary"
            text: qsTr("Change…")
            onClicked: {
                const url = FileDialogs.openDirectory(qsTr("Cache location"))
                if (url && url.toString() !== "")
                    EditorState.preferences.setCacheLocation(url)
            }
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
