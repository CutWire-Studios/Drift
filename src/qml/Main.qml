import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Window
import Drift

ApplicationWindow {
    id: window
    property alias editorHeader: workspace.editorHeader
    // Null until a layout puts the panel in play; the pool creates panels on demand.
    readonly property var assetsPanel: workspace.pool.assetsPanel
    readonly property var previewPanel: workspace.pool.previewPanel
    readonly property var propertiesPanel: workspace.pool.propertiesPanel
    readonly property var timelinePanel: workspace.pool.timelinePanel
    property alias androidMediaPreviewLoader: mediaPreviewHost.loader

    // Qt creates some chrome itself — most visibly the Undo/Cut/Copy/Paste menu on every
    // TextField and TextArea (Basic/TextField.qml declares ContextMenu.menu). Drift styles
    // none of that, so it fell through to the palette the platform theme supplies and picked
    // up the desktop's colour scheme: on a KDE session with a custom scheme the editing menu
    // rendered in that scheme's colours next to Drift's own. Palette propagates down the item
    // hierarchy, popups included, so setting the roles the Basic style reads brings Qt's own
    // chrome under Theme — and binding them keeps it following the light/dark toggle.
    palette.window: Theme.c.appWindow.paletteWindow
    palette.windowText: Theme.c.appWindow.paletteWindowText
    palette.base: Theme.c.appWindow.paletteBase
    palette.text: Theme.c.appWindow.paletteText
    palette.button: Theme.c.appWindow.paletteButton
    palette.buttonText: Theme.c.appWindow.paletteButtonText
    palette.placeholderText: Theme.c.appWindow.palettePlaceholder
    // Menu border and item states: dark draws the frame, light the hovered row,
    // midlight the pressed one.
    palette.dark: Theme.c.appWindow.paletteDark
    palette.mid: Theme.c.appWindow.paletteMid
    palette.midlight: Theme.c.appWindow.paletteMidlight
    palette.light: Theme.c.appWindow.paletteLight
    // Drawn at 12% and 50% alpha for the menu's drop shadow; appBackground would
    // make that a white glow in light mode.
    palette.shadow: Theme.c.appWindow.paletteShadow
    palette.highlight: Theme.c.appWindow.paletteHighlight
    palette.highlightedText: Theme.c.appWindow.paletteHighlightedText
    palette.toolTipBase: Theme.c.appWindow.paletteToolTipBase
    palette.toolTipText: Theme.c.appWindow.paletteToolTipText

    width: 1280
    height: 800
    // Below this the split minimums cannot all be satisfied and panels overlap.
    // Left at the expanded-layout floor on purpose. Lowering it is a prerequisite for the
    // single-pane collapse below 600dp, which is not built yet — until it is, a narrower window
    // would only let the desktop arrangement be squashed into a size it cannot lay out in.
    minimumWidth: Theme.windowMinimumWidth
    minimumHeight: Theme.windowMinimumHeight

    // Shown from Component.onCompleted, once the stored geometry is in place:
    // assigning it to a window that is already up makes it jump across the screen,
    // and a session left maximized would flash at its windowed size first.
    // Use visibility only — setting both this and `visible` makes Qt warn
    // "Conflicting properties 'visible' and 'visibility'" (Maximized + hidden).
    visibility: Window.Hidden
    title: "CutWire Drift"
    // Alpha stays 1 unless this process was started with a translucent surface.
    // backgroundOpacity is read here so the binding follows theme reloads.
    color: {
        const opacity = ThemeManager.window.backgroundOpacity
        const multiplier = (opacity === undefined || opacity === null) ? 1 : opacity
        return WindowBackdrop.windowBackground(Theme.c.appWindow.background, multiplier)
    }

    LayoutMirroring.enabled: Qt.application.layoutDirection === Qt.RightToLeft
    LayoutMirroring.childrenInherit: true

    // This shell is reachable on Android (DeX, tablets with a mouse). Android defaults to touch
    // metrics, which would also switch off mouse drag and tap on the asset tiles. A Binding
    // rather than onCompleted, so it is in place before any child reads Theme.touchUi.
    Binding {
        target: Theme
        property: "inputModeOverride"
        value: PointerDevices.mousePresent ? "pointer" : "touch"
        when: Qt.platform.os === "android"
    }

    AndroidLifecycle {
        enabled: Qt.platform.os === "android"
        denoiseWindow: denoiseWindowLoader.item
    }

    // Set true after the unsaved prompt resolves so onClosing can finish quit.
    property bool forceClose: false

    // How many Overlay-hosted dialogs/tool windows are open. Touch only: PointerHandlers on
    // the editor hit-test their parent bounds, not z-order, so a finger on empty dialog chrome
    // would still reach the timeline; this count drives a guard over the page.
    property int overlayModalCount: 0

    function pushOverlayModal() {
        overlayModalCount++
    }

    function popOverlayModal() {
        overlayModalCount = Math.max(0, overlayModalCount - 1)
    }
    property bool manualUpdateCheck: false

    // On macOS, instantiate a native MenuBar via Qt.labs.platform.
    // On other platforms (Windows, Linux, Android), no menu bar is created so the
    // custom in-window header (EditorHeader) and window chrome are completely untouched.
    Loader {
        active: Theme.isMacOS
        sourceComponent: Component {
            MainMacMenuBar {
                appWindow: window
                header: editorHeader
                assets: assetsPanel
                downloadsLoader: downloadsWindowLoader
            }
        }
    }


    onClosing: function (close) {
        // Before any of the branches below, so a quit that is cancelled at the
        // unsaved prompt still records where the window was.
        window.persistWindowState()
        if (window.forceClose || !EditorState.projectFile.hasUnsavedChanges) {
            workspace.closeWindows()
            return
        }
        close.accepted = false
        // Leave preview fullscreen so the save prompt isn't sitting on a blank
        // full-window preview.
        if (window.previewFullscreen)
            window.togglePreviewFullscreen()
        editorHeader.confirmIfDirty(function () {
            // Don't Save leaves dirty true; clear it so aboutToQuit does not
            // write a recovery the user just declined. Harmless after Save.
            EditorState.projectFile.discardUnsavedChanges()
            window.forceClose = true
            window.close()
        })
    }

    // ── Session layout memory ───────────────────────────────────────────────────
    // Window placement and panel proportions come back from the last session
    // (LayoutStore / QSettings) instead of resetting to the defaults every launch.
    // App-wide, not per-project: the layout follows the display the user is at.

    // The rect to reopen at. Sampled only while the window is ordinary-sized, so
    // maximizing or going fullscreen never overwrites what a restore should return
    // to — which is also what "restore down" means to the window manager.
    property rect windowedGeometry: Qt.rect(0, 0, 0, 0)

    // Platforms report the new size before the new visibility when a window is
    // maximized, so sampling on the geometry change itself would record the
    // maximized rect as the windowed one. Letting it settle sidesteps the race, and
    // keeps a drag-resize from writing on every frame.
    Timer {
        id: geometrySettleTimer
        interval: 250
        onTriggered: {
            // Tested by exclusion rather than against Window.Windowed: a window shown
            // via AutomaticVisibility can report that instead of Windowed, and that is
            // an ordinary window — it is only the three modes below that are not.
            const mode = window.visibility
            if (mode !== Window.Maximized && mode !== Window.FullScreen
                    && mode !== Window.Minimized && !window.mainPreviewFullscreen)
                window.windowedGeometry = Qt.rect(window.x, window.y, window.width, window.height)
            window.persistWindowState()
        }
    }

    onXChanged: geometrySettleTimer.restart()
    onYChanged: geometrySettleTimer.restart()
    onWidthChanged: {
        // Theme is a singleton and cannot see a window, so the size class it reports has to be
        // fed from whichever root is live. Screen is the wrong source: it ignores tiling.
        Theme.windowWidth = width
        geometrySettleTimer.restart()
    }

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
    onHeightChanged: geometrySettleTimer.restart()

    // System-bar and cutout insets. Desktop windows have none, so this stays 0 there.
    function _safeMargin(side) {
        return Qt.platform.os === "android" ? window.contentItem.SafeArea.margins[side] : 0
    }

    function restoreWindowGeometry() {
        // The platform sizes the window on Android; a stored desktop rect means nothing there.
        if (Qt.platform.os === "android")
            return
        const geometry = LayoutMemory.savedWindowGeometry(window.minimumWidth, window.minimumHeight)
        // Null rect: nothing stored, or the monitor it was stored on is gone. Either
        // way the window keeps the declared defaults above.
        if (geometry.width <= 0 || geometry.height <= 0)
            return
        window.x = geometry.x
        window.y = geometry.y
        window.width = geometry.width
        window.height = geometry.height
        window.windowedGeometry = geometry
    }

    function showRestored() {
        if (Qt.platform.os === "android") {
            window.visibility = Window.Maximized
            return
        }
        // Assigning visibility shows the window too, so a session that was left
        // maximized never appears at its windowed size on the way there.
        window.visibility = LayoutMemory.savedWindowMaximized()
                            ? Window.Maximized : Window.Windowed
        // A launch nobody resizes never emits a geometry change, so the sampler that
        // hangs off those signals would never run and the session would save nothing.
        geometrySettleTimer.restart()
    }

    // Which of the two reopen modes the window is currently in, or -1 while it is in
    // a state that says nothing about it. Fullscreen is a mode the user toggles into,
    // not a size to reopen at, so it answers for what was underneath it; minimized
    // answers for nothing at all and leaves the stored choice alone.
    function restoreModeIsMaximized() {
        switch (window.visibility) {
        case Window.Maximized:
            return 1
        case Window.Windowed:
        case Window.AutomaticVisibility:
            return 0
        case Window.FullScreen:
            return window._preFullscreenVisibility === Window.Maximized ? 1 : 0
        default:
            return -1
        }
    }

    // Written as the values settle rather than only on the way out: a session that
    // ends in a crash, a SIGTERM from the session manager, or a force quit never runs
    // a teardown handler, and losing the layout to that is exactly the annoyance this
    // is here to remove.
    function persistWindowState() {
        if (Qt.platform.os === "android")
            return
        const maximized = window.restoreModeIsMaximized()
        if (maximized < 0)
            return
        LayoutMemory.saveWindowState(window.windowedGeometry, maximized === 1)
    }

    // Fullscreen preview: the window goes fullscreen *and* every panel around the
    // preview collapses, so the video actually fills the display. Toggling the
    // window alone just scaled the same three-pane layout up.
    property bool previewFullscreen: false
    // The window that went fullscreen: the one holding the preview, which may be a secondary
    // workspace window.
    property var fullscreenWindow: null
    readonly property bool mainPreviewFullscreen: previewFullscreen && fullscreenWindow === window
    // Restoring to Windowed unconditionally would silently un-maximize a window
    // that was maximized before going fullscreen.
    property int _preFullscreenVisibility: Window.Windowed

    function togglePreviewFullscreen() {
        if (previewFullscreen) {
            const target = fullscreenWindow ?? window
            previewFullscreen = false
            fullscreenWindow = null
            // KDE Plasma doesn't handle Fullscreen -> Maximized for some reason
            target.visibility = _preFullscreenVisibility // Fullscreen -> Windowed
            target.visibility = _preFullscreenVisibility // Windowed -> Maximized (if it was previously maximized)
        } else {
            const target = previewPanel?.Window.window ?? window
            _preFullscreenVisibility = target.visibility
            fullscreenWindow = target
            previewFullscreen = true
            target.visibility = Window.FullScreen
        }
    }

    function restoreWindows() {
        workspace.restoreWindows()
    }

    // Android's system Back, which is not Escape. Newest-on-top: a tool window is a full-screen
    // surface over everything else, then dialogs, then fullscreen preview. Nothing to unwind does
    // nothing: Back must not close the main window from here.
    Shortcut {
        sequences: [StandardKey.Back]
        context: Qt.ApplicationShortcut
        enabled: Qt.platform.os === "android"
        onActivated: window.handleBack()
    }

    function handleBack() {
        if (window.mediaPreviewPage) {
            window.mediaPreviewPage.close()
            return
        }
        const tools = [segmentationWindowLoader, denoiseWindowLoader, restoreWindowLoader,
                       multicamWindowLoader, speedCurveWindowLoader, fadeCurveWindowLoader,
                       downloadsWindowLoader]
        for (var t = 0; t < tools.length; ++t) {
            if (tools[t].shown) {
                tools[t].item.close()
                return
            }
        }
        const modals = [shellSwitchDialogLoader.item, aboutDialogLoader.item, debugInfoDialogLoader.item,
                        settingsDialogLoader.item, pasteAttributesDialogLoader.item, updateDialogLoader.item,
                        addonManagerDialogLoader.item, missingAddonsDialogLoader.item, addonStartupDialog,
                        projectSetupDialogLoader.item, layoutChooserDialogLoader.item,
                        languageChooserDialogLoader.item, recoveryDialogLoader.item,
                        reverseProgressDialogLoader.item, subtitleProgressDialogLoader.item]
        for (var i = 0; i < modals.length; ++i) {
            const m = modals[i]
            if (!m || !m.visible)
                continue
            // close() skips accepted()/rejected(), so a dialog with no dismissal (recovery, the
            // first-run choosers) would be left unanswered. Those two progress dialogs cancel their
            // job on close; their own Cancel button stays the way out.
            if (!(m.closePolicy & Popup.CloseOnEscape)
                    || m === reverseProgressDialogLoader.item || m === subtitleProgressDialogLoader.item)
                return
            m.close()
            return
        }
        if (editorHeader.closeTopDialog())
            return
        if (window.previewFullscreen)
            window.togglePreviewFullscreen()
    }

    Shortcut {
        sequence: "Esc"
        enabled: window.previewFullscreen && !window.showStartScreen
        onActivated: window.togglePreviewFullscreen()
    }

    // The preference is stored as "not chosen" / portrait / landscape rather than a single
    // bool, so the canvas can keep driving it until the user takes the decision away — the
    // effective value is resolved here because it depends on both preference and canvas,
    // which are separate notifiers on the C++ side.
    readonly property string workspaceLayout: EditorState.preferences.workspaceLayoutOverridden
                                              ? EditorState.preferences.workspaceLayoutPreferred
                                              : (EditorState.projectPortrait ? "portrait" : "landscape")

    // The workspace actually on screen: the active one, or its portrait alternative when
    // the layout preference (or the canvas, in auto) asks for portrait.
    readonly property string effectiveWorkspaceId: {
        Workspaces.workspaces
        const active = Workspaces.activeId
        const alternative = Workspaces.workspace(active).portraitAlternative
        if (alternative && window.workspaceLayout === "portrait"
                && Workspaces.workspace(alternative).id === alternative)
            return alternative
        return active
    }
    readonly property bool portraitWorkspace: window.effectiveWorkspaceId !== Workspaces.activeId
                                              || window.effectiveWorkspaceId === "edit-portrait"

    function configureAndAddAsset(assetIndex, runner) {
        if (!EditorState.shouldConfigureProjectForAsset(assetIndex)) {
            runner()
            return
        }
        projectSetupDialogLoader.ensure().openForAsset(assetIndex, runner)
    }

    // "Decide later" closes the first-run chooser without settling on a canvas size.
    // Tracked separately from EditorState.projectLayoutChosen — which means "the canvas
    // size is decided" and gates ProjectSetupDialog — so the chooser can move on while
    // the first video/image clip still gets offered a setup step. Session-only,
    // matching projectLayoutChosen itself.
    property bool layoutPromptDismissed: false

    // True only during the startup window where continueStartupAfterLanguage() has
    // decided to ask which project to open, rather than settle on one itself — and on
    // first launch, behind the language chooser that runs before it. Cleared for good
    // once that choice is made.
    property bool showStartScreen: false

    // Header Extras icon pulses while true; never auto-opens a dialog.
    readonly property alias addonAttentionNeeded: addonStartupDialog.needsAttention

    function promptLanguageChooserIfNeeded() {
        if (!EditorState.preferences.needsUiLanguagePrompt)
            return false
        if (languageChooserDialogLoader.shown)
            return true
        languageChooserDialogLoader.ensure().openChooser()
        return true
    }

    function promptLayoutChooserIfNeeded() {
        if (EditorState.preferences.needsUiLanguagePrompt || languageChooserDialogLoader.shown)
            return
        if (EditorState.projectFile.recoveryAvailable || EditorState.projectLayoutChosen)
            return
        if (window.layoutPromptDismissed)
            return
        // The start screen is its own waiting room: land there and let the user pick
        // New / Open / a recent project rather than racing them with this dialog too.
        if (window.showStartScreen)
            return
        if (layoutChooserDialogLoader.shown || recoveryDialogLoader.shown)
            return
        layoutChooserDialogLoader.ensure().openChooser()
    }

    // Quiet catalog refresh for the header attention indicator.
    function refreshAddonAttention() {
        addonStartupDialog.refreshAttention()
    }

    // Settings / header: reopen platform layout picker anytime.
    function openLayoutChooser() {
        layoutChooserDialogLoader.ensure().openFromSettings()
    }

    LazyLoader {
        id: imageSequenceDialogLoader
        sourceComponent: Component {
            ImageSequenceDialog { }
        }
    }

    LazyLoader {
        id: projectSetupDialogLoader
        sourceComponent: Component {
            ProjectSetupDialog { }
        }
    }

    LazyLoader {
        id: languageChooserDialogLoader
        sourceComponent: Component {
            LanguageChooserDialog {
                // First-launch only. After Continue the language is stored, then the usual
                // recovery / layout prompts can run.
                onClosed: window.continueStartupAfterLanguage()
            }
        }
    }

    LazyLoader {
        id: layoutChooserDialogLoader
        sourceComponent: Component {
            LayoutChooserDialog {
                // rejected() fires before closed(), so the flag is already set by the time
                // callers check it.
                onFirstRunDismissed: window.layoutPromptDismissed = true
            }
        }
    }

    ProjectLoadDialog { }

    LazyLoader {
        id: recoveryDialogLoader
        sourceComponent: Component {
            RecoveryDialog {
                onClosed: {
                    Qt.callLater(window.promptLayoutChooserIfNeeded)
                    window.openPendingLaunch()
                }
            }
        }
    }

    LazyLoader {
        id: subtitleProgressDialogLoader
        sourceComponent: Component {
            SubtitleProgressDialog { }
        }
    }

    LazyLoader {
        id: reverseProgressDialogLoader
        sourceComponent: Component {
            ReverseProgressDialog { }
        }
    }

    LazyLoader {
        id: addonManagerDialogLoader
        sourceComponent: Component {
            AddonManagerDialog { }
        }
    }

    AddonStartupDialog {
        id: addonStartupDialog
    }

    LazyLoader {
        id: missingAddonsDialogLoader
        sourceComponent: Component {
            MissingAddonsDialog { }
        }
    }

    Connections {
        target: Updates
        function onInstallReady() {
            window.openUpdateDialog()
        }
        function onDownloadStarted() {
            downloadsWindowLoader.ensure().show()
        }
        function onQuitRequested() {
            window.close()
        }
        function onActivityChanged() {
            if (!Updates.checking && window.manualUpdateCheck) {
                window.manualUpdateCheck = false
                if (Updates.updateAvailable) {
                    window.openUpdateDialog()
                } else if (Updates.status.length > 0) {
                    if (Updates.status.indexOf(Updates.currentVersion) !== -1) {
                        Toasts.success(Updates.status)
                    } else {
                        Toasts.info(Updates.status)
                    }
                } else {
                    Toasts.success(qsTr("Drift %1 is the latest version.").arg(Updates.currentVersion))
                }
            }
        }
    }

    LazyLoader {
        id: aboutDialogLoader
        sourceComponent: Component {
            AboutDialog { }
        }
    }

    LazyLoader {
        id: updateDialogLoader
        sourceComponent: Component {
            UpdateDialog { }
        }
    }

    // Kept after its first close: it holds the last benchmark result.
    LazyLoader {
        id: debugInfoDialogLoader
        keepLoaded: true
        sourceComponent: Component {
            DebugInfoDialog { }
        }
    }

    LazyLoader {
        id: settingsDialogLoader
        sourceComponent: Component {
            SettingsDialog { }
        }
    }

    LazyLoader {
        id: shellSwitchDialogLoader
        sourceComponent: Component {
            ShellSwitchDialog { }
        }
    }

    LazyLoader {
        id: pasteAttributesDialogLoader
        sourceComponent: Component {
            PasteAttributesDialog { }
        }
    }

    LazyLoader {
        id: segmentationWindowLoader
        sourceComponent: Component {
            SegmentationWindow { }
        }
    }

    Connections {
        target: workspace.pool
        function onSourceLabShownChanged() {
            if (!workspace.pool.sourceLabShown)
                EditorState.sourceLab.clear()
        }
    }

    Connections {
        target: EditorState.segmentation
        function onOpenWindowRequested(track, clip, startSeconds, durationSeconds) {
            segmentationWindowLoader.ensure().openFor(track, clip, startSeconds, durationSeconds, true)
        }
    }

    Connections {
        target: EditorState
        function onOpenPasteAttributesRequested() {
            window.openPasteAttributes()
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
    }

    // Another view of the same timeline rather than an editor of its own — see MulticamWindow.
    LazyLoader {
        id: multicamWindowLoader
        sourceComponent: Component {
            MulticamWindow { }
        }
    }

    LazyLoader {
        id: downloadsWindowLoader
        sourceComponent: Component {
            DownloadsWindow { }
        }
    }

    // Shows the downloads window the first time a download starts, then stays out of the way:
    // reopening on every later job would yank focus mid-edit for something the header
    // badge already reports.
    property bool downloadsWindowShownOnce: false

    Connections {
        target: Market
        function onDownloadStarted(itemId) {
            if (window.downloadsWindowShownOnce)
                return
            window.downloadsWindowShownOnce = true
            downloadsWindowLoader.ensure().show()
        }
    }

    LazyLoader {
        id: denoiseWindowLoader
        sourceComponent: Component {
            DenoiseWindow { }
        }
    }

    LazyLoader {
        id: restoreWindowLoader
        sourceComponent: Component {
            RestoreWindow { }
        }
    }

    LazyLoader {
        id: speedCurveWindowLoader
        sourceComponent: Component {
            SpeedCurveWindow { }
        }
    }

    LazyLoader {
        id: fadeCurveWindowLoader
        sourceComponent: Component {
            FadeCurveWindow { }
        }
    }

    property var mediaPreviewPage: null

    LazyLoader {
        id: mediaPreviewWindowLoader
        sourceComponent: Component {
            MediaPreviewWindow { }
        }
    }

    // Opened from the clip inspector; a window rather than a dialog so the timeline stays visible.
    function openSegmentation(track, clip, startSeconds, durationSeconds) {
        segmentationWindowLoader.ensure().openFor(track, clip, startSeconds, durationSeconds)
    }

    function openDenoise(track, clip, durationSeconds) {
        if (workspace.pool.denoiseInLayout && !denoiseWindowLoader.shown) {
            workspace.pool.panel("denoise").parent?.parent?.activate?.("denoise")
            const panel = workspace.pool.denoisePanel
            if (panel) {
                panel.openFor(track, clip, durationSeconds)
                return
            }
        }
        denoiseWindowLoader.ensure().openFor(track, clip, durationSeconds)
    }

    function openRestore(track, clip) {
        const w = restoreWindowLoader.ensure()
        w.host = window
        w.openFor(track, clip)
    }

    function openRestoreAsset(assetId) {
        if (workspace.pool.sourceLabShown) {
            EditorState.sourceLab.load(assetId)
            return
        }
        const w = restoreWindowLoader.ensure()
        w.host = window
        w.openForAsset(assetId)
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

    readonly property bool mediaPreviewWindowOpen: mediaPreviewWindowLoader.shown

    function openMediaPreview(assetIndex) {
        if (workspace.pool.sourceLabShown) {
            EditorState.sourceLab.load(AssetLibrary.assetIdAt(assetIndex))
            return
        }
        // A VideoOutput in a secondary window paints black on Android, so use the in-window page.
        if (Qt.platform.os === "android") {
            if (window.mediaPreviewPage)
                return
            androidMediaPreviewLoader.active = true
            window.mediaPreviewPage = androidMediaPreviewLoader.item
            window.mediaPreviewPage.openFor(assetIndex)
            return
        }
        const viewer = workspace.pool.sourceViewerShown ? workspace.pool.sourceViewerPanel : null
        if (viewer && !mediaPreviewWindowLoader.shown) {
            viewer.openAsset(assetIndex)
            return
        }
        mediaPreviewWindowLoader.ensure().openFor(assetIndex)
    }

    function openCrop(track, clip) {
        if (workspace.pool.sourceLabShown) {
            EditorState.sourceLab.load(EditorState.clipAt(track, clip).assetId ?? "")
            return
        }
        // Same reason as openMediaPreview: the secondary window paints black on Android.
        if (Qt.platform.os === "android") {
            if (window.mediaPreviewPage)
                return
            androidMediaPreviewLoader.active = true
            window.mediaPreviewPage = androidMediaPreviewLoader.item
            window.mediaPreviewPage.openClip(track, clip)
            return
        }
        if (workspace.pool.sourceViewerShown)
            workspace.pool.sourceViewerPanel?.release()
        mediaPreviewWindowLoader.ensure().openClip(track, clip)
    }

    // Opened from the header and from the "multicam" shortcut. Unlike the windows above it is
    // not bound to one clip, so it survives any edit and only closes when the document does.
    function openMulticam() {
        multicamWindowLoader.ensure().openSession()
    }

    // Opened from the header's Settings menu. Every preference lives here now; the
    // assets rail no longer carries a settings tab.
    function openAgentAccess() {
        editorHeader.openAgentAccess()
    }

    function openSettings(pageId) {
        const dialog = settingsDialogLoader.ensure()
        if (pageId)
            dialog.openPage(pageId)
        else
            dialog.open()
    }

    function switchShell(target) {
        shellSwitchDialogLoader.ensure().openFor(target, false)
    }

    // Shell.qml only builds a shell at startup, so applying a layout change is a relaunch.
    // Android has no quit to hang it on (aboutToQuit never runs), so the recovery snapshot
    // carries unsaved work across instead of the prompt.
    function restartToApplyShell() {
        if (Qt.platform.os === "android") {
            EditorState.projectFile.flushRecoverySnapshot()
            EditorState.preferences.restartApp()
            return
        }
        editorHeader.confirmIfDirty(function () {
            EditorState.projectFile.discardUnsavedChanges()
            EditorState.preferences.restartApp()
            window.forceClose = true
            window.close()
        })
    }

    // ── Project lifecycle: New / Open / Open recent / Close ─────────────────────
    // The single gate for every entry point that starts, opens, or discards a
    // project — Ctrl+N/Ctrl+O, the header's Projects menu, the start screen's own
    // tiles, and an externally requested open (double-click a .drift, launch args)
    // all call these rather than keeping their own copy of the confirm-if-dirty
    // check and the showStartScreen bookkeeping.

    // Whether a load is in flight lives on EditorState (AppController::projectLoadPending),
    // not as a flag one of these functions sets — loadProject()/loadProjectJson() can be
    // reached from C++ too (consumeStartupProject, restoreLastSessionIfEnabled), and a
    // QML-only flag would miss those, letting Close/New race a startup load that is still
    // unpacking a bundle in the background.
    //
    // A second New/Open/Close while a load is still in flight has nowhere good to go:
    // loadProject()'s own generation counter guards against a *stale* extraction landing
    // on top of a *newer* one, but nothing invalidates one that is discarded by New/Close.
    // Simplest correct fix: only one project action in flight at a time.
    function rejectIfProjectOpenPending() {
        if (!EditorState.projectFile.projectLoadPending)
            return false
        Toasts.info(qsTr("Still opening a project — try again in a moment."))
        return true
    }

    function requestNewProject() {
        if (window.rejectIfProjectOpenPending())
            return
        editorHeader.confirmIfDirty(function () {
            EditorState.projectFile.newProject()
            window.showStartScreen = false
            window.promptLayoutChooserIfNeeded()
        })
    }

    function requestOpenProjectDialog() {
        if (window.rejectIfProjectOpenPending())
            return
        editorHeader.confirmIfDirty(function () {
            var url = FileDialogs.openFile(qsTr("Open Project"), editorHeader.projectFilter,
                                           editorHeader.projectMimeTypes)
            if (url == "")
                return
            EditorState.projectFile.loadProject(url)
        })
    }

    function requestOpenRecentProject(path) {
        if (window.rejectIfProjectOpenPending())
            return
        if (!path || path.length === 0)
            return
        editorHeader.confirmIfDirty(function () {
            EditorState.projectFile.openRecentProject(path)
        })
    }

    // Header's "Close project". Confirms like every action above, then discards the
    // document the same way New Project does — just landing on the start screen
    // afterwards instead of an empty one.
    function requestCloseProject() {
        if (window.rejectIfProjectOpenPending())
            return
        editorHeader.confirmIfDirty(function () {
            // Set before newProject(): it resets projectLayoutChosen, which a
            // Connections handler below reacts to by reopening the layout chooser —
            // the start screen needs to already be up so promptLayoutChooserIfNeeded()
            // defers to it instead.
            window.showStartScreen = true
            // silent: newProject()'s own "New project" toast is right for the header's
            // New Project action, wrong here — setting lastMessage again afterwards
            // would not replace it, since every change queues its own toast.
            EditorState.projectFile.newProject(true)
            EditorState.setLastMessage(qsTr("Project closed"))
        })
    }

    // Opened from the header, and from every empty state that a missing addon causes.
    function openAddonManager(kind) {
        if (kind === undefined)
            addonManagerDialogLoader.ensure().open()
        else
            addonManagerDialogLoader.ensure().openForKind(kind)
    }

    // A .driftfx from Drift Forge; an empty url asks with a file dialog. Shared with the effects
    // panel's Import button.
    function importUserPackage(url) {
        addonManagerDialogLoader.ensure().importUserPackage(url)
    }

    // Header Extras button: open the essential/update nudge when the icon is pulsing,
    // otherwise the full manager — same idea as the update badge vs silent check.
    function openExtras() {
        if (addonStartupDialog.openForAttention())
            return
        openAddonManager()
    }

    // Opened from the header badge, which only exists while there is something to show.
    function openAboutDialog() {
        aboutDialogLoader.ensure().open()
    }

    function checkForUpdates() {
        if (Updates.updateAvailable) {
            openUpdateDialog()
            return
        }
        if (Updates.checking) {
            Toasts.info(qsTr("Already checking for updates…"))
            return
        }
        window.manualUpdateCheck = true
        Toasts.info(qsTr("Checking for updates…"))
        Updates.checkNow()
    }

    function openUpdateDialog() {
        // Android updates come from the store; the dialog installs a desktop package.
        if (Qt.platform.os === "android")
            return
        updateDialogLoader.ensure().open()
    }

    function openDebugInfo() {
        debugInfoDialogLoader.ensure().open()
    }

    // Global search opens assets tabs and labels them; the panel itself is out of the header's reach.
    function showAssetsTab(tabId) {
        if (window.previewFullscreen)
            window.togglePreviewFullscreen()
        workspace.pool.showAssetsTab(tabId)
    }

    function assetsTabLabel(tabId) {
        return assetsPanel?.tabLabel(tabId) ?? ""
    }

    function focusEditor() {
        editorScope.forceActiveFocus()
    }

    function openPasteAttributes() {
        pasteAttributesDialogLoader.ensure().openDialog()
    }

    function promptRecoveryIfNeeded() {
        if (EditorState.preferences.needsUiLanguagePrompt || languageChooserDialogLoader.shown)
            return
        if (!EditorState.projectFile.recoveryAvailable || recoveryDialogLoader.shown)
            return
        // Opt-in reopen handles recovery (and last .drift) without asking.
        if (EditorState.preferences.reopenLastProject)
            return
        recoveryDialogLoader.ensure().open()
    }

    // Ask every launch while the previous session left an autosave snapshot
    // (typically a crash — a confirmed close already cleared it).
    Timer {
        id: recoveryOpenTimer
        interval: 150
        repeat: true
        triggeredOnStart: false
        property int attempts: 0
        onTriggered: {
            if (!EditorState.projectFile.recoveryAvailable) {
                stop()
                attempts = 0
                Qt.callLater(window.promptLayoutChooserIfNeeded)
                return
            }
            promptRecoveryIfNeeded()
            if (recoveryDialogLoader.shown || ++attempts >= 20)
                stop()
        }
    }

    Timer {
        id: layoutChooserOpenTimer
        interval: 120
        repeat: false
        onTriggered: window.promptLayoutChooserIfNeeded()
    }

    Component.onDestruction: if (ShellBridge.main === window) ShellBridge.main = null

    Component.onCompleted: {
        WindowBackdrop.attach(window)
        ShellBridge.main = window
        Theme.windowWidth = window.width
        MediaImport.sequencePrompt = function (found, done) {
            imageSequenceDialogLoader.ensure().ask(found, done)
        }
        window.restoreWindowGeometry()
        window.beginStartupProject()
        // Independent of the project-restore sequence above — it neither gates nor is
        // gated by it, so ordering here doesn't matter.
        EditorState.mcp.applyStartOnLaunch()
        // Last: the window is placed by now, and the startup flow keeps the ordering
        // it had when the window was shown at the end of completion.
        window.showRestored()
    }

    function beginStartupProject() {
        // First launch: the language chooser runs before anything decides which project to
        // open, and the empty editor behind it is a project nobody asked for. Raise the start
        // screen first, so the chooser sits on the page the rest of first launch lands on;
        // continueStartupAfterLanguage() lowers it again for the paths that open a project.
        if (EditorState.preferences.needsUiLanguagePrompt)
            window.showStartScreen = true
        if (window.promptLanguageChooserIfNeeded())
            return
        window.continueStartupAfterLanguage()
    }

    // A launch or share intent from Android. Returns true when it took over startup, so the
    // reopen-last-project and recovery paths stand down.
    function routeAndroidIntent(intent) {
        if (!intent || !intent.kind)
            return false
        const urls = intent.urls || []
        if (intent.kind === "view") {
            const url = urls.length > 0 ? String(urls[0]) : ""
            if (url === "" || Market.handleIncomingUrl(url))
                return url !== ""
            if (Addons.isUserPackage(url)) {
                window.importUserPackage(url)
                return false
            }
            // Loading deletes the recovery snapshot unasked, so let that prompt run first.
            if (EditorState.projectFile.recoveryAvailable && !EditorState.preferences.reopenLastProject) {
                window._pendingLaunchUrl = url
                recoveryOpenTimer.start()
                return true
            }
            window.openLaunchedProject(url)
            return true
        }
        if (intent.kind === "sendMedia") {
            if (urls.length > 0)
                MediaImport.importUrls(urls, false)
            return false
        }
        if (intent.kind === "sendText")
            Toasts.info(qsTr("Links can’t be opened in this layout."))
        return false
    }

    property string _pendingLaunchUrl: ""

    function openLaunchedProject(url) {
        if (window.rejectIfProjectOpenPending())
            return
        window.showStartScreen = false
        editorHeader.confirmIfDirty(function () {
            EditorState.projectFile.loadProject(url)
        })
    }

    function openPendingLaunch() {
        if (window._pendingLaunchUrl === "")
            return
        // A recovery restore is still landing; onProjectLoadFinished comes back here so the
        // unsaved prompt sees the recovered work.
        if (EditorState.projectFile.projectLoadPending)
            return
        const url = window._pendingLaunchUrl
        window._pendingLaunchUrl = ""
        window.openLaunchedProject(url)
    }

    function continueStartupAfterLanguage() {
        if (Qt.platform.os === "android" && window.routeAndroidIntent(FileDialogs.takeLaunchIntent()))
            return
        // A document the shell asked us to open (argv / QFileOpenEvent) wins over last-session
        // reopen and the recovery prompt.
        if (EditorState.projectFile.consumeStartupProject())
            return
        // An agent's instance (launch_instance) starts on the empty timeline it asked for:
        // no last-session restore, no recovery or layout prompt, no start screen.
        if (EditorState.projectFile.isolatedSession) {
            window.showStartScreen = false
            return
        }
        // Opt-in: restore unsaved recovery or the last clean project silently.
        if (EditorState.projectFile.restoreLastSessionIfEnabled())
            return
        if (EditorState.projectFile.recoveryAvailable) {
            recoveryOpenTimer.start()
            return
        }
        // Reopen is off (or the last project could not be restored): rather than land in a
        // fresh empty project, ask which project to work on.
        if (!EditorState.preferences.reopenLastProject) {
            window.showStartScreen = true
            return
        }
        // Reopen is on but there was nothing to reopen: a fresh project, set up through the
        // layout chooser — which defers to the start screen, so drop the one raised for the
        // language chooser first. (The paths above that open a document drop it themselves,
        // in onProjectLoadFinished, once the document is in.)
        window.showStartScreen = false
        layoutChooserOpenTimer.start()
    }

    onVisibilityChanged: {
        // Maximizing does not have to change the window size (a window already filling
        // the work area does not), so the geometry sampler alone can miss the switch.
        window.persistWindowState()
        if (visible && EditorState.projectFile.recoveryAvailable && !EditorState.preferences.reopenLastProject)
            recoveryOpenTimer.start()
    }

    // Warm start: a project tapped or media shared while Drift was already running.
    Connections {
        target: FileDialogs
        enabled: Qt.platform.os === "android"
        function onLaunchUrlReceived(url) {
            window.routeAndroidIntent({ kind: "view", urls: [url] })
        }
        function onIncomingIntent(intent) {
            if (intent && intent.kind !== "view")
                window.routeAndroidIntent(intent)
        }
    }

    Connections {
        target: EditorState
        function onExternalProjectOpenRequested(url) {
            if (window.rejectIfProjectOpenPending())
                return
            editorHeader.confirmIfDirty(function () {
                EditorState.projectFile.loadProject(url)
            })
        }

        // Each of these edits one clip of the project that was just discarded, so there is
        // nothing left for them to act on. The C++ sessions are already torn down; onClosing
        // calls the same end*Session() again, which no-ops.
        // Multicam addresses tracks by index, so it has nothing to act on either once the
        // document behind those indices is gone.
        function onProjectReset() {
            if (segmentationWindowLoader.item)
                segmentationWindowLoader.item.close()
            if (denoiseWindowLoader.item)
                denoiseWindowLoader.item.close()
            if (speedCurveWindowLoader.item)
                speedCurveWindowLoader.item.close()
            if (fadeCurveWindowLoader.item)
                fadeCurveWindowLoader.item.close()
            if (multicamWindowLoader.item)
                multicamWindowLoader.item.close()
        }

        function onOpenMulticamWindowRequested() {
            multicamWindowLoader.ensure().openSession()
        }

        function onProjectLayoutChosenChanged() {
            if (!EditorState.projectLayoutChosen) {
                // New Project reasserts this even when already false, so the layout
                // question is genuinely open again — including for a user who chose
                // "Decide later" last time round.
                window.layoutPromptDismissed = false
                layoutChooserOpenTimer.restart()
            }
        }

        // --- Error and status surfacing -------------------------------------
        // These signals previously had no handler anywhere in QML, so a failed
        // export or transcription was reported only to the console.
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

        // `lastMessage` is the backend's general-purpose status line. It used to
        // render as static muted text in the header that one message silently
        // overwrote; it now goes through the toast queue like everything else.
        function onLastMessageChanged() {
            const message = EditorState.lastMessage
            if (message.length === 0)
                return
            // Severity comes from the backend. This used to infer it by regexing the
            // message prose, which matched none of the real failure strings — a
            // corrupt-project open read as a neutral info toast that auto-dismissed
            // in five seconds, identical to "Project saved".
            switch (EditorState.lastMessageSeverity) {
            case "error":   Toasts.error(message); break
            case "warning": Toasts.warning(message); break
            case "success": Toasts.success(message); break
            default:        Toasts.info(message); break
            }
        }

        // Raised when an edit is refused (e.g. transforming a locked clip).
        // Only PreviewPanel used to show this, so the same block initiated from
        // the timeline was silent.
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
            if (EditorState.projectFile.recoveryAvailable) {
                recoveryOpenTimer.start()
            } else {
                Qt.callLater(window.promptLayoutChooserIfNeeded)
            }
        }

        // Terminal result of a loadProject()/loadProjectJson() call, from any source —
        // unlike lastMessageChanged, this does not also fire for the "Unpacking project
        // media…" progress message a bundle with embedded media raises first, so it's
        // safe to treat as "the open is done" rather than mistaking progress for success.
        // rejectIfProjectOpenPending() keeps at most one load in flight at a time, so
        // whichever request this is, it's the one the start screen (if up) is waiting on.
        function onProjectLoadFinished(ok) {
            if (ok)
                window.showStartScreen = false
            window.openPendingLaunch()
        }

        // An agent's close_project (true) / new_project or load_project (false). The agent already discarded
        // the document without asking, so there is no unsaved prompt here — only the screen
        // change and the same "Project closed" note requestCloseProject() posts.
        function onStartScreenRequested(visible) {
            window.showStartScreen = visible
            if (visible)
                EditorState.setLastMessage(qsTr("Project closed"))
        }

        function onMissingAddons(addons) {
            missingAddonsDialogLoader.ensure().openFor(addons)
        }

        function onPackageFinished(ok, message) {
            if (ok)
                Toasts.success(message)
            else
                Toasts.error(qsTr("Couldn't create the shareable copy: %1").arg(message))
        }
    }

    Connections {
        target: Addons
        function onRefreshingChanged() {
            if (!Addons.refreshing)
                Qt.callLater(window.refreshAddonAttention)
        }
        function onCatalogChanged() {
            Qt.callLater(window.refreshAddonAttention)
        }
        function onRemindEssentialChanged() {
            Qt.callLater(window.refreshAddonAttention)
        }
        function onRemindUpdatesChanged() {
            Qt.callLater(window.refreshAddonAttention)
        }

        // A download or signature failure only reached the addon manager dialog's
        // status line, so a pack that failed to install while that dialog was closed
        // — the normal case for the attention nudge — failed completely silently.
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

    // The one place a bound action is dispatched, shared by the ApplicationShortcut
    // Repeater below and the editor FocusScope's arrow handling. A handful of ids are
    // QML state and have no triggerAction branch at all, so a second dispatch site
    // that called EditorState.triggerAction directly would turn them into dead keys
    // the moment someone rebound one onto an arrow chord.
    //
    // Escape (clearSelection): CapCut-style — if a timeline cut tool is active,
    // first press returns to Select; only then does Escape clear the selection.
    function dispatchAction(id) {
        if (ShellBridge.keyframesActionHandler && ShellBridge.keyframesActionHandler.handleAction(id))
            return
        if (ShellBridge.subtitleActionHandler && ShellBridge.subtitleActionHandler.handleAction(id))
            return
        if (ShellBridge.sourceLabActionHandler && ShellBridge.sourceLabActionHandler.handleAction(id))
            return
        if (ShellBridge.sourceLabRangesHandler && ShellBridge.sourceLabRangesHandler.handleAction(id))
            return
        if (id === "workspace.restoreWindows") {
            window.restoreWindows()
            return
        }
        if (id === "clearSelection" && previewFullscreen && fullscreenWindow !== window) {
            window.togglePreviewFullscreen()
            return
        }
        if (id === "clearSelection" && EditorState.preview.guideEditSetId !== "") {
            EditorState.preview.guideEditSetId = ""
            return
        }
        if (id === "clearSelection"
                && timelinePanel?.visible
                && timelinePanel.timelineTool !== "") {
            timelinePanel.timelineTool = ""
            return
        }
        // Tool modes are QML state, so they are dispatched here rather
        // than by triggerAction.
        if (id === "selectTool") {
            if (timelinePanel)
                timelinePanel.timelineTool = ""
            return
        }
        if (id === "bladeTool") {
            if (timelinePanel)
                timelinePanel.timelineTool = "split"
            return
        }
        // Timeline zoom is QML state as well, and the 1.5 step matches the
        // toolbar buttons. setZoom clamps to minZoom/maxZoom and re-anchors on
        // the playhead, so holding the key walks to the end of the range and stops.
        if (id === "zoomIn") {
            if (timelinePanel?.visible)
                timelinePanel.setZoom(timelinePanel.zoom * 1.5)
            return
        }
        if (id === "zoomOut") {
            if (timelinePanel?.visible)
                timelinePanel.setZoom(timelinePanel.zoom / 1.5)
            return
        }
        if (id === "search") {
            if (editorHeader.visible)
                editorHeader.focusSearch()
            return
        }
        if (id === "workspace.next" || id === "workspace.prev") {
            const list = Workspaces.workspaces
            const step = id === "workspace.next" ? 1 : -1
            const index = list.findIndex(w => w.id === Workspaces.activeId)
            if (list.length > 0)
                Workspaces.activeId = list[(index + step + list.length) % list.length].id
            return
        }
        if (/^workspace\.[1-9]$/.test(id)) {
            const ws = Workspaces.workspaces[Number(id.slice(10)) - 1]
            if (ws)
                Workspaces.activeId = ws.id
            return
        }
        EditorState.triggerAction(id)
    }

    // Shortcut is not an Item, so wrap each binding in a zero-size host.
    Repeater {
        model: EditorState.actions
        Item {
            required property var modelData
            width: 0
            height: 0
            Shortcut {
                sequence: Theme.nativeShortcutSequence(modelData.shortcut)
                context: Qt.ApplicationShortcut
                // ApplicationShortcut fires regardless of what is on screen, so without
                // this a key like M (toggleBookmark) still edited the document hidden
                // behind the start screen. The editor Column being disabled blocks mouse
                // input the same way, but Shortcut is not an Item and does not inherit
                // that — it needs its own guard.
                //
                // New Project and Open stay live even here: both end up in
                // window.requestNewProject()/requestOpenProjectDialog() (via EditorHeader's
                // triggerAction Connections below), which already gate on a pending load
                // and unsaved changes — the same functions the start screen's own tiles
                // call — so there is nothing editing-shaped about letting them through.
                enabled: (modelData.id === "newProject" || modelData.id === "open"
                         || !window.showStartScreen)
                         && !Theme.shortcutSequenceUsesArrowKey(modelData.shortcut)
                onActivated: window.dispatchAction(modelData.id)
            }
        }
    }

    // There is no menu bar and no help overlay, so the only route to the shortcut
    // list was an unlabelled icon in a vertical rail that can itself be scrolled out
    // of view on a short window. F1 is the conventional way in and opens the
    // keyboard shortcuts page of Settings.
    Shortcut {
        sequence: "F1"
        context: Qt.ApplicationShortcut
        enabled: !window.showStartScreen
        onActivated: window.openSettings("keymap")
    }

    // Arrow-key actions (frame step, 1s/10s jump, Alt-nudge, Alt cut-point)
    // are handled here rather than as ApplicationShortcut. The focused item
    // sees the key first — text fields keep their cursor, overlay handles keep
    // their nudge — and only an unaccepted arrow bubbles up. ApplicationShortcut
    // matching for Left/Right is also a no-op on several Wayland compositors
    // (Hyprland / Omarchy), which is what made the keys look completely dead.
    FocusScope {
        id: editorScope
        anchors.fill: parent
        anchors.leftMargin: window._safeMargin("left")
        anchors.topMargin: window._safeMargin("top")
        anchors.rightMargin: window._safeMargin("right")
        anchors.bottomMargin: window._safeMargin("bottom")
        enabled: !window.showStartScreen
        focus: true
        Keys.onPressed: function(event) {
            const id = EditorState.actionForArrowChord(event.key, event.modifiers)
            if (id === "")
                return
            window.dispatchAction(id)
            event.accepted = true
        }

        EditorWorkspace {
            id: workspace
            onDownloadsRequested: downloadsWindowLoader.ensure().show()
        }
    }

    // Startup only: replaces a fresh empty project when "Reopen last project on
    // startup" is off, so the choice of what to work on is the user's rather than
    // defaulting to blank. Declared last (after the editor Column) so it draws over
    // the already-empty document sitting underneath it.
    Loader {
        anchors.fill: parent
        anchors.leftMargin: window._safeMargin("left")
        anchors.topMargin: window._safeMargin("top")
        anchors.rightMargin: window._safeMargin("right")
        anchors.bottomMargin: window._safeMargin("bottom")
        active: window.showStartScreen
        sourceComponent: Component {
            StartScreen {
                // Delegate to the same functions Ctrl+N / Ctrl+O / the header's Projects menu
                // use, so there is exactly one place that gates on unsaved changes and decides
                // when the screen is allowed to disappear.
                onNewProjectRequested: window.requestNewProject()
                onOpenProjectRequested: window.requestOpenProjectDialog()
                onOpenRecentRequested: (path) => window.requestOpenRecentProject(path)
                onMobileLayoutRequested: window.switchShell("mobile")
                onMoreSettingsRequested: window.openSettings()
                onDebugInfoRequested: window.openDebugInfo()
                onAgentAccessRequested: editorHeader.openAgentAccess()
            }
        }
    }

    MainAndroidMediaPreviewOverlay {
        id: mediaPreviewHost
    }

    MainOverlayInputGuard { }

    // Desktop has no sheet to step aside, so the drop target is live from the lift.
    Connections {
        target: TouchDrag
        function onActiveChanged() {
            if (TouchDrag.active)
                TouchDrag.clearOfSource = true
        }
    }

    TouchDragGhost {
        visible: TouchDrag.active
        z: 10000
    }

    // Notification host — above all panels, so any message lands in one place.
    ToastHost {
        topMargin: editorHeader.visible ? editorHeader.height : 0
    }
}
