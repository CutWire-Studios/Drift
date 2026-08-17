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
    visible: true
    title: "CutWire Drift"
    color: Theme.appBackground

    LayoutMirroring.enabled: Qt.application.layoutDirection === Qt.RightToLeft
    LayoutMirroring.childrenInherit: true

    // Set true after the unsaved prompt resolves so onClosing can finish quit.
    property bool forceClose: false

    onClosing: function (close) {
        // Opt-in reopen: skip Save/Don't Save — dirty work is snapshotted to the
        // recovery file on aboutToQuit without overwriting the user's .drift.
        if (window.forceClose || EditorState.reopenLastProject || !EditorState.hasUnsavedChanges)
            return
        close.accepted = false
        editorHeader.confirmIfDirty(function () {
            // Don't Save leaves dirty true; clear it so aboutToQuit does not
            // write a recovery the user just declined. Harmless after Save.
            EditorState.discardUnsavedChanges()
            window.forceClose = true
            window.close()
        })
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
            visibility = _preFullscreenVisibility
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

    onPortraitWorkspaceChanged: rootSplit.relocatePreview()

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

    function promptLayoutChooserIfNeeded() {
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

    SegmentationWindow {
        id: segmentationWindow
    }

    Connections {
        target: EditorState
        function onOpenSegmentationWindowRequested(track, clip, startSeconds, durationSeconds) {
            segmentationWindow.openFor(track, clip, startSeconds, durationSeconds, true)
        }
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

    function promptRecoveryIfNeeded() {
        if (!EditorState.recoveryAvailable || recoveryDialog.visible)
            return
        // Opt-in reopen handles recovery (and last .drift) without asking.
        if (EditorState.reopenLastProject)
            return
        recoveryDialog.open()
    }

    // Ask every launch while the previous session left an autosave snapshot
    // (unsaved work — whether the app crashed or was closed normally).
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
        // Opt-in: restore unsaved recovery or the last clean project silently.
        if (EditorState.restoreLastSessionIfEnabled())
            return
        if (EditorState.recoveryAvailable)
            recoveryOpenTimer.start()
        else
            layoutChooserOpenTimer.start()
    }

    onVisibilityChanged: {
        if (visible && EditorState.recoveryAvailable && !EditorState.reopenLastProject)
            recoveryOpenTimer.start()
    }

    Connections {
        target: EditorState
        function onRecoveryChanged() {
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
        function onProjectReset() {
            segmentationWindow.close()
            denoiseWindow.close()
            speedCurveWindow.close()
            fadeCurveWindow.close()
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
                sequence: modelData.shortcut
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
                        }

                        PropertiesPanel {
                            id: propertiesPanel
                            visible: !window.previewFullscreen
                            SplitView.preferredWidth: Math.max(0, innerSplit.width * 0.25)
                            SplitView.minimumWidth: Math.min(240, Math.max(0, innerSplit.width * 0.2))
                            // Empty-state browse CTAs jump the assets panel to the
                            // matching library tab.
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
