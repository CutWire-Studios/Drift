import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Window
import Drift
import "components"

ApplicationWindow {
    id: window

    width: 1280
    height: 800
    // Below this the split minimums cannot all be satisfied and panels overlap.
    minimumWidth: Theme.windowMinimumWidth
    minimumHeight: Theme.windowMinimumHeight
    // Shown from Component.onCompleted, once the stored geometry is in place:
    // assigning it to a window that is already up makes it jump across the screen,
    // and a session left maximized would flash at its windowed size first.
    // Use visibility only — setting both this and `visible` makes Qt warn
    // "Conflicting properties 'visible' and 'visibility'" (Maximized + hidden).
    visibility: Window.Hidden
    title: "CutWire Drift"
    color: Theme.appBackground

    LayoutMirroring.enabled: Qt.application.layoutDirection === Qt.RightToLeft
    LayoutMirroring.childrenInherit: true

    // Set true after the unsaved prompt resolves so onClosing can finish quit.
    property bool forceClose: false

    onClosing: function (close) {
        // Before any of the branches below, so a quit that is cancelled at the
        // unsaved prompt still records where the window was.
        window.persistLayout()
        if (window.forceClose || !EditorState.hasUnsavedChanges)
            return
        close.accepted = false
        // Leave preview fullscreen so the save prompt isn't sitting on a blank
        // full-window preview.
        if (window.previewFullscreen)
            window.togglePreviewFullscreen()
        editorHeader.confirmIfDirty(function () {
            // Don't Save leaves dirty true; clear it so aboutToQuit does not
            // write a recovery the user just declined. Harmless after Save.
            EditorState.discardUnsavedChanges()
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
                    && mode !== Window.Minimized && !window.previewFullscreen)
                window.windowedGeometry = Qt.rect(window.x, window.y, window.width, window.height)
            window.persistWindowState()
        }
    }

    onXChanged: geometrySettleTimer.restart()
    onYChanged: geometrySettleTimer.restart()
    onWidthChanged: geometrySettleTimer.restart()
    onHeightChanged: geometrySettleTimer.restart()

    function restoreWindowGeometry() {
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
        const maximized = window.restoreModeIsMaximized()
        if (maximized < 0)
            return
        LayoutMemory.saveWindowState(window.windowedGeometry, maximized === 1)
    }

    function persistLayout() {
        window.persistWindowState()
        window.capturePanelFractions()
        for (const key in window.panelFractions)
            LayoutMemory.setPanelFraction(key, window.panelFractions[key])
    }

    // Panel sizes are kept as a fraction of the split they sit in rather than a pixel
    // width, so a layout dragged out on a 4K display still reads on a laptop screen.
    // Keyed by workspace, because portrait and landscape are different arrangements
    // and each deserves to remember its own.
    property var panelFractions: ({})
    // Nothing is captured until the stored layout has been applied, or the defaults
    // from the first layout pass would immediately overwrite it.
    property bool panelLayoutRestored: false

    Timer {
        id: panelSettleTimer
        interval: 250
        onTriggered: {
            window.capturePanelFractions()
            for (const key in window.panelFractions)
                LayoutMemory.setPanelFraction(key, window.panelFractions[key])
        }
    }

    function schedulePanelCapture() {
        if (!window.panelLayoutRestored || workspaceApplyTimer.running)
            return
        panelSettleTimer.restart()
    }

    function capturePanelFractions() {
        // Fullscreen collapses every panel around the preview; those sizes are a mode,
        // not a layout, and must not be written over the user's arrangement.
        if (window.previewFullscreen)
            return
        const prefix = window.workspaceLayout + "/"
        if (outerSplit.height > 0 && innerSplit.height > 0)
            window.panelFractions[prefix + "editorRow"] = innerSplit.height / outerSplit.height
        if (innerSplit.width > 0) {
            window.panelFractions[prefix + "assets"] = assetsPanel.width / innerSplit.width
            window.panelFractions[prefix + "properties"] = propertiesPanel.width / innerSplit.width
        }
        // Landscape hands the preview the slack in its row (fillWidth), so it has no
        // width of its own to remember there.
        if (window.portraitWorkspace && rootSplit.width > 0)
            window.panelFractions[prefix + "preview"] = previewPanel.width / rootSplit.width
    }

    // Writing an attached SplitView size drops the default binding behind it — which
    // is the point: from here on the panel keeps the size the user chose, exactly as
    // it does after a handle drag.
    function applySavedPanelSizes() {
        const prefix = window.workspaceLayout + "/"
        const editorRow = LayoutMemory.panelFraction(prefix + "editorRow", 0)
        if (editorRow > 0 && outerSplit.height > 0)
            innerSplit.SplitView.preferredHeight = outerSplit.height * editorRow
        const assets = LayoutMemory.panelFraction(prefix + "assets", 0)
        if (assets > 0 && innerSplit.width > 0)
            assetsPanel.SplitView.preferredWidth = innerSplit.width * assets
        const properties = LayoutMemory.panelFraction(prefix + "properties", 0)
        if (properties > 0 && innerSplit.width > 0)
            propertiesPanel.SplitView.preferredWidth = innerSplit.width * properties
        if (window.portraitWorkspace) {
            const preview = LayoutMemory.panelFraction(prefix + "preview", 0)
            if (preview > 0 && rootSplit.width > 0)
                previewPanel.SplitView.preferredWidth = rootSplit.width * preview
        }
    }

    // Every extent is 0 on the first layout pass, and a fraction of 0 is 0 — so the
    // restore waits for real sizes rather than running at Component.onCompleted.
    function restorePanelSizesWhenReady() {
        if (window.panelLayoutRestored)
            return
        if (rootSplit.width <= 0 || outerSplit.height <= 0 || innerSplit.width <= 0)
            return
        window.applySavedPanelSizes()
        window.panelLayoutRestored = true
    }

    // A workspace flip moves the preview between splits, so the panels around it are
    // still resizing on the frame the flip lands. Reapplying immediately would divide
    // by stale extents; waiting lets the new arrangement settle first, and capture is
    // suppressed until it has (schedulePanelCapture checks this timer).
    Timer {
        id: workspaceApplyTimer
        interval: 100
        onTriggered: window.applySavedPanelSizes()
    }

    // Fullscreen preview: the window goes fullscreen *and* every panel around the
    // preview collapses, so the video actually fills the display. Toggling the
    // window alone just scaled the same three-pane layout up.
    property bool previewFullscreen: false
    // Restoring to Windowed unconditionally would silently un-maximize a window
    // that was maximized before going fullscreen.
    property int _preFullscreenVisibility: Window.Windowed

    function togglePreviewFullscreen() {
        if (previewFullscreen) {
            previewFullscreen = false
            // KDE Plasma doesn't handle Fullscreen -> Maximized for some reason
            visibility = _preFullscreenVisibility // Fullscreen -> Windowed
            visibility = _preFullscreenVisibility // Windowed -> Maximized (if it was previously maximized)
        } else {
            _preFullscreenVisibility = visibility
            previewFullscreen = true
            visibility = Window.FullScreen
        }
    }

    Shortcut {
        sequence: "Esc"
        enabled: window.previewFullscreen
        onActivated: window.togglePreviewFullscreen()
    }

    // Which of the two panel arrangements the editor is in. The preference is
    // stored as "not chosen" / portrait / landscape rather than a single bool, so
    // the canvas can keep driving it until the user takes the decision away — the
    // effective value is resolved here because it depends on both preference and
    // canvas, which are separate notifiers on the C++ side.
    readonly property string workspaceLayout: EditorState.workspaceLayoutOverridden
                                              ? EditorState.workspaceLayoutPreferred
                                              : (EditorState.projectPortrait ? "portrait" : "landscape")
    readonly property bool portraitWorkspace: workspaceLayout === "portrait"

    onPortraitWorkspaceChanged: {
        rootSplit.relocatePreview()
        // The two workspaces remember their own proportions, so the flip pulls in the
        // other one's — and any capture already in flight belongs to the old layout.
        if (window.panelLayoutRestored) {
            panelSettleTimer.stop()
            workspaceApplyTimer.restart()
        }
    }

    function configureAndAddAsset(assetIndex, runner) {
        if (!EditorState.shouldConfigureProjectForAsset(assetIndex)) {
            runner()
            return
        }
        projectSetupDialog.openForAsset(assetIndex, runner)
    }

    // "Decide later" closes the first-run chooser without settling on a canvas size.
    // Tracked separately from EditorState.projectLayoutChosen — which means "the canvas
    // size is decided" and gates ProjectSetupDialog — so the chooser can move on while
    // the first video/image clip still gets offered a setup step. Session-only,
    // matching projectLayoutChosen itself.
    property bool layoutPromptDismissed: false

    // Header Extras icon pulses while true; never auto-opens a dialog.
    readonly property alias addonAttentionNeeded: addonStartupDialog.needsAttention

    function promptLanguageChooserIfNeeded() {
        if (!EditorState.needsUiLanguagePrompt)
            return false
        if (languageChooserDialog.visible)
            return true
        languageChooserDialog.openChooser()
        return true
    }

    function promptLayoutChooserIfNeeded() {
        if (EditorState.needsUiLanguagePrompt || languageChooserDialog.visible)
            return
        if (EditorState.recoveryAvailable || EditorState.projectLayoutChosen)
            return
        if (window.layoutPromptDismissed)
            return
        if (layoutChooserDialog.visible || recoveryDialog.visible)
            return
        layoutChooserDialog.openChooser()
    }

    // Quiet catalog refresh for the header attention indicator.
    function refreshAddonAttention() {
        addonStartupDialog.refreshAttention()
    }

    // Settings / header: reopen platform layout picker anytime.
    function openLayoutChooser() {
        layoutChooserDialog.openFromSettings()
    }

    ProjectSetupDialog {
        id: projectSetupDialog
    }

    LanguageChooserDialog {
        id: languageChooserDialog
        // First-launch only. After Continue the language is stored, then the usual
        // recovery / layout prompts can run.
        onClosed: window.continueStartupAfterLanguage()
    }

    LayoutChooserDialog {
        id: layoutChooserDialog
        // rejected() fires before closed(), so the flag is already set by the time
        // callers check it.
        onFirstRunDismissed: window.layoutPromptDismissed = true
    }

    RecoveryDialog {
        id: recoveryDialog
        onClosed: Qt.callLater(window.promptLayoutChooserIfNeeded)
    }

    SubtitleProgressDialog {
        id: subtitleProgressDialog
    }

    ReverseProgressDialog {
        id: reverseProgressDialog
    }

    AddonManagerDialog {
        id: addonManagerDialog
    }

    AddonStartupDialog {
        id: addonStartupDialog
    }

    MissingAddonsDialog {
        id: missingAddonsDialog
    }

    UpdateDialog {
        id: updateDialog
    }

    DebugInfoDialog {
        id: debugInfoDialog
    }

    SegmentationWindow {
        id: segmentationWindow
    }

    Connections {
        target: EditorState
        function onOpenSegmentationWindowRequested(track, clip, startSeconds, durationSeconds) {
            segmentationWindow.openFor(track, clip, startSeconds, durationSeconds, true)
        }
    }

    // Another view of the same timeline rather than an editor of its own — see MulticamWindow.
    MulticamWindow {
        id: multicamWindow
    }

    DenoiseWindow {
        id: denoiseWindow
    }

    SpeedCurveWindow {
        id: speedCurveWindow
    }

    FadeCurveWindow {
        id: fadeCurveWindow
    }

    MediaPreviewWindow {
        id: mediaPreviewWindow
    }

    // Opened from the clip inspector; a window rather than a dialog so the timeline stays visible.
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

    function openMediaPreview(assetIndex) {
        mediaPreviewWindow.openFor(assetIndex)
    }

    // Opened from the header and from the "multicam" shortcut. Unlike the windows above it is
    // not bound to one clip, so it survives any edit and only closes when the document does.
    function openMulticam() {
        multicamWindow.openSession()
    }

    // Opened from the header, and from every empty state that a missing addon causes.
    function openAddonManager(kind) {
        if (kind === undefined)
            addonManagerDialog.open()
        else
            addonManagerDialog.openForKind(kind)
    }

    // Header Extras button: open the essential/update nudge when the icon is pulsing,
    // otherwise the full manager — same idea as the update badge vs silent check.
    function openExtras() {
        if (addonStartupDialog.openForAttention())
            return
        openAddonManager()
    }

    // Opened from the header badge, which only exists while there is something to show.
    function openUpdateDialog() {
        updateDialog.open()
    }

    function openDebugInfo() {
        debugInfoDialog.open()
    }

    function promptRecoveryIfNeeded() {
        if (EditorState.needsUiLanguagePrompt || languageChooserDialog.visible)
            return
        if (!EditorState.recoveryAvailable || recoveryDialog.visible)
            return
        // Opt-in reopen handles recovery (and last .drift) without asking.
        if (EditorState.reopenLastProject)
            return
        recoveryDialog.open()
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
            if (!EditorState.recoveryAvailable) {
                stop()
                attempts = 0
                Qt.callLater(window.promptLayoutChooserIfNeeded)
                return
            }
            promptRecoveryIfNeeded()
            if (recoveryDialog.visible || ++attempts >= 20)
                stop()
        }
    }

    Timer {
        id: layoutChooserOpenTimer
        interval: 120
        repeat: false
        onTriggered: window.promptLayoutChooserIfNeeded()
    }

    Component.onCompleted: {
        window.restoreWindowGeometry()
        window.beginStartupProject()
        // Last: the window is placed by now, and the startup flow keeps the ordering
        // it had when the window was shown at the end of completion.
        window.showRestored()
    }

    function beginStartupProject() {
        if (window.promptLanguageChooserIfNeeded())
            return
        window.continueStartupAfterLanguage()
    }

    function continueStartupAfterLanguage() {
        // A document the shell asked us to open (argv / QFileOpenEvent) wins over last-session
        // reopen and the recovery prompt.
        if (EditorState.consumeStartupProject())
            return
        // Opt-in: restore unsaved recovery or the last clean project silently.
        if (EditorState.restoreLastSessionIfEnabled())
            return
        if (EditorState.recoveryAvailable)
            recoveryOpenTimer.start()
        else
            layoutChooserOpenTimer.start()
    }

    onVisibilityChanged: {
        // Maximizing does not have to change the window size (a window already filling
        // the work area does not), so the geometry sampler alone can miss the switch.
        window.persistWindowState()
        if (visible && EditorState.recoveryAvailable && !EditorState.reopenLastProject)
            recoveryOpenTimer.start()
    }

    Connections {
        target: EditorState
        function onExternalProjectOpenRequested(url) {
            editorHeader.confirmIfDirty(function () {
                EditorState.loadProject(url)
            })
        }
        function onRecoveryChanged() {
            if (EditorState.needsUiLanguagePrompt || languageChooserDialog.visible)
                return
            if (EditorState.reopenLastProject)
                return
            if (EditorState.recoveryAvailable) {
                recoveryOpenTimer.start()
            } else {
                Qt.callLater(window.promptLayoutChooserIfNeeded)
            }
        }

        // Each of these edits one clip of the project that was just discarded, so there is
        // nothing left for them to act on. The C++ sessions are already torn down; onClosing
        // calls the same end*Session() again, which no-ops.
        // Multicam addresses tracks by index, so it has nothing to act on either once the
        // document behind those indices is gone.
        function onProjectReset() {
            segmentationWindow.close()
            denoiseWindow.close()
            speedCurveWindow.close()
            fadeCurveWindow.close()
            multicamWindow.close()
        }

        function onOpenMulticamWindowRequested() {
            multicamWindow.openSession()
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
        function onExportFinished(success) {
            if (success) {
                Toasts.success(qsTr("Export finished."))
                return
            }
            // Exporter reports cancellation as a failed run with this message —
            // surface it as a neutral note, not a disk/location error.
            if (EditorState.lastMessage === "Export cancelled")
                Toasts.info(qsTr("Export cancelled."))
            else
                Toasts.error(qsTr("Export failed. Check the save location and free space on your disk."))
        }

        function onMissingAddons(addons) {
            missingAddonsDialog.openFor(addons)
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

    // Shortcut is not an Item, so wrap each binding in a zero-size host.
    // Escape (clearSelection): CapCut-style — if a timeline cut tool is active,
    // first press returns to Select; only then does Escape clear the selection.
    Repeater {
        model: EditorState.actions
        Item {
            required property var modelData
            width: 0
            height: 0
            Shortcut {
                sequence: Theme.nativeShortcutSequence(modelData.shortcut)
                context: Qt.ApplicationShortcut
                onActivated: {
                    if (modelData.id === "clearSelection"
                            && timelinePanel.visible
                            && timelinePanel.timelineTool !== "") {
                        timelinePanel.timelineTool = ""
                        return
                    }
                    // Tool modes are QML state, so they are dispatched here rather
                    // than by triggerAction.
                    if (modelData.id === "selectTool") {
                        timelinePanel.timelineTool = ""
                        return
                    }
                    if (modelData.id === "bladeTool") {
                        timelinePanel.timelineTool = "split"
                        return
                    }
                    EditorState.triggerAction(modelData.id)
                }
            }
        }
    }

    // There is no menu bar and no help overlay, so the only route to the shortcut
    // list was an unlabelled icon in a vertical rail that can itself be scrolled out
    // of view on a short window. F1 is the conventional way in and reuses the tab
    // that already exists.
    Shortcut {
        sequence: "F1"
        context: Qt.ApplicationShortcut
        onActivated: {
            if (window.previewFullscreen)
                window.togglePreviewFullscreen()
            assetsPanel.showTab("shortcuts")
        }
    }

    Column {
        anchors.fill: parent
        spacing: 0

        EditorHeader {
            id: editorHeader
            width: parent.width
            visible: !window.previewFullscreen
        }

        Item {
            width: parent.width
            // Clamped so the editor body never takes a negative height while the
            // window is being resized toward its minimum.
            height: Math.max(0, parent.height
                                - (window.previewFullscreen ? 0 : Theme.headerHeight))

            // Two workspaces (issue #46). Landscape is the classic three-pane row
            // above the timeline. Portrait lifts the preview out into a full-height
            // column on the right, so a 9:16 frame is bounded by the window height
            // instead of by the height of the top row — where it renders
            // postage-stamp small. Only the preview moves between the two; assets,
            // properties and the timeline keep their slots either way.
            SplitView {
                id: rootSplit
                anchors.fill: parent
                // Edge-to-edge in fullscreen; the usual page padding otherwise.
                anchors.margins: window.previewFullscreen ? 0 : Theme.pagePadding
                anchors.topMargin: 0
                orientation: Qt.Horizontal
                spacing: Theme.panelGap

                handle: PanelSplitHandle { view: rootSplit }

                // Container.removeItem() *destroys* what it removes — takeItem() is
                // the one that hands the item back. That distinction matters here:
                // the preview owns the video sink and the GL runtime, so rebuilding
                // it (via removeItem, or a Loader) would tear down the renderer and
                // drop playback state on every workspace switch. Moving the one live
                // instance keeps the switch free.
                function relocatePreview() {
                    const wantRoot = window.portraitWorkspace
                    if (wantRoot === (previewPanel.SplitView.view === rootSplit))
                        return
                    const from = wantRoot ? innerSplit : rootSplit
                    for (let i = 0; i < from.count; ++i) {
                        if (from.itemAt(i) === previewPanel) {
                            from.takeItem(i)
                            break
                        }
                    }
                    // Index 1 either way: after the assets panel in the landscape
                    // row, after the editing stack in the portrait column.
                    if (wantRoot)
                        rootSplit.insertItem(1, previewPanel)
                    else
                        innerSplit.insertItem(1, previewPanel)
                }

                // The preview is declared in its landscape slot below, so a session
                // that starts portrait needs one move once the tree exists. Also
                // covers a layout flip that lands before this point during startup —
                // relocatePreview() is a no-op when nothing has to move.
                Component.onCompleted: relocatePreview()

                // The stored panel sizes need real extents to be fractions of, and
                // these are 0 until the first layout pass lands.
                onWidthChanged: window.restorePanelSizesWhenReady()
                onHeightChanged: window.restorePanelSizesWhenReady()

                SplitView {
                    id: outerSplit
                    SplitView.fillWidth: true
                    orientation: Qt.Vertical
                    spacing: Theme.panelGap

                    handle: PanelSplitHandle { view: outerSplit }

                    // In the portrait workspace the preview is a sibling of this
                    // whole stack, so fullscreen has to collapse the stack itself —
                    // hiding only the panels inside it would leave the preview
                    // confined to its column. SplitView drops hidden items from the
                    // layout, so this hands the window to the preview.
                    visible: !(window.previewFullscreen && window.portraitWorkspace)

                    onHeightChanged: window.restorePanelSizesWhenReady()

                    SplitView {
                        id: innerSplit
                        // Sized against the enclosing SplitView by id. `parent` here
                        // is the SplitView's own content item, which makes these
                        // percentage constraints self-referential during a resize.
                        // In fullscreen the timeline is hidden, so the normal 30–85%
                        // band would leave a dead strip below the preview.
                        //
                        // Never set a fixed minimum larger than the current parent
                        // size: on the first layout pass width/height are 0, and
                        // Qt SplitView asserts when maximum < minimum (qBound).
                        SplitView.preferredHeight: window.previewFullscreen
                                                   ? outerSplit.height : outerSplit.height * 0.5
                        SplitView.minimumHeight: window.previewFullscreen
                                                 ? outerSplit.height
                                                 : Math.min(outerSplit.height, outerSplit.height * 0.3)
                        SplitView.maximumHeight: window.previewFullscreen
                                                 ? outerSplit.height
                                                 : Math.max(SplitView.minimumHeight, outerSplit.height * 0.85)
                        orientation: Qt.Horizontal
                        spacing: Theme.panelGap

                        handle: PanelSplitHandle { view: innerSplit }

                        onWidthChanged: window.restorePanelSizesWhenReady()
                        // This split's share of the vertical one is the timeline's
                        // height, seen from the other side.
                        onHeightChanged: {
                            window.restorePanelSizesWhenReady()
                            window.schedulePanelCapture()
                        }

                        AssetsPanel {
                            id: assetsPanel
                            visible: !window.previewFullscreen
                            // A quarter of the row alongside the preview. In portrait
                            // the preview is gone from this row and the properties
                            // panel — last, and the only one without a fill — soaks up
                            // everything left over, so an unchanged quarter here left
                            // the library at a third of the inspector's width. Half
                            // splits the row evenly between the two instead.
                            SplitView.preferredWidth: Math.max(0, innerSplit.width
                                                               * (window.portraitWorkspace ? 0.5 : 0.25))
                            // Cap mins to available width so 200+320+240 never exceeds a
                            // zero/partial first layout pass (Qt asserts max < min).
                            SplitView.minimumWidth: Math.min(200, Math.max(0, innerSplit.width * 0.2))
                            onWidthChanged: window.schedulePanelCapture()
                        }

                        // Declared in its landscape slot; rootSplit.relocatePreview()
                        // moves this same instance out to the portrait column.
                        PreviewPanel {
                            id: previewPanel
                            previewFullscreen: window.previewFullscreen
                            onFullscreenRequested: window.togglePreviewFullscreen()
                            // Landscape: the pane that absorbs the slack in the top
                            // row. Portrait: a sized column, with the editing stack
                            // taking the slack instead — except in fullscreen, where
                            // the stack is hidden and the preview is all there is.
                            SplitView.fillWidth: !window.portraitWorkspace || window.previewFullscreen
                            // Portrait default: wide enough for the tall frame once
                            // the panel's toolbar and transport rows are discounted,
                            // capped so the libraries and timeline keep a usable
                            // share. Ignored while fillWidth is set, and replaced
                            // outright once the user drags the handle — SplitView
                            // writes this attached property, which drops the binding.
                            SplitView.preferredWidth: Math.max(0, Math.min(rootSplit.width * 0.4,
                                                                           (rootSplit.height - 120) * 9 / 16))
                            SplitView.minimumWidth: window.portraitWorkspace
                                                    ? Math.min(260, Math.max(0, rootSplit.width * 0.2))
                                                    : Math.min(320, Math.max(0, innerSplit.width * 0.3))
                            onWidthChanged: window.schedulePanelCapture()
                        }

                        PropertiesPanel {
                            id: propertiesPanel
                            visible: !window.previewFullscreen
                            SplitView.preferredWidth: Math.max(0, innerSplit.width * 0.25)
                            SplitView.minimumWidth: Math.min(240, Math.max(0, innerSplit.width * 0.2))
                            // Empty-state browse CTAs jump the assets panel to the
                            // matching library tab.
                            onWidthChanged: window.schedulePanelCapture()
                            onBrowseEffectsRequested: assetsPanel.showTab("effects")
                            onBrowseAudioEffectsRequested: assetsPanel.showTab("sounds")
                        }
                    }

                    TimelinePanel {
                        id: timelinePanel
                        visible: !window.previewFullscreen
                        propertiesTab: propertiesPanel.currentTabId
                        SplitView.preferredHeight: Math.max(0, outerSplit.height * 0.5)
                        SplitView.minimumHeight: Math.min(140, Math.max(0, outerSplit.height * 0.2))
                    }
                }

                // The preview is inserted here, after the editing stack, while the
                // portrait workspace is active.
            }
        }
    }

    // Notification host — above all panels, so any message lands in one place.
    ToastHost { }
}
