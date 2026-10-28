import QtQuick
import QtQuick.Window
import Drift
// CapCut-style phone preview: letterboxed canvas + compact transport.
// One finger belongs to the content — tap/drag/resize/rotate a clip through the
// transform overlay, or drag a crop edge. Two fingers belong to the view: pinch
// zooms about the centroid and moves the canvas with it. That split is what
// keeps the gestures from fighting; there is deliberately no one-finger pan.
Item {
    id: root

    // Driven by AndroidEditor, which owns the panes that hide around the preview.
    property bool fullscreen: false
    signal fullscreenToggleRequested()

    property bool optionsOpen: false

    // Typing on the clip itself: the compositor draws the text and caret, a hidden field takes input.
    readonly property alias textEditor: viewport.textEditor

    // PointerHandlers (clip DragHandler, view pinch) hit-test their parent's
    // bounds, not z-order. A sheet in Overlay is visually on top of this preview
    // but is not an ancestor of those handlers, so a press on a sheet button that
    // then moves — or a gesture that the sheet does not keep a grab on — was
    // taken over as a clip drag. overlayModalCount is the same latch the window
    // uses for its input guard; TouchDrag is not an exception here: a lift is
    // meant for the timeline, not for a transform handle.
    readonly property bool overlayBlocksPreview: {
        const w = ShellBridge.main
        return !!(w && w.overlayModalCount > 0)
    }

    readonly property real currentSeconds: EditorState.playheadSeconds
    readonly property real durationSeconds: EditorState.durationSeconds
    readonly property bool playing: EditorState.playing

    readonly property int projectFps: {
        void EditorState.tracksRevision
        const fps = EditorState.projectFile.projectFps()
        return fps > 0 ? fps : 30
    }

    function formatTimecode(seconds) {
        const fps = root.projectFps
        const totalFrames = Math.round(seconds * fps)
        const h = Math.floor(totalFrames / (fps * 3600))
        const m = Math.floor(totalFrames / (fps * 60)) % 60
        const s = Math.floor(totalFrames / fps) % 60
        const f = totalFrames % fps
        function pad(n) { return n.toString().padStart(2, "0") }
        return pad(h) + ":" + pad(m) + ":" + pad(s) + ":" + pad(f)
    }

    // Cap preferred height so a portrait project canvas cannot starve the timeline.
    // The editor SplitView may assign a taller/shorter explicit height when dragged.
    readonly property real maxPreviewBodyHeight: {
        const h = (parent && parent.height > 0) ? parent.height : 800
        return root.fullscreen ? h : h * Theme.androidPreviewMaxScreenFraction
    }

    implicitHeight: Math.min(maxPreviewBodyHeight, preferredBodyHeight)
                    + Theme.androidPreviewTransportHeight
    readonly property real preferredBodyHeight: {
        const aspect = viewport.aspect
        if (aspect <= 0 || width <= 0)
            return Math.min(maxPreviewBodyHeight, 200)
        return Math.min(maxPreviewBodyHeight, width / aspect)
    }

    // In fullscreen the top bar and the rail are gone, so the status bar, the
    // gesture pill and any cutout are this item's problem instead of theirs.
    Column {
        anchors.fill: parent
        // Read off root, not off this Column: insetting the Column would move it
        // clear of the unsafe area and zero the very margins that put it there.
        anchors.topMargin: root.fullscreen ? root.SafeArea.margins.top : 0
        anchors.bottomMargin: root.fullscreen ? root.SafeArea.margins.bottom : 0
        // The side insets are not a fullscreen-only concern: in landscape the nav
        // bar and the cutout sit beside the preview pane in the split too, and
        // zeroing them there clipped the canvas and its transform handles.
        anchors.leftMargin: root.SafeArea.margins.left
        anchors.rightMargin: root.SafeArea.margins.right

        Item {
            id: viewportOuter
            width: parent.width
            // Fill the height the splitter (or implicitHeight) allocated, minus the
            // strips below it.
            height: Math.max(0, parent.height - Theme.androidPreviewTransportHeight
                                - scrubBar.height)
            clip: true

            // The band around the canvas. Fixed dark rather than the page background,
            // which made it a white surround in light mode with the video floating in it.
            AndroidPreviewLetterbox {
                anchors.fill: parent
            }

            PreviewViewport {
                id: viewport
                anchors.fill: parent
                // The inset is also the gutter the transform grips overflow into
                // when a clip sits flush against a canvas edge — viewportOuter
                // clips, so a zero margin would shear the outer handles away.
                anchors.margins: Theme.spacing2xl
                pixelRatio: 1
                compactStatus: true

                toolHost.tools: ({
                    transform: transformTool,
                    mask: maskTool,
                    crop: cropTool
                })
                toolHost.companion: dropCompanion
                toolHost.companionTools: ["transform", "mask"]
                // Disables every DragHandler / TapHandler / MouseArea in the tools:
                // PointerHandler::wantsEvent walks isEnabled() on ancestors. Hiding
                // would also work, but the boxes should stay drawn under the scrim so
                // the project does not appear to jump.
                toolHost.enabled: !root.overlayBlocksPreview

                // Two-finger zoom + pan. On the viewport itself rather than on the
                // canvas so it keeps working once the canvas has been zoomed past the
                // edges, and so it outlives whichever tool is on top.
                PinchHandler {
                    id: viewPinch
                    target: null
                    enabled: !root.overlayBlocksPreview

                    property real lastScale: 1
                    property point lastCentroid
                    property bool atZoomLimit: false

                    // Both signals fire for the same event; consuming the deltas makes
                    // the second call a no-op instead of a double application.
                    function step() {
                        if (!active)
                            return
                        const c = centroid.position
                        if (activeScale !== lastScale && lastScale > 0) {
                            viewport.zoomAt(c.x, c.y, activeScale / lastScale)
                            lastScale = activeScale
                            const atLimit = viewport.userZoom === 0.25 || viewport.userZoom === 12.0
                            if (atLimit && !atZoomLimit)
                                Haptics.boundary()
                            atZoomLimit = atLimit
                        }
                        viewport.panX += c.x - lastCentroid.x
                        viewport.panY += c.y - lastCentroid.y
                        lastCentroid = c
                    }

                    onActiveChanged: {
                        atZoomLimit = false
                        if (!active) {
                            Haptics.drop()
                            return
                        }
                        lastScale = activeScale
                        lastCentroid = centroid.position
                        Haptics.pickUp()
                    }
                    onActiveScaleChanged: viewPinch.step()
                    onCentroidChanged: viewPinch.step()
                }

                // Composition guides. Which sets are active lives in the
                // Settings tab; this is the layer that draws them.
                GuideLayer {
                    parent: viewport.canvas
                    anchors.fill: parent
                }
            }
            AndroidPreviewOptionsStrip {
                id: optionsStrip
                anchors.top: parent.top
                anchors.left: parent.left
                anchors.right: parent.right
                z: 300
                optionsOpen: root.optionsOpen
                viewMoved: viewport.viewMoved
                userZoom: viewport.userZoom
                onResetViewRequested: viewport.resetView()
            }
        }

        // Scrub bar. Only in fullscreen: the timeline is the seek surface
        // everywhere else, and it is hidden in this mode.
        Item {
            id: scrubBar
            width: parent.width
            visible: root.fullscreen
            height: visible ? Theme.controlHeight : 0

            ThemedSlider {
                id: scrubSlider
                label: qsTr("Seek")
                anchors.verticalCenter: parent.verticalCenter
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.leftMargin: Theme.spacing2xl
                anchors.rightMargin: Theme.spacing2xl

                from: 0
                // Never collapse to a zero-width range: an empty project would
                // otherwise make the handle jump erratically.
                to: Math.max(0.001, root.durationSeconds)
                valueFormatter: function (v) { return root.formatTimecode(v) }

                onMoved: EditorState.playheadSeconds = value

                // Dragging assigns `value` directly, which would clobber a plain
                // binding to the playhead. Reasserting it only while released lets
                // playback drive the handle without fighting the drag.
                Binding on value {
                    when: !scrubSlider.pressed
                    value: root.currentSeconds
                }
            }
        }

        AndroidPreviewTransport {
            id: transport
            width: parent.width
            fullscreen: root.fullscreen
            optionsOpen: root.optionsOpen
            playing: root.playing
            currentSeconds: root.currentSeconds
            durationSeconds: root.durationSeconds
            formatTimecode: function(seconds) { return root.formatTimecode(seconds) }
            onFullscreenToggleRequested: root.fullscreenToggleRequested()
            onOptionsToggled: root.optionsOpen = !root.optionsOpen
        }
    }

    Component {
        id: transformTool
        AndroidTransformOverlay {
            onTextEditRequested: (track, clip) => viewport.textEditor.start(track, clip, false)
        }
    }

    Component {
        id: maskTool
        MaskOverlay { }
    }

    Component {
        id: cropTool
        AndroidCropOverlay {
            previewCanvas: viewport.canvas
        }
    }

    // Lifted asset cards can be dropped on the preview as well as on the timeline: overlays land
    // at the playhead where the finger lets go, effects and masks on the clip under it.
    Component {
        id: dropCompanion

        // A wrapper, because the host sizes what it loads to the whole viewport.
        Item {
            PreviewDropOverlay {
                id: previewDrop
                x: viewport.canvas.x
                y: viewport.canvas.y
                width: viewport.canvas.width
                height: viewport.canvas.height
                visible: EditorState.projectFile.projectWidth() > 0

                function local(sceneX, sceneY) {
                    return previewDrop.mapFromItem(null, sceneX, sceneY)
                }
                function touchDropContains(sceneX, sceneY) {
                    const p = local(sceneX, sceneY)
                    return previewDrop.containsLocal(p.x, p.y)
                }
                function updateTouchDrop(kind, payload, sceneX, sceneY) {
                    const p = local(sceneX, sceneY)
                    return previewDrop.hover(kind, payload, p.x, p.y)
                }
                function performTouchDrop(kind, payload, sceneX, sceneY) {
                    const p = local(sceneX, sceneY)
                    previewDrop.drop(kind, payload, TouchDrag.label, p.x, p.y)
                }
                function clearTouchDrop() {
                    previewDrop.clear()
                }

                Component.onCompleted: TouchDrag.registerTarget(previewDrop)
                Component.onDestruction: TouchDrag.unregisterTarget(previewDrop)
            }
        }
    }

    // Seeks and edits render from the engine itself (setPlayheadUs, notifyProjectEdited); asking
    // again here doubled every scrub step and copied the whole project each time.
    Connections {
        target: EditorState
        function onPlayingChanged() {
            if (!EditorState.playing)
                EditorState.playback.refreshFrame()
        }
    }

    Component.onCompleted: EditorState.playback.refreshFrame()
}
