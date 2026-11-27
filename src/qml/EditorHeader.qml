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

    function focusAsk() {
        searchField.focusAsk()
    }

    // Clears the field and hands the keyboard back to the editor, so shortcuts work again.
    function endSearch() {
        searchField.dismiss()
        ShellBridge.main.focusEditor()
    }

    function closeTopDialog() {
        const dialogs = [unsavedDialog, exportDialogLoader.item, exportProgressDialogLoader.item,
                         projectPropertiesDialogLoader.item, projectHealthDialogLoader.item, agentAccessDialog, videoSizeDialogLoader.item, bakesDialogLoader.item, renderQueueDialogLoader.item,
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

    // A project as a folder of JSON files that editors and agents can change on disk.
    function openProjectFolder() {
        if (ShellBridge.main.rejectIfProjectOpenPending())
            return
        root.confirmIfDirty(function () {
            var url = FileDialogs.openDirectory(qsTr("Open Project Folder"))
            if (url != "")
                EditorState.projectFile.loadProject(url)
        })
    }

    function saveProjectAsFolder() {
        var url = FileDialogs.openDirectory(qsTr("Choose where to create the project folder"))
        if (url != "")
            EditorState.projectFile.folder.saveAsFolder(url, EditorState.projectFile.projectName)
    }

    function exportDriftBundle() {
        var url = FileDialogs.saveFile(qsTr("Export .drift Bundle"), root.projectFilter,
                                       EditorState.projectFile.projectName, "drift", "",
                                       root.projectMimeTypes)
        if (url != "")
            EditorState.projectFile.folder.exportBundle(url)
    }

    // Save As with every source file copied in, so the result opens on a machine that has none of
    // the media. Always asks for a path: it is a different artefact from the working save.
    function packageProject() {
        var url = FileDialogs.saveFile(qsTr("Save with media"), root.projectFilter,
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

    function openRenderQueue() {
        renderQueueDialogLoader.ensure().openDialog()
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
        function onSaveFailed(error) { saveFailedDialog.openWith(error) }
        function onProjectSaved(ok) {
            if (!ok || !unsavedDialog.visible || !root._pendingAfterUnsaved)
                return
            const action = root._pendingAfterUnsaved
            root._pendingAfterUnsaved = null
            unsavedDialog.close()
            action()
        }
    }

    ThemedDialog {
        id: saveFailedDialog
        property string errorText: ""
        function openWith(error) {
            errorText = error
            open()
        }
        title: qsTr("Project not saved")
        showReject: false
        preferredWidth: Theme.dialogWidthMd

        contentItem: ThemedLabel {
            wrapMode: Text.Wrap
            size: "sm"
            tone: "default"
            text: saveFailedDialog.errorText.length > 0
                  ? qsTr("The project couldn't be saved, and your changes are not on disk yet: %1").arg(saveFailedDialog.errorText)
                  : qsTr("The project couldn't be saved, and your changes are not on disk yet.")
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
        id: templateRenderDialogLoader
        sourceComponent: Component { TemplateRenderDialog { } }
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

    LazyLoader {
        id: projectHealthDialogLoader
        sourceComponent: Component { ProjectHealthDialog { } }
    }

    LazyLoader {
        id: renderQueueDialogLoader
        sourceComponent: Component { RenderQueueDialog { } }
    }

    LazyLoader {
        id: bakesDialogLoader
        sourceComponent: Component { BakesDialog { } }
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
                onExportRequested: root.exportVideo()
                onRenderQueueRequested: root.openRenderQueue()
                onPackageRequested: root.packageProject()
                onSaveJsonRequested: root.saveProjectJson()
                onOpenJsonRequested: root.openProjectJson()
                onOpenFolderRequested: root.openProjectFolder()
                onSaveFolderRequested: root.saveProjectAsFolder()
                onExportBundleRequested: root.exportDriftBundle()
                onRenderTemplateRequested: templateRenderDialogLoader.ensure().openDialog()
                onImportExportRequested: ShellBridge.main?.openInterchange("import", "")
                onPropertiesRequested: projectPropertiesDialogLoader.ensure().openDialog()
                onHealthRequested: projectHealthDialogLoader.ensure().openDialog()
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

            EditorHeaderVideoButton {
                dialogShown: videoSizeDialogLoader.shown || bakesDialogLoader.shown
                onBakesRequested: bakesDialogLoader.ensure().openDialog()
                onLayoutRequested: videoSizeDialogLoader.ensure().openDialog()
            }

            EditorHeaderSeparator {}

            EditorHeaderWorkspaceButton { }
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
            onBakesRequested: bakesDialogLoader.ensure().openDialog()
            onCropRequested: EditorState.preview.canvasCropMode = true
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

            EditorHeaderHelpButton { }

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
