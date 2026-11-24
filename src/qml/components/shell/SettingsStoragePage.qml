import QtQuick
import Drift

SettingsPage {
    id: page

    pageId: "storage"
    title: qsTr("Storage & media")

    function cacheSizeText(bytes) {
        const gib = 1024 * 1024 * 1024
        if (bytes >= gib)
            return qsTr("%1 GB").arg((bytes / gib).toFixed(1))
        return qsTr("%1 MB").arg((bytes / (1024 * 1024)).toFixed(1))
    }

    SettingsGroupHeader {
        text: qsTr("Cache")
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
        text: qsTr("Preview copies (proxies)")
        query: page.query
    }

    SettingsRow {
        title: qsTr("Use preview copies for playback")
        searchId: "preview-use-proxies"
        description: qsTr("Play clips from their low-resolution preview copies (proxies) where one exists. Export always uses the original media.")
        keywords: "proxy low resolution optimized"
        query: page.query

        ThemedSwitch {
            checked: EditorState.playback.useProxies
            onToggled: EditorState.playback.useProxies = checked
        }
    }

    SettingsRow {
        title: qsTr("Suggest preview copies on import")
        searchId: "preview-proxy-suggestion"
        description: qsTr("What to do when imported footage may preview slowly.")
        keywords: "proxy import suggest notification automatic"
        query: page.query

        ThemedComboBox {
            width: Theme.c.settingsRow.controlWidth
            textRole: "label"
            valueRole: "id"
            model: [
                { id: "ask", label: qsTr("Ask") },
                { id: "always", label: qsTr("Always create preview copies") },
                { id: "never", label: qsTr("Never ask") }
            ]
            currentIndex: {
                for (var i = 0; i < model.length; ++i) {
                    if (model[i].id === EditorState.preferences.proxySuggestion)
                        return i
                }
                return 0
            }
            onActivated: {
                if (currentIndex >= 0 && currentIndex < model.length)
                    EditorState.preferences.proxySuggestion = model[currentIndex].id
            }
        }
    }

    SettingsRow {
        title: qsTr("Preview copy resolution")
        searchId: "preview-proxy-resolution"
        description: qsTr("Size of new preview copies. Copies made at another size are not used until you create them again.")
        keywords: "proxy size 720p 1080p"
        query: page.query

        ThemedComboBox {
            width: Theme.c.settingsRow.controlWidth
            textRole: "label"
            valueRole: "id"
            model: [
                { id: 360, label: qsTr("360p") },
                { id: 540, label: qsTr("540p") },
                { id: 720, label: qsTr("720p") },
                { id: 1080, label: qsTr("1080p") }
            ]
            currentIndex: {
                for (var i = 0; i < model.length; ++i) {
                    if (model[i].id === EditorState.playback.proxySize)
                        return i
                }
                return 2
            }
            onActivated: EditorState.playback.proxySize = model[currentIndex].id
        }
    }

    SettingsRow {
        title: qsTr("Save screen and camera recordings")
        searchId: "capture-recording-location"
        description: qsTr("Where Drift puts the recordings it makes. A recording is the only copy of what it caught, so Drift’s cache is not an option here — it gets cleared. Changing this doesn’t move recordings you already made.")
        keywords: "record recording screen camera capture folder location movies videos"
        stacked: true
        query: page.query
        visible: EditorState.capture.screenAvailable() || EditorState.capture.cameraAvailable()

        Column {
            id: recordingColumn
            width: parent.width
            spacing: Theme.spacingSm

            readonly property bool hasFolder: EditorState.preferences.recordingFolder.length > 0

            function chooseFolder() {
                const url = FileDialogs.openDirectory(qsTr("Folder for recordings"),
                                                      EditorState.preferences.recordingFolderUrl())
                if (!url || url.toString() === "")
                    return false
                const reason = EditorState.preferences.setRecordingFolder(url.toString())
                if (reason.length > 0) {
                    Toasts.error(reason)
                    return false
                }
                EditorState.preferences.recordingLocation = "folder"
                return true
            }

            ThemedComboBox {
                id: recordingLocationCombo
                width: Theme.c.settingsRow.controlWidth
                textRole: "label"
                valueRole: "id"
                model: FileDialogs.supportsDirectoryPicker()
                       ? [
                           { id: "movies", label: qsTr("In my Movies folder") },
                           { id: "project", label: qsTr("Beside the project") },
                           { id: "folder", label: qsTr("In this folder…") }
                         ]
                       : [
                           { id: "movies", label: qsTr("In my Movies folder") },
                           { id: "project", label: qsTr("Beside the project") }
                         ]
                currentIndex: {
                    for (var i = 0; i < model.length; ++i) {
                        if (model[i].id === EditorState.preferences.recordingLocation)
                            return i
                    }
                    return 0
                }
                onActivated: {
                    const choice = model[currentIndex].id
                    if (choice === "folder" && !recordingColumn.hasFolder
                            && !recordingColumn.chooseFolder()) {
                        // Cancelled or refused: back to what was set.
                        currentIndex = Qt.binding(() => {
                            for (var i = 0; i < model.length; ++i) {
                                if (model[i].id === EditorState.preferences.recordingLocation)
                                    return i
                            }
                            return 0
                        })
                        return
                    }
                    EditorState.preferences.recordingLocation = choice
                }
            }

            Row {
                width: parent.width
                spacing: Theme.spacingSm
                visible: EditorState.preferences.recordingLocation === "folder"
                         || recordingColumn.hasFolder

                ThemedLabel {
                    anchors.verticalCenter: parent.verticalCenter
                    width: parent.width - chooseRecordingFolder.width - parent.spacing
                    text: recordingColumn.hasFolder ? EditorState.preferences.recordingFolder
                                                    : qsTr("No folder chosen")
                    font.family: Theme.monoFontFamily
                    elide: Text.ElideMiddle
                }
                ThemedButton {
                    id: chooseRecordingFolder
                    anchors.verticalCenter: parent.verticalCenter
                    variant: "secondary"
                    text: qsTr("Choose…")
                    visible: FileDialogs.supportsDirectoryPicker()
                    onClicked: recordingColumn.chooseFolder()
                }
            }

            // Said here rather than only when it bites: "Beside the project" cannot work until the
            // project has been saved somewhere, and the fallback happens mid-recording.
            ThemedLabel {
                width: parent.width
                size: "xs"
                tone: "muted"
                wrapMode: Text.WordWrap
                visible: EditorState.preferences.recordingLocation === "project"
                text: qsTr("Until the project is saved, recordings go to your Movies folder.")
            }
        }
    }

    SettingsRow {
        title: qsTr("Save preview copies and reversed clips")
        searchId: "preview-proxy-location"
        description: qsTr("Where Drift saves the files it makes from your media. Changing the folder doesn’t move or delete anything. Proxies in the previous folder are no longer found; choosing that folder again finds them.")
        keywords: "proxy folder location cache reversed disk drive"
        stacked: true
        query: page.query

        Column {
            id: locationColumn
            width: parent.width
            spacing: Theme.spacingSm

            readonly property bool hasFolder: EditorState.preferences.proxyFolder.length > 0

            function chooseFolder() {
                const url = FileDialogs.openDirectory(qsTr("Folder for proxies"),
                                                      EditorState.preferences.proxyFolderUrl())
                if (!url || url.toString() === "")
                    return false
                const reason = EditorState.preferences.setProxyFolder(url.toString())
                if (reason.length > 0) {
                    Toasts.error(reason)
                    return false
                }
                EditorState.preferences.proxyLocation = "folder"
                return true
            }

            ThemedComboBox {
                id: locationCombo
                width: Theme.c.settingsRow.controlWidth
                textRole: "label"
                valueRole: "id"
                model: EditorState.preferences.proxyFolderSupported
                       ? [
                           { id: "beside", label: qsTr("Beside the original") },
                           { id: "cache", label: qsTr("In Drift’s cache") },
                           { id: "folder", label: qsTr("In this folder…") }
                         ]
                       : [
                           { id: "beside", label: qsTr("Beside the original") },
                           { id: "cache", label: qsTr("In Drift’s cache") }
                         ]
                currentIndex: {
                    for (var i = 0; i < model.length; ++i) {
                        if (model[i].id === EditorState.preferences.proxyLocation)
                            return i
                    }
                    return 0
                }
                onActivated: {
                    const choice = model[currentIndex].id
                    if (choice === "folder" && !locationColumn.hasFolder
                            && !locationColumn.chooseFolder()) {
                        // Cancelled or refused: back to what was set.
                        currentIndex = Qt.binding(() => {
                            for (var i = 0; i < model.length; ++i) {
                                if (model[i].id === EditorState.preferences.proxyLocation)
                                    return i
                            }
                            return 0
                        })
                        return
                    }
                    EditorState.preferences.proxyLocation = choice
                    EditorState.preferences.refreshProxyFolderUsage()
                }
            }

            Row {
                width: parent.width
                spacing: Theme.spacingSm
                visible: EditorState.preferences.proxyLocation === "folder" || locationColumn.hasFolder

                ThemedLabel {
                    anchors.verticalCenter: parent.verticalCenter
                    width: parent.width - chooseProxyFolder.width - parent.spacing
                    text: locationColumn.hasFolder ? EditorState.preferences.proxyFolder
                                                   : qsTr("No folder chosen")
                    font.family: Theme.monoFontFamily
                    elide: Text.ElideMiddle
                }
                ThemedButton {
                    id: chooseProxyFolder
                    anchors.verticalCenter: parent.verticalCenter
                    variant: "secondary"
                    text: qsTr("Choose…")
                    visible: EditorState.preferences.proxyFolderSupported
                    onClicked: locationColumn.chooseFolder()
                }
            }

            ThemedLabel {
                width: parent.width
                size: "xs"
                tone: "muted"
                wrapMode: Text.WordWrap
                visible: locationColumn.hasFolder && EditorState.preferences.proxyFolderBytes >= 0
                text: qsTr("Drift’s files in this folder: %1")
                          .arg(DownloadFormat.formatBytes(EditorState.preferences.proxyFolderBytes) || qsTr("0 B"))
            }

            ThemedLabel {
                width: parent.width
                size: "xs"
                tone: "muted"
                wrapMode: Text.WordWrap
                visible: EditorState.preferences.proxyLocation === "cache"
                text: qsTr("Drift’s cache is pruned when it passes its size limit.")
            }

            Component.onCompleted: EditorState.preferences.refreshProxyFolderUsage()
            Connections {
                target: EditorState.preferences
                function onProxyFolderChanged() { EditorState.preferences.refreshProxyFolderUsage() }
            }
        }
    }

    SettingsRow {
        title: qsTr("Preview copy name patterns")
        searchId: "preview-proxy-name-patterns"
        description: qsTr("Linking a proxy teaches Drift its name pattern. Proxies next to your media that match a pattern below link automatically.")
        keywords: "proxy naming file name regex"
        stacked: true
        query: page.query

        Column {
            width: parent.width
            spacing: Theme.spacingSm

            Repeater {
                model: EditorState.preferences.proxyNamePatterns

                Row {
                    id: patternRow
                    required property string modelData
                    width: parent.width
                    spacing: Theme.spacingSm

                    ThemedLabel {
                        anchors.verticalCenter: parent.verticalCenter
                        width: parent.width - removePattern.width - parent.spacing
                        text: patternRow.modelData
                        font.family: Theme.monoFontFamily
                        elide: Text.ElideMiddle
                    }
                    ThemedButton {
                        id: removePattern
                        anchors.verticalCenter: parent.verticalCenter
                        variant: "ghost"
                        glyph: Theme.icons.x
                        tooltip: qsTr("Remove pattern")
                        onClicked: EditorState.preferences.proxyNamePatterns =
                            EditorState.preferences.proxyNamePatterns.filter(p => p !== patternRow.modelData)
                    }
                }
            }

            ThemedButton {
                variant: "secondary"
                text: qsTr("Reset to defaults")
                onClicked: EditorState.preferences.resetProxyNamePatterns()
            }
        }
    }
}
