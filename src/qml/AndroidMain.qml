import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Window
import Drift
import "components"

// Android entry shell: home (layout + recent + import) → CapCut editor.
ApplicationWindow {
    id: window
    visible: true
    color: Theme.appBackground
    title: "CutWire Drift"

    // Theme is a singleton and cannot see a window, so the size class it reports has to be fed
    // from whichever root is live. Screen is the wrong source: it ignores tiling and split view.
    onWidthChanged: Theme.windowWidth = width

    // Qt creates some chrome itself — most visibly the Undo/Cut/Copy/Paste menu on every
    // TextField and TextArea (Basic/TextField.qml declares ContextMenu.menu). Drift styles
    // none of that, so it fell through to the palette the platform theme supplies and picked
    // up the desktop's colour scheme: on a KDE session with a custom scheme the editing menu
    // rendered in that scheme's colours next to Drift's own. Palette propagates down the item
    // hierarchy, popups included, so setting the roles the Basic style reads brings Qt's own
    // chrome under Theme — and binding them keeps it following the light/dark toggle.
    palette.window: Theme.panelBackground
    palette.windowText: Theme.panelForeground
    palette.base: Theme.panelAccent
    palette.text: Theme.panelForeground
    palette.button: Theme.panelAccent
    palette.buttonText: Theme.panelForeground
    palette.placeholderText: Theme.mutedForeground
    // Menu border and item states: dark draws the frame, light the hovered row,
    // midlight the pressed one.
    palette.dark: Theme.panelBorder
    palette.mid: Theme.panelMuted
    palette.midlight: Theme.panelAccent
    palette.light: Theme.popoverHover
    // Drawn at 12% and 50% alpha for the menu's drop shadow; appBackground would
    // make that a white glow in light mode.
    palette.shadow: "#000000"
    palette.highlight: Theme.primary
    palette.highlightedText: Theme.primaryForeground
    palette.toolTipBase: Theme.panelBackground
    palette.toolTipText: Theme.panelForeground

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

    function confirmIfDirty(action) {
        if (!EditorState.hasUnsavedChanges) {
            action()
            return
        }
        window._pendingAfterUnsaved = action
        unsavedDialog.openDialog()
    }

    function saveProject() {
        if (EditorState.currentProjectPath && EditorState.currentProjectPath.length > 0) {
            EditorState.saveProject(EditorState.fileUrl(EditorState.currentProjectPath))
            return !EditorState.hasUnsavedChanges
        }
        const url = FileDialogs.saveFile(qsTr("Save Project"), window.projectFilter,
                                         EditorState.projectName, "drift")
        if (url === "")
            return false
        EditorState.saveProject(url)
        return !EditorState.hasUnsavedChanges
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
        EditorState.setProjectSetup(setup.width, setup.height, setup.fps)
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
            EditorState.newProject()
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
    function beginQuickEdit(urls) {
        window.confirmIfDirty(function() {
            EditorState.newProject()
            const landed = function(added) {
                if (added > 0) {
                    const index = AssetLibrary.count - added
                    window.applyInferredSetup(index)
                    EditorState.addClipFromAsset(index)
                }
                window.showEditor()
            }
            if (urls && urls.length > 0)
                MediaImport.importUrls(urls, false, landed)
            else
                MediaImport.pickAndImport(landed)
        })
    }

    function openLayoutChooser() {
        layoutSheet.openSheet()
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
        if (EditorState.hasUnsavedChanges) {
            confirmIfDirty(function () {
                EditorState.discardUnsavedChanges()
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
            window.confirmIfDirty(function () {
                EditorState.loadProject(url)
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
    // Handled by the Market destination rather than by a sheet over whatever is on screen: it
    // ends in a marketplace download and it asks the user to pick a catalog provider, so the
    // store is where it belongs. Parked when Home is not built yet — a share can cold-start the
    // app, and this runs from the window's own onCompleted.
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
        if (window.inEditor)
            window.showHome()
        if (!window.homePage)
            return
        const url = window.pendingLinkUrl
        window.pendingLinkUrl = ""
        window.homePage.startLinkImport(url)
    }

    function openProjectFile() {
        confirmIfDirty(function () {
            const url = FileDialogs.openFile(qsTr("Open Project"), window.projectFilter)
            if (url !== "") {
                EditorState.loadProject(url)
                showEditor()
            }
        })
    }

    function openRecent(path) {
        confirmIfDirty(function () {
            EditorState.openRecentProject(path)
            showEditor()
        })
    }

    onClosing: function (close) {
        // Opting in to reopening the last project means the autosave is restored on the
        // next launch, so asking to save on the way out is a question already answered.
        if (window.forceClose || !EditorState.hasUnsavedChanges || EditorState.reopenLastProject)
            return
        close.accepted = false
        confirmIfDirty(function () {
            EditorState.discardUnsavedChanges()
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
            // Home owns a nav stack of its own now: a secondary destination, and any drill-down
            // inside it, unwinds before Back means "leave".
            if (!window.inEditor && window.homePage && window.homePage.handleBack())
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
        // Not in the list below: it is a sheet, so it goes out through dismiss() — close()
        // would take the panel off screen in the frame Back is pressed instead of sliding it.
        if (layoutSheet.opened) {
            layoutSheet.dismiss()
            return true
        }
        // debugInfoDialog before settingsDialog: it opens from inside Settings, so it is the
        // one on top whenever both are up.
        const modals = [debugInfoDialog, settingsDialog, projectPropertiesDialog,
                        recoveryDialog, unsavedDialog, addonStartupDialog, addonManagerDialog,
                        missingAddonsDialog, updateDialog, reverseProgressDialog,
                        subtitleProgressDialog]
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
                if (modals[i] === reverseProgressDialog
                        || modals[i] === subtitleProgressDialog
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

    LanguageChooserDialog {
        id: languageChooserDialog
        onClosed: window.continueStartupAfterLanguage()
    }
    // The phone view over LayoutPresets. Hosted here rather than in the editor because
    // Settings → Video reaches it through Window.window.openLayoutChooser() too.
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

    function openProjectProperties() {
        projectPropertiesDialog.openDialog()
    }

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
        const url = window._pendingLaunchUrl
        window._pendingLaunchUrl = ""
        window.confirmIfDirty(function () {
            EditorState.loadProject(url)
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
            EditorState.discardUnsavedChanges()
            if (action)
                action()
        }
        onRejected: window._pendingAfterUnsaved = null
    }

    AddonManagerDialog { id: addonManagerDialog }
    AddonStartupDialog { id: addonStartupDialog }
    MissingAddonsDialog { id: missingAddonsDialog }
    UpdateDialog { id: updateDialog }
    DebugInfoDialog { id: debugInfoDialog }

    SettingsDialog { id: settingsDialog }
    SubtitleProgressDialog { id: subtitleProgressDialog }
    ReverseProgressDialog { id: reverseProgressDialog }

    // Long-running clip tools. These are top-level Windows on desktop; on Android the
    // platform gives each one the whole screen, so they read as full-screen pages that
    // the system Back key dismisses (see the Back shortcut above).
    SegmentationWindow { id: segmentationWindow }
    DenoiseWindow { id: denoiseWindow }
    SpeedCurveWindow { id: speedCurveWindow }
    FadeCurveWindow { id: fadeCurveWindow }
    MulticamWindow { id: multicamWindow }

    // Every inspector reaches these through Window.window.<name>() — the same contract
    // Main.qml offers on desktop. A missing one is a runtime TypeError, not a dead button.
    function openSegmentation(track, clip, startSeconds, durationSeconds) {
        segmentationWindow.openFor(track, clip, startSeconds, durationSeconds)
    }

    function openDenoise(track, clip, durationSeconds) {
        denoiseWindow.openFor(track, clip, durationSeconds)
    }

    function openSpeedCurve(track, clip) {
        speedCurveWindow.openFor(track, clip)
    }

    function openFadeCurve(track, clip) {
        fadeCurveWindow.openFor(track, clip)
    }

    function openTransitionCurve(track, transitionId) {
        fadeCurveWindow.openForTransition(track, transitionId)
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
        multicamWindow.openSession()
    }

    // Opened from the overflow menu, which used to switch the asset sheet to a
    // settings tab.
    function openSettings() {
        settingsDialog.open()
    }

    function openAddonManager(kind) {
        if (kind === undefined)
            addonManagerDialog.open()
        else
            addonManagerDialog.openForKind(kind)
    }

    function openExtras() {
        if (addonStartupDialog.openForAttention())
            return
        openAddonManager()
    }

    function openUpdateDialog() {
        updateDialog.open()
    }

    function openDebugInfo() {
        debugInfoDialog.open()
    }

    readonly property alias addonAttentionNeeded: addonStartupDialog.needsAttention

    function refreshAddonAttention() {
        addonStartupDialog.refreshAttention()
    }

    // True while any of the full-screen clip tools owns the display, so the editor's
    // Back handling defers to them instead of popping the stack behind them.
    readonly property bool toolWindowOpen: segmentationWindow.visible || denoiseWindow.visible
                                           || speedCurveWindow.visible || fadeCurveWindow.visible
                                           || multicamWindow.visible
                                           || window.mediaPreviewPage !== null

    function closeTopToolWindow() {
        if (segmentationWindow.visible)
            segmentationWindow.close()
        else if (denoiseWindow.visible)
            denoiseWindow.close()
        else if (speedCurveWindow.visible)
            speedCurveWindow.close()
        else if (fadeCurveWindow.visible)
            fadeCurveWindow.close()
        else if (multicamWindow.visible)
            multicamWindow.close()
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
            if (EditorState.needsUiLanguagePrompt || languageChooserDialog.visible)
                return
            if (!EditorState.recoveryAvailable || EditorState.reopenLastProject) {
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

    Component.onCompleted: {
        Theme.windowWidth = window.width
        if (EditorState.needsUiLanguagePrompt) {
            languageChooserDialog.openChooser()
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
        const launched = intent && intent.urls && intent.urls.length > 0
                         ? String(intent.urls[0]) : ""
        if (launched !== "" && !Market.handleIncomingUrl(launched)) {
            // Unless the previous session left a snapshot: loading the launched project
            // deletes it unasked, so park the URL and let the recovery prompt run first.
            if (EditorState.recoveryAvailable && !EditorState.reopenLastProject) {
                window._pendingLaunchUrl = launched
                recoveryOpenTimer.start()
                return
            }
            window.confirmIfDirty(function () {
                EditorState.loadProject(launched)
                Qt.callLater(window.showEditor)
            })
            return
        }
        // "Reopen last project" restores the autosave or the last clean .drift silently and
        // lands straight in the editor; the home page would be a step backwards from it.
        if (EditorState.restoreLastSessionIfEnabled()) {
            Qt.callLater(window.showEditor)
            return
        }
        if (EditorState.recoveryAvailable)
            recoveryOpenTimer.start()
        else
            Qt.callLater(window.refreshAddonAttention)
    }

    Connections {
        target: EditorState
        function onRecoveryChanged() {
            if (EditorState.needsUiLanguagePrompt || languageChooserDialog.visible)
                return
            if (EditorState.reopenLastProject)
                return
            if (EditorState.recoveryAvailable)
                recoveryOpenTimer.start()
        }
        function onOpenSegmentationWindowRequested(track, clip, startSeconds, durationSeconds) {
            segmentationWindow.openFor(track, clip, startSeconds, durationSeconds, true)
        }
        function onOpenMulticamWindowRequested() {
            multicamWindow.openSession()
        }
        function onMissingAddons(addons) {
            missingAddonsDialog.openFor(addons)
        }
        function onExportFinished(success) {
            if (success) {
                Toasts.success(qsTr("Export finished."))
                return
            }
            if (EditorState.lastMessage === "Export cancelled")
                Toasts.info(qsTr("Export cancelled."))
            else
                Toasts.error(qsTr("Export failed. Check the save location and free space."))
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

    // Tapping a .drift while we are already running arrives through onNewIntent, which
    // Component.onCompleted above is long past. Same handling as the cold start.
    Connections {
        target: FileDialogs
        function onLaunchUrlReceived(url) {
            if (Market.handleIncomingUrl(url))
                return
            if (recoveryDialog.visible) {
                window._pendingLaunchUrl = url
                return
            }
            window.confirmIfDirty(function () {
                EditorState.loadProject(url)
                Qt.callLater(window.showEditor)
            })
        }
    }

    // Backgrounding the app leaves the audio sink and the composite timer running, which
    // keeps decoding video nobody can see and holds the audio focus. Android will kill the
    // process for it eventually; pausing is the honest response to losing the foreground.
    Connections {
        target: Qt.application
        function onStateChanged() {
            if (Qt.application.state === Qt.ApplicationActive)
                return
            if (EditorState.playing)
                EditorState.playback.pause()
            // The two audition players are separate transports with their own audio sinks and
            // decode timers; pausing only the main one left them playing to nobody.
            EditorState.pauseSpeedCurvePreview()
            denoiseWindow.stopPlayback()
            // Losing the foreground is the last moment guaranteed to run: the OS can reclaim the
            // process from here without another callback, and aboutToQuit does not fire when it does.
            EditorState.flushRecoverySnapshot()
            // Decoder buffers, GL targets and image caches are all rebuildable; holding them
            // while backgrounded is what gets the process killed instead of resumed. Only for a
            // real backgrounding though: Android reports Inactive for every SAF picker, share
            // sheet and permission dialog, and tearing all of that down on a tap of Import would
            // cost a full rebuild on the way back. A job still running needs its decoders.
            if (Qt.application.state === Qt.ApplicationSuspended
                    || Qt.application.state === Qt.ApplicationHidden) {
                if (!EditorState.exportInProgress && !EditorState.packaging)
                    EditorState.releaseTransientCaches()
            }
        }
    }

    Connections {
        target: Addons
        function onRefreshingChanged() {
            if (!Addons.refreshing)
                Qt.callLater(window.refreshAddonAttention)
        }
        function onCatalogChanged() { Qt.callLater(window.refreshAddonAttention) }
        function onRemindEssentialChanged() { Qt.callLater(window.refreshAddonAttention) }
        function onRemindUpdatesChanged() { Qt.callLater(window.refreshAddonAttention) }

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
            // window's onCompleted, before this page exists to route it to.
            Component.onCompleted: {
                window.homePage = this
                window.deliverPendingLink()
            }
            Component.onDestruction: if (window.homePage === this) window.homePage = null
            onEnterEditor: window.showEditor()
            onNewProjectRequested: window.startNewProject()
            onQuickEditRequested: window.beginQuickEdit(null)
            onOpenProjectRequested: window.openProjectFile()
            onOpenRecentRequested: (path) => window.openRecent(path)
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
            Component.onCompleted: window.editorPage = this
            Component.onDestruction: if (window.editorPage === this) window.editorPage = null
            onBackRequested: window.goBack()
        }
    }

    ToastHost { parent: Overlay.overlay }
    TransitionSelectionBanner { parent: Overlay.overlay }
}
