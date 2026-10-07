import QtQuick
import QtQuick.Window
import Drift
import "components"
import "components/preview"

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

    // PointerHandlers (clip DragHandler, view pinch) hit-test their parent's
    // bounds, not z-order. A sheet in Overlay is visually on top of this preview
    // but is not an ancestor of those handlers, so a press on a sheet button that
    // then moves — or a gesture that the sheet does not keep a grab on — was
    // taken over as a clip drag. overlayModalCount is the same latch the window
    // uses for its input guard; TouchDrag is not an exception here: a lift is
    // meant for the timeline, not for a transform handle.
    readonly property bool overlayBlocksPreview: {
        const w = root.Window.window
        return !!(w && w.overlayModalCount > 0)
    }

    readonly property real currentSeconds: EditorState.playheadSeconds
    readonly property real durationSeconds: EditorState.durationSeconds
    readonly property bool playing: EditorState.playing

    readonly property int projectFps: {
        void EditorState.tracksRevision
        const fps = EditorState.projectFps()
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
            Rectangle {
                anchors.fill: parent
                color: Theme.previewLetterbox
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

                // Masks have no touch editor, so a selected mask keeps the clip grips.
                toolHost.tools: ({
                    transform: transformTool,
                    mask: transformTool,
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

            // View settings. A phone transport has room for the transport and very
            // little else, so the four playback/view chips live in a strip that the
            // sliders button raises over the canvas instead of competing for it.
            Rectangle {
                id: optionsStrip
                anchors.top: parent.top
                anchors.left: parent.left
                anchors.right: parent.right
                height: optionsFlow.height + Theme.spacingLg * 2
                color: Theme.scrimStrong
                z: 300
                visible: opacity > 0
                opacity: root.optionsOpen && !EditorState.preview.canvasCropMode ? 1 : 0

                Behavior on opacity {
                    NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
                }

                Flow {
                    id: optionsFlow
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: Theme.spacingLg
                    spacing: Theme.spacingMd

                    // The zoom readout, and the way back to 100% — the same pair
                    // desktop's toolbar offers. It replaces a Fit/Fill toggle, which
                    // only stretched the letterbox rect: PreviewItem aspect-fits the
                    // frame inside it either way, so Fill changed nothing you could see.
                    ThemedChip {
                        selected: viewport.viewMoved
                        text: Math.round(viewport.userZoom * 100) + "%"
                        onClicked: viewport.resetView()
                    }

                    ThemedChip {
                        // Same four the desktop quality combo offers, in the same
                        // order — Auto is the engine's default, and cycling a list
                        // that left it out mislabelled it as Full.
                        readonly property var values: ["full", "half", "quarter", "auto"]
                        readonly property var labels: [qsTr("Full"), qsTr("Half"),
                                                       qsTr("Quarter"), qsTr("Auto")]
                        readonly property int currentIndex:
                            Math.max(0, values.indexOf(EditorState.playback.previewQuality))
                        text: qsTr("Quality: %1").arg(labels[currentIndex])
                        onClicked: EditorState.playback.previewQuality =
                                   values[(currentIndex + 1) % values.length]
                    }

                    ThemedChip {
                        readonly property var values: [0.25, 0.5, 1.0, 1.5, 2.0, 4.0]
                        readonly property var labels: ["0.25×", "0.5×", "1×", "1.5×", "2×", "4×"]
                        readonly property int currentIndex:
                            Math.max(0, values.indexOf(EditorState.playback.playbackRate))
                        text: labels[currentIndex]
                        onClicked: EditorState.playback.playbackRate =
                                   values[(currentIndex + 1) % values.length]
                    }

                    ThemedChip {
                        // Populated from the engine, so the entries are the backends
                        // whose device actually opens on this phone. Hidden when that
                        // leaves nothing to choose between.
                        readonly property var modes: EditorState.playback.decodeModes
                        readonly property var values:
                            modes.map(function (m) { return m.id })
                        readonly property int currentIndex:
                            Math.max(0, values.indexOf(EditorState.playback.decodeMode))
                        visible: modes.length > 1
                        text: modes[currentIndex].label
                        onClicked: EditorState.playback.decodeMode =
                                   values[(currentIndex + 1) % values.length]
                    }

                    ThemedChip {
                        selected: EditorState.preview.guidesEnabled
                        text: qsTr("Guides")
                        onClicked: EditorState.preview.guidesEnabled = !EditorState.preview.guidesEnabled
                    }
                }
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

        Item {
            id: transport
            width: parent.width
            height: Theme.androidPreviewTransportHeight
            // Backstop: in a narrow landscape pane the centred transport can extend past
            // both edges, and nothing above this sets clip.
            clip: true

            // Width the full five-button row needs, plus the margins either side of it.
            readonly property real fullTransportWidth:
                5 * Theme.androidIconButtonSize + 4 * Theme.spacingXs + 2 * Theme.spacingMd
            // Same for the three buttons that never fold away.
            readonly property real coreTransportWidth:
                3 * Theme.androidIconButtonSize + 2 * Theme.spacingXs + 2 * Theme.spacingMd
            // The view buttons are pinned right while the transport is centred, so the
            // row has to leave that much clear on *both* sides to stay off them.
            readonly property real viewButtonsClearance:
                2 * (2 * Theme.androidIconButtonSize + Theme.spacingXs + Theme.spacingMd)
            // Fullscreen and the view/playback options exist nowhere else in the phone
            // shell, so they claim their space before the ±1s skips do — a clearance
            // test that ran the other way round hid them on every 360-400px phone.
            readonly property bool showViewButtons:
                width >= coreTransportWidth + viewButtonsClearance
            // The ±1s skips are the first thing to go: stepping and play/pause cannot be
            // reached any other way, and jumping has the timeline scrubber as a fallback.
            readonly property bool showSkips:
                width >= fullTransportWidth + (showViewButtons ? viewButtonsClearance : 0)

            // Transport first, timecode second: on a phone the centre of the strip is the
            // easiest place to hit, and the readout only has to stay legible.
            Row {
                id: transportRow
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.verticalCenter: parent.verticalCenter
                spacing: Theme.spacingXs

                IconButton {
                    anchors.verticalCenter: parent.verticalCenter
                    buttonSize: Theme.androidIconButtonSize
                    iconSize: Theme.iconSizeBase
                    glyph: Theme.icons.rewind
                    variant: "text"
                    visible: transport.showSkips
                    width: visible ? buttonSize : 0
                    tooltip: qsTr("Back 1 second")
                    onClicked: EditorState.jumpSeconds(-1)
                }

                IconButton {
                    anchors.verticalCenter: parent.verticalCenter
                    buttonSize: Theme.androidIconButtonSize
                    iconSize: Theme.iconSizeBase
                    glyph: Theme.icons.stepBack
                    variant: "text"
                    tooltip: qsTr("Previous frame")
                    onClicked: EditorState.stepFrames(-1)
                }

                IconButton {
                    anchors.verticalCenter: parent.verticalCenter
                    buttonSize: Theme.androidIconButtonSize
                    iconSize: Theme.iconSizeLg
                    glyph: root.playing ? Theme.icons.pause : Theme.icons.play
                    variant: "text"
                    tooltip: root.playing ? qsTr("Pause") : qsTr("Play")
                    onClicked: EditorState.togglePlayback()
                }

                IconButton {
                    anchors.verticalCenter: parent.verticalCenter
                    buttonSize: Theme.androidIconButtonSize
                    iconSize: Theme.iconSizeBase
                    glyph: Theme.icons.stepForward
                    variant: "text"
                    tooltip: qsTr("Next frame")
                    onClicked: EditorState.stepFrames(1)
                }

                IconButton {
                    anchors.verticalCenter: parent.verticalCenter
                    buttonSize: Theme.androidIconButtonSize
                    iconSize: Theme.iconSizeBase
                    glyph: Theme.icons.repeat
                    variant: "text"
                    tooltip: EditorState.loopWorkAreaEnabled
                             ? qsTr("Loop work area on — tap to turn off")
                             : qsTr("Loop work area off — tap to turn on")
                    active: EditorState.loopWorkAreaEnabled
                    enabled: EditorState.workAreaActive
                    onClicked: EditorState.toggleLoopWorkArea()
                }

                IconButton {
                    anchors.verticalCenter: parent.verticalCenter
                    buttonSize: Theme.androidIconButtonSize
                    iconSize: Theme.iconSizeBase
                    glyph: Theme.icons.fastForward
                    variant: "text"
                    visible: transport.showSkips
                    width: visible ? buttonSize : 0
                    tooltip: qsTr("Forward 1 second")
                    onClicked: EditorState.jumpSeconds(1)
                }
            }

            Text {
                id: timecodeLabel
                anchors.left: parent.left
                anchors.leftMargin: Theme.spacingMd
                anchors.right: transportRow.left
                anchors.rightMargin: Theme.spacingXs
                anchors.verticalCenter: parent.verticalCenter
                // The transport is centred and the view buttons are pinned right, so on
                // a narrow phone this is the strip's smallest column. Drop the total
                // rather than elide the current time, which is the half worth reading —
                // and drop the readout entirely once the column would go negative.
                readonly property string full: root.formatTimecode(root.currentSeconds)
                                               + " / " + root.formatTimecode(root.durationSeconds)
                readonly property string shortText: root.formatTimecode(root.currentSeconds)
                visible: width > 0
                text: width >= fullMetrics.width ? full : shortText
                color: Theme.mutedForeground
                font.family: Theme.monoFontFamily
                font.pixelSize: Theme.fontSizeTick
                elide: Text.ElideRight

                TextMetrics {
                    id: fullMetrics
                    font: timecodeLabel.font
                    text: timecodeLabel.full
                }
            }

            Row {
                id: viewButtons
                anchors.right: parent.right
                anchors.rightMargin: Theme.spacingMd
                anchors.verticalCenter: parent.verticalCenter
                spacing: Theme.spacingXs
                visible: transport.showViewButtons

                IconButton {
                    buttonSize: Theme.androidIconButtonSize
                    iconSize: Theme.iconSizeBase
                    glyph: Theme.icons.sliders
                    variant: "text"
                    active: root.optionsOpen
                    tooltip: qsTr("View and playback settings")
                    onClicked: root.optionsOpen = !root.optionsOpen
                }

                IconButton {
                    buttonSize: Theme.androidIconButtonSize
                    iconSize: Theme.iconSizeBase
                    glyph: root.fullscreen ? Theme.icons.minimize : Theme.icons.maximize
                    variant: "text"
                    active: root.fullscreen
                    tooltip: root.fullscreen ? qsTr("Exit fullscreen preview")
                                             : qsTr("Fullscreen preview")
                    // The panes that hide around the preview belong to AndroidEditor,
                    // so the toggle is requested rather than performed here.
                    onClicked: root.fullscreenToggleRequested()
                }
            }
        }
    }

    Component {
        id: transformTool
        AndroidTransformOverlay { }
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
                visible: EditorState.projectWidth() > 0

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
