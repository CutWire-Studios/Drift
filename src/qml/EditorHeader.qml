import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Window
import Drift

Item {
    id: root

    // Main owns the window; the header only asks for it.
    signal downloadsRequested()

    height: Theme.headerHeight

    ThemeSlot {
        anchors.fill: parent
        slot: "EditorHeaderBackground"
        control: root
        fallback: Component {
            Rectangle {
                anchors.fill: parent
                color: Theme.c.editorHeader.background
            }
        }
    }

    property string projectName: EditorState.projectFile.projectName

    // Disabled: external project imports (Premiere Pro, DaVinci Resolve/FCPXML, Kdenlive/Shotcut,
    // .mogrt, EDL, OTIO) need more fixing before shipping. Uncomment these entries, the open*()
    // functions below and the RecentProjectsPopup connections when the readers are stable.
    readonly property var projectFilter: [
        qsTr("Drift project (*.drift)"),
        qsTr("JSON document (*.json)"),
        qsTr("All Files (*)")
    ]
    readonly property var projectMimeTypes: [
        "application/x-drift-project",
        "application/xml",
        "text/xml",
        "application/zip",
        "application/octet-stream"
    ]

    // Action to run after Save or Don't Save resolves. Null when idle.
    property var _pendingAfterUnsaved: null

    // Runs `action` immediately when clean; otherwise opens the unsaved prompt.
    // Used by New / Open / Recent / Quit and the matching shortcuts.
    // Android Back: closes the topmost dialog this header owns. True if one was open, even when
    // it swallowed the key.
    function focusSearch() {
        searchField.focusField()
    }

    // Clears the field and hands the keyboard back to the editor, so shortcuts work again.
    function endSearch() {
        searchField.dismiss()
        ShellBridge.main.focusEditor()
    }

    function closeTopDialog() {
        const dialogs = [unsavedDialog, exportDialogLoader.item, exportProgressDialogLoader.item,
                         projectPropertiesDialogLoader.item, agentAccessDialog, videoSizeDialogLoader.item,
                         languageChooserDialogLoader.item, packageProgressDialog]
        for (var i = 0; i < dialogs.length; ++i) {
            const d = dialogs[i]
            if (!d || !d.visible)
                continue
            // Packaging cancels on close; the dialog's own button is the way out.
            if (d !== packageProgressDialog && (d.closePolicy & Popup.CloseOnEscape))
                d.close()
            return true
        }
        return false
    }

    function confirmIfDirty(action) {
        if (!EditorState.projectFile.hasUnsavedChanges) {
            action()
            return
        }
        root._pendingAfterUnsaved = action
        unsavedDialog.openDialog()
    }

    // New / Open / Recent / Close all delegate to the window: it's the one place
    // that owns the confirm-if-dirty gate for these together with the start
    // screen's show/hide bookkeeping, so Ctrl+N/Ctrl+O, this header's Projects
    // menu, and the start screen's own tiles can't drift out of sync.
    function openProject() {
        ShellBridge.main.requestOpenProjectDialog()
    }

    function requestNewProject() {
        ShellBridge.main.requestNewProject()
    }

    function openRecent(path) {
        ShellBridge.main.requestOpenRecentProject(path)
    }

    function closeProject() {
        ShellBridge.main.requestCloseProject()
    }

    // Returns true when the project is clean after the attempt. False if the write failed or is
    // still pending — callers must not continue. A first save asks for the project name before
    // the picker (the name is the title, independent of the file name), so it always returns
    // false; onProjectSaved runs any action queued behind the unsaved-changes dialog.
    function saveProject() {
        if (EditorState.projectFile.currentProjectPath && EditorState.projectFile.currentProjectPath.length > 0) {
            EditorState.projectFile.saveProject(EditorState.fileUrl(EditorState.projectFile.currentProjectPath))
            return !EditorState.projectFile.hasUnsavedChanges
        }
        projectNameDialog.openWith(qsTr("Name your project"), EditorState.projectFile.projectName)
        return false
    }

    function saveProjectToNewFile() {
        var url = FileDialogs.saveFile(qsTr("Save Project"), root.projectFilter,
                                       EditorState.projectFile.projectName, "drift", "",
                                       root.projectMimeTypes)
        if (url != "")
            EditorState.projectFile.saveProject(url)
    }

    // Save As: writes the open project to a new .drift and keeps editing that one, leaving the
    // file it came from exactly as it was on disk. The suggested name is the project's plus
    // "copy", so accepting the picker's default cannot overwrite the original.
    function saveProjectAs() {
        var url = FileDialogs.saveFile(qsTr("Save Project As"), root.projectFilter,
                                       qsTr("%1 copy").arg(EditorState.projectFile.projectName), "drift", "",
                                       root.projectMimeTypes)
        if (url == "")
            return false
        EditorState.projectFile.saveProjectAs(url)
        return !EditorState.projectFile.hasUnsavedChanges
    }

    // Raw document JSON: an export, not a project file. Always asks for a path and leaves the
    // current .drift association untouched.
    function saveProjectJson() {
        var url = FileDialogs.saveFile(qsTr("Save Project JSON"),
                                       [qsTr("JSON document (*.json)")],
                                       EditorState.projectFile.projectName, "json", "",
                                       ["application/json"])
        if (url != "")
            EditorState.projectFile.saveProjectJson(url)
    }

    // Inverse of saveProjectJson. Confirms unsaved work like Open, because it replaces the
    // timeline. The JSON does not become the current project path.
    //
    // loadProjectJson() itself rejects a second call while another load is still in
    // flight (see AppController::beginProjectLoad), so this check only saves the user a
    // trip through the file dialog for a request the backend would refuse anyway.
    function openProjectJson() {
        if (ShellBridge.main.rejectIfProjectOpenPending())
            return
        root.confirmIfDirty(function () {
            var url = FileDialogs.openFile(qsTr("Open Project JSON"),
                                           [qsTr("JSON document (*.json)")],
                                           ["application/json"])
            if (url != "")
                EditorState.projectFile.loadProjectJson(url)
        })
    }

    // Disabled: external project imports (Premiere Pro, DaVinci Resolve/FCPXML, Kdenlive/Shotcut,
    // .mogrt, EDL, OTIO). Uncomment alongside the loadProject() routing in AppController once the
    // readers are stable.
    //
    // function openPremiereProject() {
    //     root.confirmIfDirty(function () {
    //         var url = FileDialogs.openFile(qsTr("Import Premiere Pro Project"),
    //                                        [qsTr("Premiere Pro project (*.prproj *.xml)"),
    //                                         qsTr("Premiere Pro project (*.prproj)"),
    //                                         qsTr("Final Cut Pro XML (*.xml)")],
    //                                        ["application/xml", "text/xml"])
    //         if (url != "")
    //             EditorState.projectFile.loadPremiereProject(url)
    //     })
    // }
    //
    // function openMogrt() {
    //     var url = FileDialogs.openFile(qsTr("Import Motion Graphics Template"),
    //                                    [qsTr("Motion Graphics Template (*.mogrt)"),
    //                                     qsTr("All Files (*)")],
    //                                    ["application/zip", "application/octet-stream"])
    //     if (url != "")
    //         EditorState.projectFile.importMogrt(url)
    // }
    //
    // function openKdenliveProject() {
    //     root.confirmIfDirty(function () {
    //         var url = FileDialogs.openFile(qsTr("Import Kdenlive / Shotcut Project"),
    //                                        [qsTr("Kdenlive & Shotcut project (*.kdenlive *.mlt)"),
    //                                         qsTr("Kdenlive project (*.kdenlive)"),
    //                                         qsTr("Shotcut project (*.mlt)")],
    //                                        ["application/xml", "text/xml", "application/x-kdenlive"])
    //         if (url != "")
    //             EditorState.projectFile.loadKdenliveProject(url)
    //     })
    // }
    //
    // function openResolveProject() {
    //     root.confirmIfDirty(function () {
    //         var url = FileDialogs.openFile(qsTr("Import DaVinci Resolve Project / FCPXML"),
    //                                        [qsTr("DaVinci Resolve project (*.drp *.fcpxml)"),
    //                                         qsTr("DaVinci Resolve project archive (*.drp)"),
    //                                         qsTr("Final Cut Pro X XML (*.fcpxml)")],
    //                                        ["application/zip", "application/octet-stream", "application/xml", "text/xml"])
    //         if (url != "")
    //             EditorState.projectFile.loadResolveProject(url)
    //     })
    // }
    //
    // function openEdlTimeline() {
    //     root.confirmIfDirty(function () {
    //         var url = FileDialogs.openFile(qsTr("Import Edit Decision List (.edl)"),
    //                                        [qsTr("Edit Decision List (*.edl)")],
    //                                        ["text/plain", "application/octet-stream"])
    //         if (url != "")
    //             EditorState.projectFile.loadEdlTimeline(url)
    //     })
    // }
    //
    // function openOtioTimeline() {
    //     root.confirmIfDirty(function () {
    //         var url = FileDialogs.openFile(qsTr("Import OpenTimelineIO (.otio)"),
    //                                        [qsTr("OpenTimelineIO sequence (*.otio)")],
    //                                        ["application/json", "text/plain", "application/octet-stream"])
    //         if (url != "")
    //             EditorState.projectFile.loadOtioTimeline(url)
    //     })
    // }

    // Save As with every source file copied in, so the result opens on a machine that has none of
    // the media. Always asks for a path: it is a different artefact from the working save.
    function packageProject() {
        var url = FileDialogs.saveFile(qsTr("Save Shareable Copy"), root.projectFilter,
                                       EditorState.projectFile.projectName, "drift", "",
                                       root.projectMimeTypes)
        if (url != "")
            EditorState.projectFile.packageProject(url)
    }

    // The start screen offers these too, so they work before any project is open. The language
    // chooser here is the header's own, not Main's first-launch one, whose close resumes startup.
    function openAgentAccess() {
        agentAccessDialog.openDialog()
    }

    function exportVideo() {
        exportDialogLoader.ensure().openDialog()
    }

    // True once the user has dismissed the progress dialog while an export is
    // still running; drives the circular-progress badge next to Export.
    property bool exportProgressDismissed: false

    Connections {
        target: EditorState

        function onExportInProgressChanged() {
            if (EditorState.exportInProgress) {
                root.exportProgressDismissed = false
                exportProgressDialogLoader.ensure().openDialog()
            } else if (EditorState.exportByAgent && exportProgressDialogLoader.item) {
                // An agent's export has no one waiting on the "Export finished" state; the
                // toast reports the result and the agent gets it from export_status.
                exportProgressDialogLoader.item.close()
            }
        }
    }

    Connections {
        target: EditorState.projectFile
        function onProjectNameChanged() { root.projectName = EditorState.projectFile.projectName }
        function onSaveRequested() { root.saveProject() }
        function onSaveAsRequested() { root.saveProjectAs() }
        function onOpenRequested() { root.openProject() }
        function onNewProjectRequested() { root.requestNewProject() }
        // A synchronous save also lands here while _pendingAfterUnsaved is set; onSaveChosen
        // then finds it already cleared, so the action still runs once.
        function onProjectSaved(ok) {
            if (!ok || !unsavedDialog.visible || !root._pendingAfterUnsaved)
                return
            const action = root._pendingAfterUnsaved
            root._pendingAfterUnsaved = null
            unsavedDialog.close()
            action()
        }
    }

    NameDialog {
        id: projectNameDialog
        placeholder: qsTr("Untitled Project")
        onSubmitted: function (name) {
            EditorState.projectFile.projectName = name
            Qt.callLater(root.saveProjectToNewFile)
        }
    }

    LazyLoader {
        id: exportDialogLoader
        sourceComponent: Component { ExportDialog { } }
    }

    LazyLoader {
        id: exportProgressDialogLoader
        sourceComponent: Component {
            ExportProgressDialog {
                onClosed: if (EditorState.exportInProgress) root.exportProgressDismissed = true
            }
        }
    }

    LazyLoader {
        id: projectPropertiesDialogLoader
        sourceComponent: Component { ProjectPropertiesDialog { } }
    }

    PackageProgressDialog {
        id: packageProgressDialog
    }

    LazyLoader {
        id: languageChooserDialogLoader
        sourceComponent: Component { LanguageChooserDialog { } }
    }

    AgentAccessDialog {
        id: agentAccessDialog
    }

    LazyLoader {
        id: videoSizeDialogLoader
        sourceComponent: Component { VideoSizeDialog { } }
    }

    UnsavedChangesDialog {
        id: unsavedDialog

        // Null pending *before* close(): Dialog.close() rejects, and onRejected
        // must not wipe an action we are about to run.
        onSaveChosen: {
            if (!root.saveProject())
                return
            const action = root._pendingAfterUnsaved
            root._pendingAfterUnsaved = null
            close()
            if (action)
                action()
        }
        onDiscardChosen: {
            const action = root._pendingAfterUnsaved
            root._pendingAfterUnsaved = null
            close()
            if (action)
                action()
        }
        onRejected: root._pendingAfterUnsaved = null
    }

    EditorHeaderHairline { }

    // The three groups used to be independently anchored, so at narrow widths
    // they overlapped instead of compressing. A RowLayout arbitrates the space.
    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.pagePadding
        anchors.rightMargin: Theme.pagePadding
        anchors.verticalCenterOffset: 1
        spacing: Theme.spacingLg

        // --- Left: project switcher + save ------------------------------------
        Row {
            Layout.alignment: Qt.AlignVCenter
            spacing: Theme.spacingLg

            EditorHeaderProjectsButton {
                projectName: root.projectName
                onOpenFileRequested: root.openProject()
                onNewProjectRequested: root.requestNewProject()
                onOpenRecentRequested: (path) => root.openRecent(path)
                onCloseProjectRequested: root.closeProject()
                onSaveAsRequested: root.saveProjectAs()
                onPackageRequested: root.packageProject()
                onSaveJsonRequested: root.saveProjectJson()
                onOpenJsonRequested: root.openProjectJson()
                onPropertiesRequested: projectPropertiesDialogLoader.ensure().openDialog()
            }

            IconButton {
                glyph: Theme.icons.save
                variant: "ghost"
                text: qsTr("Save")
                tooltip: {
                    const keys = EditorState.shortcutFor("save")
                    return keys.length > 0 ? qsTr("Save project (%1)").arg(Theme.shortcutDisplay(keys))
                                           : qsTr("Save project")
                }
                anchors.verticalCenter: parent.verticalCenter
                onClicked: root.saveProject()
            }

            IconButton {
                glyph: Theme.icons.ratio
                variant: "ghost"
                text: qsTr("Video")
                active: videoSizeDialogLoader.shown || EditorState.preview.canvasCropMode
                tooltip: qsTr("Video size and layout")
                anchors.verticalCenter: parent.verticalCenter
                onClicked: videoSizeDialogLoader.ensure().openDialog()
            }
        }

        // Absorbs leftover space so the groups stay apart but can compress.
        Item {
            Layout.fillWidth: true
            Layout.minimumWidth: 0
        }

        EditorHeaderSearchField {
            id: searchField
            Layout.alignment: Qt.AlignVCenter
            Layout.fillWidth: true
            Layout.minimumWidth: 260
            Layout.maximumWidth: 520
            onExportRequested: root.exportVideo()
            onDownloadsRequested: root.downloadsRequested()
            onLanguageRequested: languageChooserDialogLoader.ensure().openFromHeader()
            onVideoSizeRequested: videoSizeDialogLoader.ensure().openDialog()
            onSearchFinished: root.endSearch()
        }

        Item {
            Layout.fillWidth: true
            Layout.minimumWidth: 0
        }

        // --- Right: appearance | extras | agent | infrequent | export ------
        Row {
            Layout.alignment: Qt.AlignVCenter
            spacing: Theme.spacingSm

            EditorHeaderDownloadsButton { }

            EditorHeaderSettingsButton { }

            EditorHeaderSeparator {}

            EditorHeaderExtrasButton { }

            EditorHeaderNotificationsButton { }

            EditorHeaderSeparator {}

            IconButton {
                glyph: Theme.icons.bot
                variant: "ghost"
                text: qsTr("Agent")
                active: EditorState.mcp.running
                tooltip: EditorState.mcp.running
                         ? qsTr("Agent access is on")
                         : qsTr("Agent access")
                anchors.verticalCenter: parent.verticalCenter
                onClicked: agentAccessDialog.openDialog()
            }

            EditorHeaderSeparator {}

            EditorHeaderExportBadge {
                dismissed: root.exportProgressDismissed
                onReopenRequested: {
                    root.exportProgressDismissed = false
                    exportProgressDialogLoader.ensure().openDialog()
                }
            }

            EditorHeaderExportButton {
                onExportRequested: root.exportVideo()
            }
        }
    }
}
