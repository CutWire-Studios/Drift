import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Window
import Drift
// Android entry shell: home (layout + recent + import) → CapCut editor.
ApplicationWindow {
    id: window
    visible: true
    // Android never starts translucent; windowBackground forces alpha to 1 so a
    // theme token with alpha cannot clear this surface to black.
    color: {
        const opacity = ThemeManager.window.backgroundOpacity
        const multiplier = (opacity === undefined || opacity === null) ? 1 : opacity
        return WindowBackdrop.windowBackground(Theme.c.androidMain.background, multiplier)
    }
    title: "CutWire Drift"

    // On Android the platform sizes the window. Chosen on a desktop, this shell opens phone-shaped
    // instead of filling a desktop-sized default; it stays resizable, and nothing is persisted.
    readonly property bool _desktopHost: Qt.platform.os !== "android" && Qt.platform.os !== "ios"
    width: _desktopHost ? 420 : 0
    height: _desktopHost ? 860 : 0
    minimumWidth: _desktopHost ? 320 : 0
    minimumHeight: _desktopHost ? 560 : 0

    // Theme is a singleton and cannot see a window, so the size class it reports has to be fed
    // from whichever root is live. Screen is the wrong source: it ignores tiling and split view.
    onWidthChanged: Theme.windowWidth = width

    Binding {
        target: Theme
        property: "keyboardInset"
        value: {
            if (Qt.platform.os !== "android" && Qt.platform.os !== "ios")
                return 0
            const r = Qt.inputMethod.keyboardRectangle
            return Qt.inputMethod.visible && r.height > 0 ? Math.max(0, window.height - r.y) : 0
        }
    }

    // Qt creates some chrome itself — most visibly the Undo/Cut/Copy/Paste menu on every
    // TextField and TextArea (Basic/TextField.qml declares ContextMenu.menu). Drift styles
    // none of that, so it fell through to the palette the platform theme supplies and picked
    // up the desktop's colour scheme: on a KDE session with a custom scheme the editing menu
    // rendered in that scheme's colours next to Drift's own. Palette propagates down the item
    // hierarchy, popups included, so setting the roles the Basic style reads brings Qt's own
    // chrome under Theme — and binding them keeps it following the light/dark toggle.
    palette.window: Theme.c.androidMain.paletteWindow
    palette.windowText: Theme.c.androidMain.paletteWindowText
    palette.base: Theme.c.androidMain.paletteBase
    palette.text: Theme.c.androidMain.paletteText
    palette.button: Theme.c.androidMain.paletteButton
    palette.buttonText: Theme.c.androidMain.paletteButtonText
    palette.placeholderText: Theme.c.androidMain.palettePlaceholder
    // Menu border and item states: dark draws the frame, light the hovered row,
    // midlight the pressed one.
    palette.dark: Theme.c.androidMain.paletteDark
    palette.mid: Theme.c.androidMain.paletteMid
    palette.midlight: Theme.c.androidMain.paletteMidlight
    palette.light: Theme.c.androidMain.paletteLight
    // Drawn at 12% and 50% alpha for the menu's drop shadow; appBackground would
    // make that a white glow in light mode.
    palette.shadow: Theme.c.androidMain.paletteShadow
    palette.highlight: Theme.c.androidMain.paletteHighlight
    palette.highlightedText: Theme.c.androidMain.paletteHighlightedText
    palette.toolTipBase: Theme.c.androidMain.paletteToolTipBase
    palette.toolTipText: Theme.c.androidMain.paletteToolTipText

    readonly property var projectFilter: [qsTr("Drift project (*.drift)")]
    property bool inEditor: false
    property bool forceClose: false
    property var _pendingAfterUnsaved: null
    // A .drift launch intent parked until the recovery prompt has been answered. Loading a
    // project deletes the autosave snapshot, so opening the launched file first would throw
    // away the very work the prompt is asking about.
    property string _pendingLaunchUrl: ""
    // Open menus, newest last. A Popup is not an Item and the window overlay only exposes
    // opaque popup items, so there is nothing to enumerate — each themed menu registers
    // itself here instead, and Back closes the top one.
    property var _openMenus: []
    // How many Overlay-hosted dialogs/sheets are open. PointerHandlers on the
    // editor hit-test their parent bounds, not z-order, so a tap on empty dialog
    // chrome still reached the timeline; this count drives a guard over the page.
    property int overlayModalCount: 0

    function pushOverlayModal() {
        overlayModalCount++
    }

    function popOverlayModal() {
        overlayModalCount = Math.max(0, overlayModalCount - 1)
    }

    // The live AndroidEditor instance, so Back can ask it to close a sheet first.
    property var editorPage: null
    // The live AndroidHome instance, so Back can refuse to leave mid-import.
    property var homePage: null
    // The preview-and-edit screen while it is on the stack; Back pops it before anything else.
    property var mediaPreviewPage: null
    // The full-screen Settings or Extras page while it is on the stack; Back pops it first.
    property var subPage: null
    property string subPageKind: ""

    function confirmIfDirty(action) {
        if (!EditorState.projectFile.hasUnsavedChanges) {
            action()
            return
        }
        window._pendingAfterUnsaved = action
        unsavedDialog.openDialog()
    }

    function saveProject() {
        if (EditorState.projectFile.currentProjectPath && EditorState.projectFile.currentProjectPath.length > 0) {
            EditorState.projectFile.saveProject(EditorState.fileUrl(EditorState.projectFile.currentProjectPath))
            return !EditorState.projectFile.hasUnsavedChanges
        }
        // The name is the project's title, independent of the file name, so it is asked for
        // before the picker. The save finishes later; onProjectSaved runs any queued action.
        projectNameDialog.openWith(qsTr("Name your project"), EditorState.projectFile.projectName)
        return false
    }

    function saveProjectToNewFile() {
        const url = FileDialogs.saveFile(qsTr("Save Project"), window.projectFilter,
                                         EditorState.projectFile.projectName, "drift")
        if (url !== "")
            EditorState.projectFile.saveProject(url)
    }

    NameDialog {
        id: projectNameDialog
        placeholder: qsTr("Untitled Project")
        onSubmitted: function (name) {
            EditorState.projectFile.projectName = name
            Qt.callLater(window.saveProjectToNewFile)
        }
    }

    // The canvas is inferred from the first clip instead of asked about. shouldConfigure...()
    // returns false the moment projectLayoutChosen is set, so marking it here does not suppress
    // a dialog — it answers the question the dialog would have asked. Every first-clip route on
    // the phone goes through this one function, drop-on-timeline included, so they all inherit
    // the behaviour without touching a single call site.
    function applyInferredSetup(assetIndex) {
        // A source-derived canvas needs a probed source. The probe runs off-thread, so an asset
        // that has not finished one reports 0x0 — and suggestedProjectSetupForAsset() then falls
        // back to the *project defaults* rather than failing, which markProjectLayoutChosen()
        // would lock in as though it had come from the clip. MediaImport holds its callback until
        // the probe lands; this guards the case where it never does, and the audio-first case,
        // where there are no dimensions to infer from at all.
        const asset = AssetLibrary.assetAt(assetIndex)
        if (!asset || !(asset.width > 0) || !(asset.height > 0))
            return false
        const setup = EditorState.suggestedProjectSetupForAsset(assetIndex)
        if (!setup || !setup.width || !setup.height)
            return false
        EditorState.projectFile.setProjectSetup(setup.width, setup.height, setup.fps)
        EditorState.markProjectLayoutChosen()
        Toasts.info(qsTr("Canvas set to %1×%2 at %3 fps from your first clip.")
                    .arg(setup.width).arg(setup.height).arg(setup.fps))
        return true
    }

    function configureAndAddAsset(assetIndex, runner) {
        if (EditorState.shouldConfigureProjectForAsset(assetIndex))
            window.applyInferredSetup(assetIndex)
        runner()
    }

    // Run once the canvas sheet is committed with Done, and cleared if it is dismissed instead.
    property var _afterLayoutChosen: null

    // "New project" is the deliberate route: choose the canvas, then land in an empty editor and
    // add media from there. Quick edit is the other half of the pair — it asks for a clip and
    // infers the canvas from it — and between them neither path has to answer a question it does
    // not care about.
    function startNewProject() {
        window.confirmIfDirty(function() {
            EditorState.projectFile.newProject()
            window._afterLayoutChosen = function() { window.showEditor() }
            layoutSheet.openSheet()
        })
    }

    // Home's "Quick edit" tile, and later the share targets: one clip, straight onto a
    // fresh timeline, in the ordinary editor. Zero dialogs on the way — applyInferredSetup()
    // calls markProjectLayoutChosen(), which is what shouldConfigureProjectForAsset() reads,
    // so the canvas question is answered rather than suppressed.
    //
    // Takes urls, not an asset index: newProject() swaps the whole document, asset pool
    // included, so anything imported before it is gone by the time addClipFromAsset() would
    // run. The import has to happen on the far side of the reset, which means the caller can
    // only hand over something that survives it. Pass nothing to open the picker.
    // `then(assetIndex)` runs once the editor is up with the clip on the timeline.
    function beginQuickEdit(urls, then) {
        window.confirmIfDirty(function() {
            EditorState.projectFile.newProject()
            const landed = function(added) {
                let index = -1
                if (added > 0) {
                    index = AssetLibrary.count - added
                    window.applyInferredSetup(index)
                    EditorState.addClipFromAsset(index)
                }
                window.showEditor()
                if (then && index >= 0)
                    Qt.callLater(then, index)
            }
            if (urls && urls.length > 0)
                MediaImport.importUrls(urls, false, landed)
            else
                MediaImport.pickAndImport(landed)
        })
    }

    // Asset waiting for its preview-and-edit save, after which it goes onto a timeline and into
    // the export dialog. -1 when no crop/trim quick action is in flight.
    property int _pendingMediaEdit: -1

    // Crop and trim quick actions: the picked file opens in preview-and-edit on the matching tab,
    // and its Save (a non-destructive trim, or a crop re-encode) carries on to export.
    function beginMediaEdit(mode) {
        window.confirmIfDirty(function() {
            EditorState.projectFile.newProject()
            MediaImport.pickAndImport(function(added) {
                if (added <= 0)
                    return
                const index = AssetLibrary.count - added
                const asset = AssetLibrary.assetAt(index)
                const kind = asset ? asset.kind : ""
                if (mode === "crop" ? kind === "audio" : kind === "image") {
                    Toasts.info(mode === "crop" ? qsTr("Pick a video or image to crop.")
                                                : qsTr("Pick a video or audio file to trim."))
                    return
                }
                window._pendingMediaEdit = index
                window.openMediaPreview(index)
                if (window.mediaPreviewPage)
                    window.mediaPreviewPage.mode = mode
            })
        })
    }

    Connections {
        target: EditorState.bin
        function onEditFinished(ok, message) {
            if (!ok || EditorState.bin.editIsConversion || window._pendingMediaEdit < 0)
                return
            const index = window._pendingMediaEdit
            window._pendingMediaEdit = -1
            Qt.callLater(function() {
                window.applyInferredSetup(index)
                EditorState.addClipFromAsset(index)
                window.showEditor()
                Qt.callLater(function() {
                    if (window.editorPage)
                        window.editorPage.openExport(false)
                })
            })
        }
    }

    Connections {
        target: EditorState
        function onProjectReset() { window._pendingMediaEdit = -1 }
    }

    property real _stillSeconds: 5

    ThemedDialog {
        id: stillDurationDialog
        title: qsTr("Video length")
        acceptText: qsTr("Export")
        preferredWidth: Theme.dialogWidthSm
        onAccepted: {
            EditorState.setClipDuration(0, 0, window._stillSeconds)
            if (window.editorPage)
                window.editorPage.openExport(false)
        }

        contentItem: Column {
            spacing: Theme.spacingLg
            width: parent ? parent.width : Theme.dialogWidthSm

            ThemedLabel {
                text: qsTr("How long should the image play?")
                wrapMode: Text.WordWrap
                width: parent.width
            }

            ThemedNumberField {
                width: parent.width
                from: 0.5
                to: 300
                step: 0.5
                decimals: 1
                unit: "s"
                value: window._stillSeconds
                onEdited: (v) => window._stillSeconds = v
            }
        }
    }

    function openLayoutChooser() {
        layoutSheet.openSheet()
    }

    // Home's quick-action tiles other than "quick".
    function runQuickAction(id) {
        switch (id) {
        case "filter":
            window.beginQuickEdit(null, function() { window.editorPage.openTool("filters") })
            break
        case "adjust":
        case "text":
        case "sticker":
            window.beginQuickEdit(null, function() { window.editorPage.openTool(id) })
            break
        case "crop":
        case "trim":
            window.beginMediaEdit(id)
            break
        case "toAudio":
            window.beginQuickEdit(null, function() { window.editorPage.openExport(true) })
            break
        case "toVideo":
            window.beginQuickEdit(null, function(index) {
                const asset = AssetLibrary.assetAt(index)
                if (asset && asset.kind === "image") {
                    window._stillSeconds = 5
                    stillDurationDialog.open()
                } else {
                    window.editorPage.openExport(false)
                }
            })
            break
        default:
            window.beginQuickEdit(null)
        }
    }

    function showHome() {
        if (!window.inEditor)
            return
        if (stack.depth > 1)
            stack.pop()
        window.inEditor = false
    }

    function showEditor() {
        if (window.inEditor)
            return
        stack.push(editorComponent)
        window.inEditor = true
    }

    function goBack() {
        if (window.inEditor) {
            confirmIfDirty(function () {
                window.showHome()
            })
            return
        }
        if (EditorState.projectFile.hasUnsavedChanges) {
            confirmIfDirty(function () {
                EditorState.projectFile.discardUnsavedChanges()
                window.forceClose = true
                window.close()
            })
        } else {
            window.forceClose = true
            window.close()
        }
    }

    // A share that arrived from another app, cold or warm. Media goes onto a timeline; text is
    // a link and belongs to the marketplace resolver, which step 6 fills in.
    function routeIncomingIntent(intent) {
        if (!intent || !intent.kind)
            return
        if (intent.kind === "view") {
            const url = intent.urls && intent.urls.length > 0 ? String(intent.urls[0]) : ""
            if (url === "" || Market.handleIncomingUrl(url))
                return
            if (Addons.isUserPackage(url)) {
                window.importUserPackage(url)
                return
            }
            window.confirmIfDirty(function () {
                EditorState.projectFile.loadProject(url)
                Qt.callLater(window.showEditor)
            })
            return
        }
        if (intent.kind === "sendMedia") {
            const urls = intent.urls || []
            if (urls.length === 0)
                return
            // Already working on something: adding to it and starting fresh are both reasonable
            // and only the user knows which, so ask rather than guess.
            if (window.inEditor) {
                shareTargetSheet.openFor(urls)
                return
            }
            window.beginQuickEdit(urls)
            return
        }
        if (intent.kind === "sendText")
            window.routeSharedLink(intent.text || "")
    }

    // TikTok and Instagram share "Check this out … https://…", so the link is a token inside the
    // text rather than the whole of it.
    function routeSharedLink(text) {
        const match = /https?:\/\/\S+/i.exec(String(text || ""))
        if (!match) {
            Toasts.warning(qsTr("That share had no link in it."))
            return
        }
        window.openLinkImport(match[0])
    }

    // A shared link. The marketplace is what knows how to turn a page URL into media, so with no
    // marketplace configured there is nothing to degrade to but saying so.
    //
    // Handled by the editor's link-import sheet. Parked until an editor exists: a share can
    // cold-start the app, and from Home it first opens an empty project to host the sheet.
    property string pendingLinkUrl: ""

    function openLinkImport(url) {
        if (!Market.configured) {
            Toasts.info(qsTr("Links can’t be opened in this build."))
            return
        }
        window.pendingLinkUrl = url
        window.deliverPendingLink()
    }

    // A marketplace download has landed in the current project's bin. That is invisible from the
    // Market destination — the bin lives in the editor — which is why finishing there looked like
    // nothing had happened at all. Put the clip on a timeline and show it.
    //
    // No newProject() here, unlike the picker path: the asset is already *in* this project, and
    // replacing the document would throw it away. shouldConfigureProjectForAsset() is what
    // decides whether this is the first clip and the canvas should follow it.
    function openDownloadedAsset(assetId) {
        const index = AssetLibrary.indexOfId(assetId)
        if (index < 0) {
            Toasts.warning(qsTr("That download is no longer in your media."))
            return
        }
        // Freshly imported, so unprobed: its size, frame rate and duration are all still zero.
        MediaImport.awaitProbes([assetId], function () {
            const at = AssetLibrary.indexOfId(assetId)
            if (at < 0)
                return
            window.configureAndAddAsset(at, function () {
                EditorState.addClipFromAsset(at)
                window.showEditor()
            })
        })
    }

    function deliverPendingLink() {
        if (window.pendingLinkUrl === "")
            return
        if (window.editorPage) {
            const url = window.pendingLinkUrl
            window.pendingLinkUrl = ""
            window.editorPage.startLinkImport(url)
            return
        }
        // Editor is being pushed; its onCompleted delivers.
        if (window.inEditor || !window.homePage)
            return
        window.confirmIfDirty(function() {
            EditorState.projectFile.newProject()
            window.showEditor()
        })
    }

    function openProjectFile() {
        confirmIfDirty(function () {
            const url = FileDialogs.openFile(qsTr("Open Project"), window.projectFilter)
            if (url !== "") {
                EditorState.projectFile.loadProject(url)
                showEditor()
            }
        })
    }

    function openRecent(path) {
        confirmIfDirty(function () {
            EditorState.projectFile.openRecentProject(path)
            showEditor()
        })
    }

    onClosing: function (close) {
        // Opting in to reopening the last project means the autosave is restored on the
        // next launch, so asking to save on the way out is a question already answered.
        if (window.forceClose || !EditorState.projectFile.hasUnsavedChanges || EditorState.preferences.reopenLastProject)
            return
        close.accepted = false
        confirmIfDirty(function () {
            EditorState.projectFile.discardUnsavedChanges()
            window.forceClose = true
            window.close()
        })
    }

    // Android's system Back. Order matters: a full-screen clip tool, then any open
    // sheet/dialog, and only then the home/exit step — otherwise Back walked out of the
    // editor from behind a modal that stayed on screen.
    // Application context, not the default window context: every tool window below calls
    // requestActivate() and so becomes the active window, which left a WindowShortcut on
    // this window dead exactly when the toolWindowOpen branch was needed.
    Shortcut {
        sequences: [StandardKey.Back, "Esc"]
        context: Qt.ApplicationShortcut
        onActivated: {
            if (window.subPage) {
                if (window.closeTopMenu() || window.closeTopModal())
                    return
                window.closeSubPage()
                return
            }
            if (window.toolWindowOpen) {
                window.closeTopToolWindow()
                return
            }
            if (window.closeTopMenu())
                return
            if (window.closeTopModal())
                return
            if (window.editorPage && window.editorPage.handleBack())
                return
            // A SAF copy is running and AssetLibrary has no cancel entry point, so
            // closing the window kills the worker part-way through a file. Swallow
            // Back — but say so, because a Back key that silently does nothing is
            // its own defect.
            if (AssetLibrary.importing) {
                Toasts.info(qsTr("Import in progress…"))
                return
            }
            window.goBack()
        }
    }

    // Modals hosted here. They close on Escape, which Android's Back key is not, so
    // without this Back left them on screen and walked out of the editor behind them.
    // Ordered by how they stack: newest-opened first.
    function closeTopModal() {
        // A project open has no cancel; Back must not reach whatever sits under the dialog.
        if (projectLoadDialog.visible)
            return true
        // Not in the list below: it is a sheet, so it goes out through dismiss() — close()
        // would take the panel off screen in the frame Back is pressed instead of sliding it.
        if (layoutSheet.opened) {
            layoutSheet.dismiss()
            return true
        }
        const modals = [shellSwitchDialogLoader.item, aboutDialogLoader.item, debugInfoDialogLoader.item, projectPropertiesDialog,
                        recoveryDialog, unsavedDialog, addonStartupDialog,
                        missingAddonsDialogLoader.item, updateDialogLoader.item,
                        reverseProgressDialogLoader.item, subtitleProgressDialogLoader.item]
        for (var i = 0; i < modals.length; ++i) {
            if (modals[i] && modals[i].visible) {
                // The recovery prompt is the one modal with no dismissal: close() skips
                // accepted()/rejected(), so neither Restore nor Discard runs, the snapshot
                // stays unanswered, and the next autosave overwrites it. Swallow Back and
                // leave it up — that is what its NoAutoClose policy already asks for.
                if (modals[i] === recoveryDialog)
                    return true
                // Same reasoning, different cause: these three cancel the job they
                // are reporting on when they close, and Back is far too easy to
                // hit for that. The dialog's own Cancel button stays the way out.
                if (modals[i] === reverseProgressDialogLoader.item
                        || modals[i] === subtitleProgressDialogLoader.item
                        || modals[i] === packageProgressDialog)
                    return true
                modals[i].close()
                return true
            }
        }
        return false
    }

    // Registration points for ThemedContextMenu / NewTrackMenu; see _openMenus above.
    function pushMenu(menu) {
        window._openMenus.push(menu)
    }

    function popMenu(menu) {
        const index = window._openMenus.indexOf(menu)
        if (index >= 0)
            window._openMenus.splice(index, 1)
    }

    function closeTopMenu() {
        while (window._openMenus.length > 0) {
            const menu = window._openMenus.pop()
            if (menu && menu.visible) {
                // A Dialog dismissed with close() emits neither accepted nor
                // rejected, so the onRejected cleanup its owner relies on never
                // runs. Menus have no reject(); dialogs do.
                if (typeof menu.reject === "function")
                    menu.reject()
                else
                    menu.close()
                return true
            }
        }
        return false
    }

    LazyLoader {
        id: languageChooserDialogLoader
        sourceComponent: Component {
            LanguageChooserDialog {
                onClosed: window.continueStartupAfterLanguage()
            }
        }
    }
    // The phone view over LayoutPresets. Hosted here rather than in the editor because
    // Settings → Video reaches it through ShellBridge.main.openLayoutChooser() too.
    // Every marketplace download, whichever surface started it, gets a durable copy.
    //
    // Without this a download lives only in AppDataLocation — wiped by an uninstall or a "clear
    // data", and reachable from no file manager or gallery. The user paid quota for it and could
    // lose it having never seen where it went. The copy is a second pass over the bytes, which is
    // why it runs off the GUI thread and why nothing waits on it: the import has already happened
    // and the clip is usable meanwhile.
    Connections {
        target: Market
        function onDownloadImported(itemId, name) {
            const job = Market.downloadInfo(itemId)
            const path = job && job.filePath ? String(job.filePath) : ""
            // A download the user steered to their own folder is already somewhere they chose.
            if (path === "" || (job.destinationDir && String(job.destinationDir).length > 0))
                return
            EditorState.saveToGallery(path, name)
        }
    }

    Connections {
        target: EditorState
        function onSavedToGallery(displayName, ok, location, error) {
            if (ok)
                Toasts.success(qsTr("Saved to %1").arg(location))
            else if (error.length > 0)
                Toasts.warning(error)
        }
    }

    // Warm start: a share that landed while Drift was already running.
    Connections {
        target: FileDialogs
        function onIncomingIntent(intent) {
            if (intent && intent.kind !== "view")
                window.routeIncomingIntent(intent)
        }
    }

    AndroidShareTargetSheet {
        id: shareTargetSheet
        onAddToProject: (urls) => MediaImport.importUrls(urls, false, function (added) {
            if (added > 0)
                EditorState.addClipFromAsset(AssetLibrary.count - added)
        })
        onStartQuickEdit: (urls) => window.beginQuickEdit(urls)
    }

    AndroidLayoutSheet {
        id: layoutSheet
        onApplied: {
            const next = window._afterLayoutChosen
            window._afterLayoutChosen = null
            if (next)
                next()
        }
        // Dismissed rather than committed. The project has already been reset by then, so this
        // leaves the user on Home with an empty project rather than pushing them into an editor
        // for a canvas they declined to choose.
        onClosed: window._afterLayoutChosen = null
    }

    // Read by the editor page: it binds the preview down by this so a sheet that keeps the
    // editor live letterboxes the frame instead of cutting it in half.
    readonly property real nonBlockingSheetHeight:
        (layoutSheet.opened && !layoutSheet.blocking) ? layoutSheet.panelHeight : 0

    // Desktop reaches this from EditorHeader, which Android replaces with AndroidTopBar;
    // hosting it here is what gives that bar's overflow something to open.
    ProjectPropertiesDialog { id: projectPropertiesDialog }

    Connections {
        target: EditorState.projectFile
        function onProjectLoadFinished() { window.openPendingLaunch() }

        // An agent's close_project (true) / new_project or load_project (false): Android's start screen is Home.
        // The agent already discarded the document, so no unsaved prompt. Unwinds the whole
        // stack rather than showHome()'s single pop — a media preview pushed over the editor
        // would otherwise be all that came off.
        function onStartScreenRequested(visible) {
            if (!visible) {
                window.showEditor()
                return
            }
            window.mediaPreviewPage = null
            if (stack.depth > 1)
                stack.pop(null)
            window.inEditor = false
            EditorState.setLastMessage(qsTr("Project closed"))
        }
    }

    function openProjectProperties() {
        projectPropertiesDialog.openDialog()
    }

    ProjectLoadDialog { id: projectLoadDialog }

    RecoveryDialog {
        id: recoveryDialog
        // Restore leaves the recovered timeline unsaved, so a parked launch goes through
        // confirmIfDirty and asks before replacing it; New session has already cleared the
        // snapshot, so it opens straight away.
        //
        // The editor is shown either way. Cancelling that prompt leaves the recovered work as the
        // live project, and staying on the home screen would hide it behind a Recent Projects list
        // that does not list it. openPendingLaunch queues its own showEditor, which no-ops here.
        onAccepted: {
            Qt.callLater(window.showEditor)
            window.openPendingLaunch()
        }
        onRejected: window.openPendingLaunch()
    }

    function openPendingLaunch() {
        if (window._pendingLaunchUrl === "")
            return false
        // A recovery restore is still landing; onProjectLoadFinished comes back here so the
        // unsaved prompt sees the recovered work.
        if (EditorState.projectFile.projectLoadPending)
            return true
        const url = window._pendingLaunchUrl
        window._pendingLaunchUrl = ""
        window.confirmIfDirty(function () {
            EditorState.projectFile.loadProject(url)
            Qt.callLater(window.showEditor)
        })
        return true
    }

    UnsavedChangesDialog {
        id: unsavedDialog
        onSaveChosen: {
            // False also means "still running" now: an embedded-media save finishes on a
            // worker, and onProjectSaved below runs the queued action when it lands. Either
            // way the dialog stays up and the action stays queued.
            if (!window.saveProject())
                return
            const action = window._pendingAfterUnsaved
            window._pendingAfterUnsaved = null
            close()
            if (action)
                action()
        }
        onDiscardChosen: {
            const action = window._pendingAfterUnsaved
            window._pendingAfterUnsaved = null
            close()
            EditorState.projectFile.discardUnsavedChanges()
            if (action)
                action()
        }
        onRejected: window._pendingAfterUnsaved = null
    }

    function importUserPackage(url) {
        window.openAddonManager().importUserPackage(url)
    }
    AddonStartupDialog { id: addonStartupDialog }
    LazyLoader { id: missingAddonsDialogLoader; sourceComponent: Component { MissingAddonsDialog { } } }
    LazyLoader { id: accelerationFailedDialogLoader; sourceComponent: Component { AccelerationFailedDialog { } } }
    LazyLoader { id: updateDialogLoader; sourceComponent: Component { UpdateDialog { } } }
    // Kept after its first close: it holds the last benchmark result.
    LazyLoader { id: debugInfoDialogLoader; keepLoaded: true; sourceComponent: Component { DebugInfoDialog { } } }

    LazyLoader { id: aboutDialogLoader; sourceComponent: Component { AboutDialog { } } }
    LazyLoader { id: shellSwitchDialogLoader; sourceComponent: Component { ShellSwitchDialog { } } }
    LazyLoader { id: subtitleProgressDialogLoader; sourceComponent: Component { SubtitleProgressDialog { } } }
    LazyLoader { id: reverseProgressDialogLoader; sourceComponent: Component { ReverseProgressDialog { } } }

    // Long-running clip tools. ToolWindow hosts each as a full-screen popup in this window,
    // so they read as pages that the system Back key dismisses (see the Back shortcut above).
    LazyLoader { id: segmentationWindowLoader; sourceComponent: Component { SegmentationWindow { } } }
    LazyLoader { id: denoiseWindowLoader; sourceComponent: Component { DenoiseWindow { } } }
    LazyLoader { id: speedCurveWindowLoader; sourceComponent: Component { SpeedCurveWindow { } } }
    LazyLoader { id: fadeCurveWindowLoader; sourceComponent: Component { FadeCurveWindow { } } }
    LazyLoader { id: multicamWindowLoader; sourceComponent: Component { MulticamWindow { } } }

    // Every inspector reaches these through ShellBridge.main.<name>() — the same contract
    // Main.qml offers on desktop. A missing one is a runtime TypeError, not a dead button.
    function openSegmentation(track, clip, startSeconds, durationSeconds) {
        segmentationWindowLoader.ensure().openFor(track, clip, startSeconds, durationSeconds)
    }

    function openDenoise(track, clip, durationSeconds) {
        denoiseWindowLoader.ensure().openFor(track, clip, durationSeconds)
    }

    function openSpeedCurve(track, clip) {
        speedCurveWindowLoader.ensure().openFor(track, clip)
    }

    function openFadeCurve(track, clip) {
        fadeCurveWindowLoader.ensure().openFor(track, clip)
    }

    function openTransitionCurve(track, transitionId) {
        fadeCurveWindowLoader.ensure().openForTransition(track, transitionId)
    }

    // Preview-and-edit is its own screen, not one of the Windows above: a secondary Window gets
    // no safe-area insets here (its header lands under the status bar) and a VideoOutput inside
    // one paints black. Pushed onto the stack rather than layered over it, because an item over
    // the stack still lost its top band to the editor's top bar and its bottom to the rail.
    function openMediaPreview(assetIndex) {
        if (window.mediaPreviewPage)
            return
        if (window.editorPage)
            window.editorPage.closeSheets()
        const page = stack.push(mediaPreviewComponent)
        window.mediaPreviewPage = page
        page.openFor(assetIndex)
    }

    function closeMediaPreview() {
        if (!window.mediaPreviewPage)
            return
        window.mediaPreviewPage.close()
    }

    function openMulticam() {
        multicamWindowLoader.ensure().openSession()
    }

    // Opened from the overflow menu, which used to switch the asset sheet to a
    // settings tab.
    function openSettings() {
        if (window.subPageKind === "settings")
            return
        window.closeSubPage()
        window.subPage = stack.push(settingsPageComponent)
        window.subPageKind = "settings"
    }

    // Settings and Extras are pages on the phone, not the dialogs the desktop shell opens.
    function closeSubPage() {
        const page = window.subPage
        if (!page)
            return
        window.subPage = null
        window.subPageKind = ""
        if (stack.currentItem === page)
            stack.pop()
    }

    function switchShell(target) {
        shellSwitchDialogLoader.ensure().openFor(target, false)
    }

    // A mouse on a tablet or in DeX means the touch layout is probably the wrong one, so offer the
    // desktop shell: at launch, or whenever one is plugged in, at most once per session. Held back
    // while a startup prompt is up so the two do not stack.
    property bool _mouseOfferMade: false
    Timer {
        interval: 1200
        repeat: true
        running: Qt.platform.os === "android" && PointerDevices.mousePresent && !window._mouseOfferMade
        onTriggered: {
            if (recoveryDialog.visible
                    || (languageChooserDialogLoader.item && languageChooserDialogLoader.item.visible))
                return
            window._mouseOfferMade = true
            shellSwitchDialogLoader.ensure().openFor("desktop", true)
        }
    }

    // Shell.qml only builds a shell at startup, so applying a layout change is a relaunch. On
    // Android the recovery snapshot carries unsaved work across; on a desktop running this shell
    // the usual prompt runs first.
    function restartToApplyShell() {
        if (Qt.platform.os === "android") {
            EditorState.projectFile.flushRecoverySnapshot()
            EditorState.preferences.restartApp()
            return
        }
        window.confirmIfDirty(function () {
            EditorState.projectFile.discardUnsavedChanges()
            EditorState.preferences.restartApp()
            window.forceClose = true
            window.close()
        })
    }

    function openAddonManager(kind) {
        if (window.subPageKind !== "extras") {
            window.closeSubPage()
            window.subPage = stack.push(extrasPageComponent)
            window.subPageKind = "extras"
        }
        if (kind !== undefined)
            window.subPage.panel.openForKind(kind)
        return window.subPage.panel
    }

    function openExtras() {
        if (addonStartupDialog.openForAttention())
            return
        openAddonManager()
    }

    function openUpdateDialog() {
        updateDialogLoader.ensure().open()
    }

    function openDebugInfo() {
        debugInfoDialogLoader.ensure().open()
    }

    function openAboutDialog() {
        aboutDialogLoader.ensure().open()
    }

    readonly property alias addonAttentionNeeded: addonStartupDialog.needsAttention

    function refreshAddonAttention() {
        addonStartupDialog.refreshAttention()
    }

    // True while any of the full-screen clip tools owns the display, so the editor's
    // Back handling defers to them instead of popping the stack behind them.
    readonly property bool toolWindowOpen: segmentationWindowLoader.shown || denoiseWindowLoader.shown
                                           || speedCurveWindowLoader.shown || fadeCurveWindowLoader.shown
                                           || multicamWindowLoader.shown
                                           || window.mediaPreviewPage !== null

    // A full-page tool (AndroidToolPage) is open in the editor; the editor's handleBack cancels it.
    readonly property bool toolPageOpen: window.editorPage !== null && window.editorPage.toolPageOpen

    function closeTopToolWindow() {
        if (segmentationWindowLoader.shown)
            segmentationWindowLoader.item.close()
        else if (denoiseWindowLoader.shown)
            denoiseWindowLoader.item.close()
        else if (speedCurveWindowLoader.shown)
            speedCurveWindowLoader.item.close()
        else if (fadeCurveWindowLoader.shown)
            fadeCurveWindowLoader.item.close()
        else if (multicamWindowLoader.shown)
            multicamWindowLoader.item.close()
        else if (window.mediaPreviewPage)
            window.mediaPreviewPage.close()
    }

    Timer {
        id: recoveryOpenTimer
        interval: 150
        repeat: true
        property int attempts: 0
        onTriggered: {
            // Opting in to reopening the last project already restores the autosave, so
            // asking about it as well is a question the user has answered once already.
            if (EditorState.preferences.needsUiLanguagePrompt || languageChooserDialogLoader.shown)
                return
            if (!EditorState.projectFile.recoveryAvailable || EditorState.preferences.reopenLastProject) {
                stop()
                attempts = 0
                return
            }
            if (!recoveryDialog.visible)
                recoveryDialog.open()
            if (recoveryDialog.visible || ++attempts >= 20)
                stop()
        }
    }

    Component.onDestruction: if (ShellBridge.main === window) ShellBridge.main = null

    Component.onCompleted: {
        WindowBackdrop.attach(window)
        ShellBridge.main = window
        Theme.windowWidth = window.width
        // Independent of the language/recovery/launch-intent branching below — it
        // neither gates nor is gated by any of it.
        EditorState.mcp.applyStartOnLaunch()
        if (EditorState.preferences.needsUiLanguagePrompt) {
            languageChooserDialogLoader.ensure().openChooser()
            return
        }
        window.continueStartupAfterLanguage()
    }

    function continueStartupAfterLanguage() {
        // Tapping a .drift in a file manager launches us with ACTION_VIEW. That project is
        // what the user asked for, so it outranks the reopen-last-project restore below.
        // Empty on desktop, where the intent does not exist.
        // String(): takeLaunchUrl returns a QUrl, which reaches QML as a `url` value, not a
        // string — so `launched !== ""` was true even for an empty one. Every cold start then
        // tried to load an empty URL, which failed with "That project location isn't valid" and
        // left the user in an empty editor instead of on the home screen.
        const intent = FileDialogs.takeLaunchIntent()
        if (intent && intent.kind && intent.kind !== "view") {
            window.routeIncomingIntent(intent)
            return
        }
        let launched = intent && intent.urls && intent.urls.length > 0
                       ? String(intent.urls[0]) : ""
        // A .driftfx opened from a file manager installs over whatever startup does next.
        if (launched !== "" && Addons.isUserPackage(launched)) {
            Qt.callLater(window.importUserPackage, launched)
            launched = ""
        }
        if (launched !== "" && !Market.handleIncomingUrl(launched)) {
            // Unless the previous session left a snapshot: loading the launched project
            // deletes it unasked, so park the URL and let the recovery prompt run first.
            if (EditorState.projectFile.recoveryAvailable && !EditorState.preferences.reopenLastProject) {
                window._pendingLaunchUrl = launched
                recoveryOpenTimer.start()
                return
            }
            window.confirmIfDirty(function () {
                EditorState.projectFile.loadProject(launched)
                Qt.callLater(window.showEditor)
            })
            return
        }
        // "Reopen last project" restores the autosave or the last clean .drift silently and
        // lands straight in the editor; the home page would be a step backwards from it.
        if (EditorState.projectFile.restoreLastSessionIfEnabled()) {
            Qt.callLater(window.showEditor)
            return
        }
        if (EditorState.projectFile.recoveryAvailable)
            recoveryOpenTimer.start()
        else
            Qt.callLater(window.refreshAddonAttention)
    }

    Connections {
        target: EditorState.segmentation
        function onOpenWindowRequested(track, clip, startSeconds, durationSeconds) {
            segmentationWindowLoader.ensure().openFor(track, clip, startSeconds, durationSeconds, true)
        }
    }

    Connections {
        target: EditorState

        function onOpenMulticamWindowRequested() {
            multicamWindowLoader.ensure().openSession()
        }
        // These two dialogs open themselves from the same signals, but only once they exist.
        function onSubtitleGeneratingChanged() {
            if (EditorState.subtitleGenerating)
                subtitleProgressDialogLoader.ensure().open()
        }
        function onReverseConfirmRequested(trackIndex, clipIndex, seconds) {
            const dialog = reverseProgressDialogLoader.ensure()
            dialog.pendingTrack = trackIndex
            dialog.pendingClip = clipIndex
            dialog.pendingSeconds = seconds
            dialog.open()
        }
        // Failures and cancellation surface through lastMessage, which carries the
        // actual reason; a generic toast here used to blame disk space for everything.
        function onExportFinished(success) {
            if (success)
                Toasts.success(qsTr("Export finished."))
        }
        function onSubtitleGenerationFinished(ok, message) {
            if (ok)
                Toasts.success(message.length > 0 ? message : qsTr("Captions created."))
            else
                Toasts.error(message.length > 0
                             ? qsTr("Couldn’t create captions: %1").arg(message)
                             : qsTr("Couldn’t create captions."))
        }
        // Severity comes from the backend. Inferring it by regexing the prose matched
        // none of the real failure strings, so a corrupt-project open read as a neutral
        // info toast that auto-dismissed like "Project saved".
        function onLastMessageChanged() {
            const message = EditorState.lastMessage
            if (message.length === 0)
                return
            switch (EditorState.lastMessageSeverity) {
            case "error":   Toasts.error(message); break
            case "warning": Toasts.warning(message); break
            case "success": Toasts.success(message); break
            default:        Toasts.info(message); break
            }
        }
        function onTransformBlocked(reason) {
            Toasts.warning(reason)
        }
    }

    Connections {
        target: EditorState.projectFile
        function onRecoveryChanged() {
            if (EditorState.preferences.needsUiLanguagePrompt || languageChooserDialogLoader.shown)
                return
            if (EditorState.preferences.reopenLastProject)
                return
            if (EditorState.projectFile.recoveryAvailable)
                recoveryOpenTimer.start()
        }
        function onMissingAddons(addons) {
            missingAddonsDialogLoader.ensure().openFor(addons)
        }
        // Saving a project whose media is embedded runs on a worker, so saveProject() returns
        // before the result exists and the action queued behind the unsaved-changes dialog
        // cannot ride on its return value. The synchronous path emits this too, while
        // _pendingAfterUnsaved is still set, so the action still runs exactly once.
        function onProjectSaved(ok) {
            // unsavedDialog.visible is the guard that keeps this tied to a save the dialog asked
            // for. Back dismisses that dialog with close(), which emits neither accepted nor
            // rejected, so the parked action survives — and without this an ordinary Save minutes
            // later would fire it and navigate away from under the user.
            if (!ok || !unsavedDialog.visible || !window._pendingAfterUnsaved)
                return
            const action = window._pendingAfterUnsaved
            window._pendingAfterUnsaved = null
            unsavedDialog.close()
            action()
        }
        function onPackageFinished(ok, message) {
            if (ok)
                Toasts.success(message)
            else
                Toasts.error(qsTr("Couldn't create the shareable copy: %1").arg(message))
        }
    }

    // Tapping a .drift while we are already running arrives through onNewIntent, which
    // Component.onCompleted above is long past. Same handling as the cold start.
    Connections {
        target: FileDialogs
        function onLaunchUrlReceived(url) {
            if (Market.handleIncomingUrl(url))
                return
            if (Addons.isUserPackage(url)) {
                window.importUserPackage(url)
                return
            }
            if (recoveryDialog.visible) {
                window._pendingLaunchUrl = url
                return
            }
            window.confirmIfDirty(function () {
                EditorState.projectFile.loadProject(url)
                Qt.callLater(window.showEditor)
            })
        }
    }

    AndroidLifecycle { denoiseWindow: denoiseWindowLoader.item }

    Connections {
        target: Addons
        function onRefreshingChanged() {
            if (!Addons.refreshing)
                Qt.callLater(window.refreshAddonAttention)
        }
        function onCatalogChanged() { Qt.callLater(window.refreshAddonAttention) }
        function onRemindEssentialChanged() { Qt.callLater(window.refreshAddonAttention) }
        function onRemindUpdatesChanged() { Qt.callLater(window.refreshAddonAttention) }
        function onAccelerationFailed(variant, error, waiting) {
            accelerationFailedDialogLoader.ensure().openFor(variant, error, waiting)
        }

        // A download or signature failure only reaches the addon manager's status line,
        // so a pack that failed while that dialog was closed failed silently.
        function onTransferFailed(id, reason) {
            if (reason === "Cancelled")
                return
            Toasts.error(qsTr("Couldn’t install “%1”: %2").arg(id).arg(reason))
        }
    }

    Connections {
        target: Market
        function onAuthFinished(ok, message) {
            if (ok)
                Toasts.success(message)
            else
                Toasts.error(message)
        }
        function onDownloadImported(itemId, name) {
            Toasts.success(name.length > 0
                           ? qsTr("Imported “%1”.").arg(name)
                           : qsTr("Imported from the marketplace."))
        }
        function onDownloadFailed(itemId, code, message) {
            Toasts.error(message)
        }
    }

    StackView {
        id: stack
        anchors.fill: parent
        initialItem: homeComponent

        // Forward slides in from the right and pushes the old page a third of the
        // way off; Back reverses it, faster. The Basic style's default is a 400ms
        // OutCubic with the same duration both ways, which is off every motion
        // token in Theme and reads as drift rather than navigation.
        pushEnter: Transition {
            NumberAnimation { property: "x"; from: stack.width; to: 0
                              duration: Theme.durationSlow; easing.type: Theme.easing }
        }
        pushExit: Transition {
            NumberAnimation { property: "x"; from: 0; to: -stack.width * 0.3
                              duration: Theme.durationSlow; easing.type: Theme.easing }
        }
        popEnter: Transition {
            NumberAnimation { property: "x"; from: -stack.width * 0.3; to: 0
                              duration: Theme.durationBase; easing.type: Theme.easing }
        }
        popExit: Transition {
            NumberAnimation { property: "x"; from: 0; to: stack.width
                              duration: Theme.durationBase; easing.type: Theme.easing }
        }
        replaceEnter: pushEnter
        replaceExit: pushExit
    }

    // Under the Overlay, over the page. Overlay popups stay interactive; the
    // editor cannot. Hidden during TouchDrag so a lift-from-sheet can still land.
    MouseArea {
        id: overlayInputGuard
        anchors.fill: parent
        visible: window.overlayModalCount > 0 && !TouchDrag.active
        acceptedButtons: Qt.AllButtons
        hoverEnabled: true
        preventStealing: true
        onPressed: (mouse) => { mouse.accepted = true }
        onWheel: (wheel) => { wheel.accepted = true }

        readonly property int _stealHandlers: PointerHandler.CanTakeOverFromHandlersOfSameType
                                            | PointerHandler.CanTakeOverFromHandlersOfDifferentType

        TapHandler {
            acceptedButtons: Qt.AllButtons
            grabPermissions: overlayInputGuard._stealHandlers
        }

        PinchHandler {
            target: null
            grabPermissions: overlayInputGuard._stealHandlers
        }

        DragHandler {
            target: null
            grabPermissions: overlayInputGuard._stealHandlers
        }
    }

    Component {
        id: homeComponent
        AndroidHome {
            // Also flushes a link shared into a cold start: openLinkImport() can run from the
            // window's onCompleted, before this page exists to start an editor from.
            Component.onCompleted: {
                window.homePage = this
                window.deliverPendingLink()
            }
            Component.onDestruction: if (window.homePage === this) window.homePage = null
            onEnterEditor: window.showEditor()
            onNewProjectRequested: window.startNewProject()
            onQuickActionRequested: (id) => {
                if (id === "quick")
                    window.beginQuickEdit(null)
                else
                    window.runQuickAction(id)
            }
            onOpenProjectRequested: window.openProjectFile()
            onOpenRecentRequested: (path) => window.openRecent(path)
        }
    }

    Component {
        id: settingsPageComponent
        AndroidSubPage {
            title: qsTr("Settings")
            onBackRequested: window.closeSubPage()
            Component.onDestruction: if (window.subPage === this) { window.subPage = null; window.subPageKind = "" }

            SettingsPane { anchors.fill: parent; mobile: true }
        }
    }

    Component {
        id: extrasPageComponent
        AndroidSubPage {
            title: qsTr("Extras")
            property alias panel: extrasPanel
            onBackRequested: window.closeSubPage()
            Component.onDestruction: if (window.subPage === this) { window.subPage = null; window.subPageKind = "" }

            AddonManagerPanel {
                id: extrasPanel
                anchors.fill: parent
                anchors.margins: Theme.androidPagePadding
                Component.onCompleted: refresh()
            }
        }
    }

    Component {
        id: mediaPreviewComponent
        AndroidMediaPreview {
            id: mediaPreviewItem
            onClosed: {
                window.mediaPreviewPage = null
                if (stack.currentItem === mediaPreviewItem)
                    stack.pop()
            }
        }
    }

    Component {
        id: editorComponent
        AndroidEditor {
            Component.onCompleted: {
                window.editorPage = this
                window.deliverPendingLink()
            }
            Component.onDestruction: if (window.editorPage === this) window.editorPage = null
            onBackRequested: window.goBack()
        }
    }

    // Below the top bar and centered: the bottom is the timeline and the rail, where the
    // thumb is.
    ToastHost {
        parent: Overlay.overlay
        alignRight: false
        topMargin: SafeArea.margins.top + Theme.androidTopBarHeight
    }
}
