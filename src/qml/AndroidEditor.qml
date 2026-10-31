import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Templates as T
import QtQuick.Window
import Drift
// CapCut-style phone editor: top bar, resizable preview | tools+timeline, bottom rail.
Item {
    id: root

    signal backRequested()

    property string sheetKind: "" // "" | "assets"

    // How much of the window bottom a sheet that does not blank the editor is covering.
    // The canvas sheet is hosted by the window.
    readonly property real nonBlockingSheetHeight: Math.max(
        (ShellBridge.main?.nonBlockingSheetHeight ?? 0),
        composer.coveredHeight,
        textEditBar.coveredHeight)

    // Fullscreen preview: the timeline pane, top bar and rail step aside and
    // AndroidPreview takes the page. Readable from outside so AndroidMain's Back
    // dispatcher can be pointed at exitPreviewFullscreen().
    property bool previewFullscreen: false

    function exitPreviewFullscreen() {
        previewFullscreen = false
    }

    // A full-page tool is up. AndroidMain reads this so Back and the stack leave the editor alone.
    readonly property bool toolPageOpen: toolPage.isOpen

    // Opens the shared tool page with `source` (a Component, or a URL relative to
    // AndroidToolPage.qml) as its controls. opts: see AndroidToolPage.qml. Returns false when a
    // page is already open.
    function openToolPage(source, opts) {
        composer.close()
        preview.textEditor.commit()
        closeSheets()
        return toolPage.open(Object.assign({}, opts || {}, { source: source }))
    }

    // Full-page tools. Add an entry here when a page lands: `url` is relative to
    // components/android/AndroidToolPage.qml. addPage: the page adds its own clip, so no
    // selection is passed in. needsClip: act on the selected clip, else the one under the playhead.
    readonly property var toolSpecs: ({
        "filters": { url: "pages/AndroidFiltersPage.qml", title: qsTr("Effects"), editLabel: qsTr("Effects"),
                     showRangeBar: true, needsClip: true },
        "adjust": { url: "pages/AndroidAdjustPage.qml", title: qsTr("Adjust"), editLabel: qsTr("Adjust"),
                    showRangeBar: true, needsClip: true },
        "audio": { url: "pages/AndroidVolumePage.qml", title: qsTr("Volume"), editLabel: qsTr("Volume"),
                   showRangeBar: true, needsClip: true },
        "speed": { url: "pages/AndroidSpeedPage.qml", title: qsTr("Speed"), editLabel: qsTr("Speed"),
                   showRangeBar: true, needsClip: true },
        "transform": { url: "pages/AndroidTransformPage.qml", title: qsTr("Transform"), editLabel: qsTr("Transform"),
                       showRangeBar: true, needsClip: true },
        "crop": { url: "pages/AndroidCropPage.qml", title: qsTr("Crop"), editLabel: qsTr("Crop"),
                  showRangeBar: false, needsClip: false },
        "background": { url: "pages/AndroidBackgroundPage.qml", title: qsTr("Background"),
                        editLabel: qsTr("Background"), showRangeBar: false, needsClip: false },
        "sticker": { url: "pages/AndroidStickerPage.qml", title: qsTr("Sticker"),
                     editLabel: qsTr("Add sticker"), showRangeBar: true, addPage: true },
        "shape": { url: "pages/AndroidShapePage.qml", title: qsTr("Shape"),
                   editLabel: qsTr("Add shape"), showRangeBar: true, addPage: true },
        "text": { url: "pages/AndroidTextPage.qml", title: qsTr("Text"),
                  editLabel: qsTr("Add text"), showRangeBar: true, addPage: true },
        "textStyle": { url: "pages/AndroidTextStylePage.qml", title: qsTr("Text style"),
                       editLabel: qsTr("Text style"), showRangeBar: true, needsClip: true },
        "textSettings": { url: "pages/AndroidTextSettingsPage.qml", title: qsTr("Text settings"),
                          editLabel: qsTr("Text settings"), showRangeBar: false, needsClip: true },
        "textAnim": { url: "pages/AndroidTextAnimationPage.qml", title: qsTr("Text animation"),
                      editLabel: qsTr("Text animation"), showRangeBar: true, needsClip: true },
        "animation": { url: "pages/AndroidAnimationPage.qml", title: qsTr("Animation"),
                       editLabel: qsTr("Animation"), showRangeBar: true, needsClip: true },
        "blending": { url: "pages/AndroidBlendingPage.qml", title: qsTr("Blending"),
                      editLabel: qsTr("Blending"), showRangeBar: false, needsClip: true },
        "shapeStyle": { url: "pages/AndroidShapeStylePage.qml", title: qsTr("Shape style"),
                        editLabel: qsTr("Shape style"), showRangeBar: true, needsClip: true },
        "stabilize": { url: "pages/AndroidStabilizePage.qml", title: qsTr("Stabilize"),
                       editLabel: qsTr("Stabilize"), showRangeBar: true, needsClip: true },
        "motion": { url: "pages/AndroidMotionPage.qml", title: qsTr("Motion"),
                    editLabel: qsTr("Motion"), showRangeBar: true, needsClip: true },
        "model3d": { url: "pages/AndroidModel3dPage.qml", title: qsTr("3D Model"),
                     editLabel: qsTr("3D Model"), showRangeBar: true, needsClip: true },
        "pdf": { url: "pages/AndroidPdfPage.qml", title: qsTr("PDF"),
                 editLabel: qsTr("PDF"), showRangeBar: true, needsClip: true },
        "camera": { url: "pages/AndroidCameraPage.qml", title: qsTr("Camera"),
                    editLabel: qsTr("Camera"), showRangeBar: true, needsClip: true },
        "maskEdit": { url: "pages/AndroidMaskEditPage.qml", title: qsTr("Mask"),
                      editLabel: qsTr("Mask"), showRangeBar: true, needsClip: true },
        "audioFxEdit": { url: "pages/AndroidAudioEffectsPage.qml", title: qsTr("Audio FX"),
                         editLabel: qsTr("Audio FX"), showRangeBar: true, needsClip: true },
        "transition": { url: "pages/AndroidTransitionPage.qml", title: qsTr("Transition"),
                        editLabel: qsTr("Transition"), showRangeBar: false, needsClip: false },
        "subtitles": { url: "pages/AndroidSubtitlesPage.qml", title: qsTr("Subtitles"),
                       editLabel: qsTr("Subtitles"), showRangeBar: false, needsClip: true },
        "details": { url: "pages/AndroidDetailsPage.qml", title: qsTr("Details"),
                     editLabel: qsTr("Details"), showRangeBar: false, needsClip: true },
        "mask": { url: "pages/AndroidMaskPage.qml", title: qsTr("Mask"),
                  editLabel: qsTr("Add mask"), showRangeBar: true, addPage: true, needsClip: true }
    })

    // The selected clip, or the one under the playhead. False (with a hint) when there is none.
    function ensureClipSelected() {
        if (EditorState.selectedTrack < 0 || EditorState.selectedClip < 0)
            EditorState.selectClipUnderPlayhead()
        if (EditorState.selectedTrack >= 0 && EditorState.selectedClip >= 0)
            return true
        Toasts.info(qsTr("Tap a clip to edit it"))
        return false
    }

    function openTool(toolId, props) {
        const spec = root.toolSpecs[toolId]
        if (!spec)
            return false
        if (spec.needsClip && !root.ensureClipSelected())
            return false
        return root.openToolPage(spec.url, {
            title: spec.title,
            editLabel: spec.editLabel,
            showRangeBar: spec.showRangeBar,
            previewFraction: spec.addPage ? 0.4 : undefined,
            trackIndex: spec.addPage ? -1 : undefined,
            clipIndex: spec.addPage ? -1 : undefined,
            props: props
        })
    }

    function openExport(audioOnly) {
        composer.close()
        closeSheets()
        if (audioOnly)
            exportDialog.openAudioOnly()
        else
            exportDialog.openDialog()
    }

    readonly property var projectFilter: [qsTr("Drift project (*.drift)")]

    function openAssetsTab(tabId) {
        composer.close()
        preview.textEditor.commit()
        sheetKind = "assets"
        rail.activeId = tabId
        assetsPanel.showTab(tabId)
        assetsSheet.title = assetsPanel.tabLabel(tabId)
        if (!assetsSheet.opened)
            assetsSheet.open()
    }

    // The rail's [+]. Clears whatever sheet is up first: the add menu is a step on
    // the way to one of them, and stacking it over an open Media sheet would leave
    // two sheets to dismiss to get back to the timeline.
    function openAddMenu() {
        composer.close()
        preview.textEditor.commit()
        sheetKind = ""
        rail.activeId = ""
        if (assetsSheet.opened)
            assetsSheet.dismiss()
        if (!addMenu.opened)
            addMenu.open()
    }

    // The toolbar's fourth slot. Clears the browsers first: the sheet is a peer of them,
    // not something to stack on top.
    function openMoreTools() {
        composer.close()
        preview.textEditor.commit()
        if (assetsSheet.opened)
            assetsSheet.dismiss()
        if (fadeSheet.opened)
            fadeSheet.dismiss()
        if (!moreToolsSheet.opened)
            moreToolsSheet.open()
    }

    function openFadeSheet() {
        composer.close()
        preview.textEditor.commit()
        if (assetsSheet.opened)
            assetsSheet.dismiss()
        if (moreToolsSheet.opened)
            moreToolsSheet.dismiss()
        if (!fadeSheet.opened)
            fadeSheet.open()
    }

    function openEasingSheet(track, clip, prop, label, seconds) {
        composer.close()
        preview.textEditor.commit()
        if (assetsSheet.opened)
            assetsSheet.dismiss()
        if (moreToolsSheet.opened)
            moreToolsSheet.dismiss()
        if (fadeSheet.opened)
            fadeSheet.dismiss()
        easingSheet.openFor(track, clip, prop, label, seconds)
    }

    // Typing on a phone happens on the clip in the preview, with a slim bar above the keyboard:
    // the sheets are put away so they do not sit under it, and the preview shrinks into the
    // space the keyboard leaves.
    function startInlineTextEdit(track, clip, selectAllText) {
        root.closeSheets()
        composer.close()
        preview.textEditor.start(track, clip, selectAllText)
    }

    function openCueComposer(index) {
        root.closeSheets()
        composer.openForCue(index)
    }

    // A shared link, routed here by AndroidMain once the editor exists.
    function startLinkImport(url) {
        linkImportSheet.openFor(url)
    }

    function closeSheets() {
        sheetKind = ""
        rail.activeId = ""
        if (addMenu.opened)
            addMenu.dismiss()
        if (moreToolsSheet.opened)
            moreToolsSheet.dismiss()
        if (fadeSheet.opened)
            fadeSheet.dismiss()
        if (assetsSheet.opened)
            assetsSheet.dismiss()
    }

    // Android Back, delegated from AndroidMain. Returns true when it consumed the press,
    // so a sheet or dialog closes instead of the editor being popped out from under it.
    function handleBack() {
        if (exportProgressDialog.visible) {
            exportProgressDialog.close()
            return true
        }
        if (exportDialog.visible) {
            exportDialog.close()
            return true
        }
        if (packageProgressDialog.visible)
            return true // a package render is running; swallow rather than abandon it
        if (preview.textEditor.active) {
            preview.textEditor.commit()
            return true
        }
        if (composer.active) {
            composer.close()
            return true
        }
        if (linkImportSheet.opened) {
            linkImportSheet.dismiss()
            return true
        }
        if (projectSheet.opened) {
            projectSheet.dismiss()
            return true
        }
        if (moreToolsSheet.opened) {
            moreToolsSheet.dismiss()
            return true
        }
        if (fadeSheet.opened) {
            fadeSheet.dismiss()
            return true
        }
        if (addMenu.opened) {
            addMenu.dismiss()
            return true
        }
        if (assetsSheet.opened) {
            root.closeSheets()
            return true
        }
        if (EditorState.preview.canvasCropMode && !toolPage.isOpen) {
            EditorState.preview.canvasCropMode = false
            return true
        }
        if (root.previewFullscreen) {
            root.exitPreviewFullscreen()
            return true
        }
        if (toolPage.isOpen) {
            toolPage.cancel()
            return true
        }
        if (EditorState.activeSequenceId !== "") {
            EditorState.openSequence("")
            return true
        }
        return false
    }

    function saveProject() {
        return ShellBridge.main.saveProject()
    }

    // Save As: the open project written to a second file, which the session then continues in.
    // The file it came from is left as it was, which is the whole point — one project per
    // variation. Suggests "<name> copy" so the default cannot land back on the original.
    function saveProjectAs() {
        const url = FileDialogs.saveFile(qsTr("Save Project As"), root.projectFilter,
                                         qsTr("%1 copy").arg(EditorState.projectFile.projectName), "drift")
        if (url === "")
            return false
        EditorState.projectFile.saveProjectAs(url)
        return !EditorState.projectFile.hasUnsavedChanges
    }

    function packageProject() {
        const url = FileDialogs.saveFile(qsTr("Save Shareable Copy"), root.projectFilter,
                                         EditorState.projectFile.projectName, "drift")
        if (url !== "")
            EditorState.projectFile.packageProject(url)
    }

    // Reached from EditorState's own signals (desktop menus, launch intents), not from any
    // phone control: Open and New are tiles on Home -> Projects now.
    function openProject() {
        ShellBridge.main.confirmIfDirty(function () {
            const url = FileDialogs.openFile(qsTr("Open Project"), root.projectFilter)
            if (url !== "")
                EditorState.projectFile.loadProject(url)
        })
    }

    function requestNewProject() {
        ShellBridge.main.confirmIfDirty(function () {
            EditorState.projectFile.newProject()
            ShellBridge.main.showHome()
        })
    }

    // Everything the project title's sheet offers. Routed here rather than from the sheet
    // itself because half of it is the editor's own (export, save, package) and half the
    // window's, and the sheet should not have to know which is which.
    function runProjectAction(actionId) {
        switch (actionId) {
        case "export":
            if (EditorState.exportInProgress) {
                exportProgressDialog.dismissed = false
                exportProgressDialog.openDialog()
            } else {
                exportDialog.openDialog()
            }
            break
        case "save":
            root.saveProject()
            break
        case "saveAs":
            root.saveProjectAs()
            break
        case "package":
            root.packageProject()
            break
        case "layout":
            ShellBridge.main.openLayoutChooser()
            break
        case "crop":
            root.openTool("crop", { mode: "video" })
            break
        case "properties":
            ShellBridge.main.openProjectProperties()
            break
        case "multicam":
            ShellBridge.main.openMulticam()
            break
        case "extras":
            ShellBridge.main.openExtras()
            break
        case "settings":
            ShellBridge.main.openSettings()
            break
        case "theme":
            Theme.toggleDarkMode()
            break
        case "desktopLayout":
            ShellBridge.main.switchShell("desktop")
            break
        case "update":
            ShellBridge.main.openUpdateDialog()
            break
        case "debug":
            ShellBridge.main.openDebugInfo()
            break
        case "about":
            ShellBridge.main.openAboutDialog()
            break
        }
    }

    Column {
        anchors.fill: parent
        spacing: 0

        AndroidTopBar {
            id: topBar
            width: parent.width
            visible: !root.previewFullscreen
            onBackRequested: root.backRequested()
            onExportRequested: exportDialog.openDialog()
            onExportProgressRequested: {
                exportProgressDialog.dismissed = false
                exportProgressDialog.openDialog()
            }
            onProjectMenuRequested: projectSheet.open()
            onDownloadsRequested: ShellBridge.main.openMarketplace("downloads")
            onNotificationsRequested: notificationsSheet.open()
        }

        SplitView {
            id: editorSplit
            width: parent.width
            // Column skips hidden children but their `height` still reads non-zero,
            // so the two strips only count while they are on screen.
            height: Math.max(0, parent.height - (topBar.visible ? topBar.height : 0)
                                - (rail.visible ? rail.height : 0))
            // Landscape and multi-window leave roughly 260px of height, which a vertical
            // split cannot divide into a usable preview and a usable timeline. Side by
            // side it divides the axis there is room on instead.
            readonly property bool sideBySide: width > height * 1.2
            orientation: sideBySide ? Qt.Horizontal : Qt.Vertical

            // Both minimums used to clamp to the whole pane (Math.min(height, ...)), so
            // together they could demand more than the pane had and SplitView let the last
            // item overflow behind the rail. They share a budget now: scaled down in
            // proportion whenever the two wants exceed what is actually available.
            readonly property real budget: Math.max(
                0, (sideBySide ? width : height) - Theme.androidSplitterHeight)
            readonly property real wantPreviewMin: Theme.androidPreviewTransportHeight + 72
            readonly property real wantTimelineMin: Theme.androidClipToolbarHeight + 120
            // Side by side the axis being divided is width, and the two panes need very
            // different amounts of it: the preview's transport is a five-button row, the
            // timeline needs room for its track labels plus some visible seconds. Reusing
            // the vertical numbers let the preview shrink to 120px — narrower than its own
            // transport.
            readonly property real wantPreviewMinW: 220
            readonly property real wantTimelineMinW: Theme.androidTrackLabelsWidth + 160
            readonly property real minScale: {
                const want = sideBySide ? wantPreviewMinW + wantTimelineMinW
                                        : wantPreviewMin + wantTimelineMin
                return want > 0 ? Math.min(1, budget / want) : 1
            }
            // A non-blocking sheet covers the bottom of the window, the rail included; what
            // is left of this split above it is all the preview may occupy. Sliced at the
            // sheet edge a video frame reads as broken rather than as covered.
            readonly property real coveredBySheet: Math.max(
                0, root.nonBlockingSheetHeight - Theme.androidBottomRailHeight)
            readonly property real uncoveredHeight: Math.max(0, height - coveredBySheet)

            // Sized from the project aspect (preview.implicitHeight), then held inside a
            // band so the transport row's y does not move between the editor, crop mode and
            // a sheet being open. Below the floor the canvas letterboxes inside the pane
            // instead of the pane shrinking to meet it.
            readonly property real previewTarget: {
                const wanted = Math.min(preview.implicitHeight,
                                        height * Theme.androidPreviewMaxScreenFraction)
                const held = Math.max(previewMin,
                                      Math.max(height * Theme.androidPreviewMinFraction, wanted))
                return Math.min(held, uncoveredHeight)
            }

            readonly property real previewMin: wantPreviewMin * minScale
            readonly property real timelineMin: wantTimelineMin * minScale
            readonly property real previewMinW: wantPreviewMinW * minScale
            readonly property real timelineMinW: wantTimelineMinW * minScale

            handle: Item {
                implicitWidth: editorSplit.sideBySide ? Theme.androidSplitterHeight
                                                      : editorSplit.width
                implicitHeight: editorSplit.sideBySide ? editorSplit.height
                                                       : Theme.androidSplitterHeight

                Rectangle {
                    anchors.fill: parent
                    color: Theme.c.androidEditor.splitterBackground
                }

                Rectangle {
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.verticalCenter: parent.verticalCenter
                    // 4x40 was a hairline on a phone: the one control between two panes
                    // that both want to be bigger, and the hardest thing on the screen to
                    // land a thumb on.
                    width: editorSplit.sideBySide ? 6 : 48
                    height: editorSplit.sideBySide ? 48 : 6
                    radius: Theme.c.androidEditor.handleRadius
                    color: T.SplitHandle.pressed ? Theme.c.androidEditor.handleBackgroundPressed
                                                 : Theme.c.androidEditor.handleBackground
                }

                MouseArea {
                    anchors.fill: parent
                    acceptedButtons: Qt.NoButton
                    cursorShape: editorSplit.sideBySide ? Qt.SplitHCursor : Qt.SplitVCursor
                }
            }

            AndroidPreview {
                id: preview
                fullscreen: root.previewFullscreen
                onFullscreenToggleRequested: root.previewFullscreen = !root.previewFullscreen

                // With the timeline pane hidden the preview is the only item left, and
                // its ceiling would otherwise still hold it to 72% of the split.
                // Avoid fixed mins larger than the first layout pass (height may be 0).
                SplitView.preferredHeight: root.previewFullscreen
                    ? editorSplit.height
                    : editorSplit.previewTarget
                SplitView.minimumHeight: root.previewFullscreen ? 0 : editorSplit.previewMin
                SplitView.maximumHeight: root.previewFullscreen
                    ? editorSplit.height
                    : Math.max(editorSplit.previewMin, editorSplit.height * 0.72)
                // SplitView reads only the pair matching its current orientation, so both
                // sets are declared rather than swapped.
                SplitView.preferredWidth: root.previewFullscreen
                    ? editorSplit.width
                    : Math.max(editorSplit.previewMinW, editorSplit.width * 0.45)
                SplitView.minimumWidth: root.previewFullscreen ? 0 : editorSplit.previewMinW
                SplitView.maximumWidth: root.previewFullscreen
                    ? editorSplit.width
                    : Math.max(editorSplit.previewMinW, editorSplit.width * 0.65)
            }

            Item {
                id: timelinePane
                visible: !root.previewFullscreen
                SplitView.fillHeight: true
                SplitView.fillWidth: true
                SplitView.minimumHeight: editorSplit.timelineMin
                SplitView.minimumWidth: editorSplit.timelineMinW

                AndroidClipToolbar {
                    id: clipToolbar
                    anchors.top: parent.top
                    anchors.left: parent.left
                    anchors.right: parent.right
                    panel: timeline
                    onMoreRequested: root.openMoreTools()
                    onFadeRequested: root.openFadeSheet()
                    onEditTextRequested: root.startInlineTextEdit(EditorState.selectedTrack,
                                                                  EditorState.selectedClip, false)
                }

                Item {
                    id: timelineHost
                    anchors.top: clipToolbar.bottom
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom

                    AndroidTimeline {
                        id: timeline
                        anchors.fill: parent
                        onOpenMediaRequested: root.openAssetsTab("media")
                        onOpenPropertiesRequested: root.openTool("details")
                        onSubtitleCueTapped: index => root.openCueComposer(index)
                    }

                    Rectangle {
                        anchors.fill: parent
                        visible: {
                            // A lift from the asset sheet has to see the lanes it is
                            // aiming at. Covering them for the empty-state copy made
                            // the drop target invisible for the whole gesture.
                            if (TouchDrag.active)
                                return false
                            return EditorState.clipCount === 0
                        }
                        color: Qt.rgba(Theme.c.androidEditor.emptyBackground.r,
                                       Theme.c.androidEditor.emptyBackground.g,
                                       Theme.c.androidEditor.emptyBackground.b, 0.82)
                        z: 5

                        Column {
                            anchors.centerIn: parent
                            spacing: Theme.spacingLg
                            width: Math.min(260, parent.width - 32)

                            Text {
                                width: parent.width
                                horizontalAlignment: Text.AlignHCenter
                                text: qsTr("Your timeline is empty")
                                color: Theme.c.androidEditor.foreground
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontSizeBase
                                font.weight: Font.Medium
                            }

                            Text {
                                width: parent.width
                                horizontalAlignment: Text.AlignHCenter
                                wrapMode: Text.WordWrap
                                text: qsTr("Import media or open the Media library to start editing.")
                                color: Theme.c.androidEditor.foregroundMuted
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontSizeSm
                            }

                            ThemedButton {
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: qsTr("Open Media")
                                glyph: Theme.icons.film
                                variant: "primary"
                                onClicked: root.openAssetsTab("media")
                            }
                        }
                    }
                }
            }
        }

        AndroidBottomRail {
            id: rail
            width: parent.width
            visible: !root.previewFullscreen
            onTabRequested: (tabId) => root.openAssetsTab(tabId)
            onToolRequested: (toolId, props) => root.openTool(toolId, props)
            onEditTextRequested: root.startInlineTextEdit(EditorState.selectedTrack,
                                                          EditorState.selectedClip, false)
            onAddRequested: root.openAddMenu()
        }
    }

    AndroidToolPage {
        id: toolPage
        anchors.fill: parent
        previewItem: preview
        previewHome: editorSplit
        previewHomeBefore: timelinePane
    }

    AndroidAddMenu {
        id: addMenu
        // A transform layer is added straight away; everything else opens its assets tab.
        onPicked: (tabId) => {
            const addPages = { "stickers": "sticker", "shapes": "shape", "text": "text", "masks": "mask" }
            if (tabId === "transformLayer")
                EditorState.addTransformLayerForSelection()
            else if (addPages[tabId] !== undefined)
                root.openTool(addPages[tabId])
            else if (tabId === "market")
                ShellBridge.main.openMarketplace()
            else
                root.openAssetsTab(tabId)
        }
    }

    AndroidProjectSheet {
        id: projectSheet
        onPicked: (actionId) => root.runProjectAction(actionId)
    }

    AndroidNotificationsSheet {
        id: notificationsSheet
    }

    Connections {
        target: Toasts
        function onHistoryRequested() { notificationsSheet.open() }
    }

    Connections {
        target: ShellBridge
        function onEasingSheetRequested(track, clip, prop, label, seconds) {
            root.openEasingSheet(track, clip, prop, label, seconds)
        }
    }

    AndroidLinkImport {
        id: linkImportSheet
    }

    AndroidMoreToolsSheet {
        id: moreToolsSheet
        panel: timeline
    }

    AndroidFadeSheet {
        id: fadeSheet
    }

    AndroidEasingSheet {
        id: easingSheet
    }

    AndroidBottomSheet {
        id: assetsSheet
        onClosed: {
            if (sheetKind === "assets") {
                sheetKind = ""
                rail.activeId = ""
            }
        }

        AssetsPanel {
            id: assetsPanel
            anchors.fill: parent
            sheetMode: true
            // Text, subtitles, stickers and shapes land at the playhead the moment
            // they are tapped, so the sheet has done its job and is now covering
            // the result.
            onAddCompleted: root.closeSheets()
        }
    }

    AndroidTextComposer {
        id: composer
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.leftMargin: root.SafeArea.margins.left
        anchors.rightMargin: root.SafeArea.margins.right
        anchors.bottomMargin: Math.max(root.SafeArea.margins.bottom, Theme.keyboardInset)
        z: 101
    }

    // Only the buttons: the text is edited on the preview itself, which stays visible above this.
    AndroidTextEditBar {
        id: textEditBar
        editing: preview.textEditor.active
        textEditor: preview.textEditor
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.leftMargin: root.SafeArea.margins.left
        anchors.rightMargin: root.SafeArea.margins.right
        anchors.bottomMargin: Math.max(root.SafeArea.margins.bottom, Theme.keyboardInset)
        z: 101
    }

    ExportDialog {
        id: exportDialog
    }

    ExportProgressDialog {
        id: exportProgressDialog
        property bool dismissed: false
        onClosed: if (EditorState.exportInProgress) dismissed = true
    }

    PackageProgressDialog {
        id: packageProgressDialog
    }

    Connections {
        target: EditorState
        // A text clip added with no text is typed straight onto the preview; the assets sheet
        // it was added from is put away first.
        function onInlineTextEditRequested(trackIndex, clipIndex) {
            root.startInlineTextEdit(trackIndex, clipIndex, true)
        }
        function onExportInProgressChanged() {
            if (EditorState.exportInProgress) {
                exportProgressDialog.dismissed = false
                exportProgressDialog.openDialog()
            } else if (EditorState.exportByAgent) {
                // Same as the desktop header: an agent's export closes its own progress dialog.
                exportProgressDialog.close()
            }
        }
    }

    Connections {
        target: EditorState.projectFile

        // Selection must not auto-open the Edit sheet: that ran on press (before
        // release), stole the gesture so clips could not drag, and blocked long-press
        // menus. Open via the Edit rail; an already-open sheet still updates via
        // PropertiesPanel bindings.
        function onSaveRequested() { root.saveProject() }
        function onSaveAsRequested() { root.saveProjectAs() }
        function onOpenRequested() { root.openProject() }
        function onNewProjectRequested() { root.requestNewProject() }
    }

    // The crop frame is dragged on the preview, and the Settings tab it is
    // started from covers most of the screen. Cropping therefore takes the page
    // for as long as it is on, and hands it back when it ends.
    Connections {
        target: EditorState.preview
        function onCanvasCropModeChanged() {
            // The crop page hosts the preview itself; fullscreen would take it back.
            if (toolPage.isOpen)
                return
            if (EditorState.preview.canvasCropMode)
                root.closeSheets()
            root.previewFullscreen = EditorState.preview.canvasCropMode
        }
    }

    Component.onCompleted: AssetLibrary.ensureAllMedia()
}
