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
        void EditorState.tracks
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

            Item {
                id: viewport
                anchors.fill: parent
                // The inset is also the gutter the transform grips overflow into
                // when a clip sits flush against a canvas edge — viewportOuter
                // clips, so a zero margin would shear the outer handles away.
                anchors.margins: Theme.spacing2xl

                property real aspect: {
                    void EditorState.tracks
                    const w = EditorState.projectWidth()
                    const h = EditorState.projectHeight()
                    return (w > 0 && h > 0) ? (w / h) : (16 / 9)
                }
                property bool fitMode: true
                // Crop mode pulls the canvas in so there is room around it to drag
                // an edge outward and grow the frame.
                property real cropZoom: EditorState.canvasCropMode ? 0.72 : 1.0
                property real userZoom: 1.0
                property real panX: 0
                property real panY: 0

                readonly property real baseWidth: fitMode ? Math.min(width, height * aspect) : width
                readonly property real baseHeight: fitMode ? baseWidth / aspect : height
                readonly property real fitWidth: baseWidth * cropZoom * userZoom
                readonly property real fitHeight: baseHeight * cropZoom * userZoom

                readonly property bool viewMoved: userZoom !== 1.0 || panX !== 0 || panY !== 0

                function resetView() {
                    userZoom = 1.0
                    panX = 0
                    panY = 0
                }

                // Scales about (mx, my) in viewport coords: the point under the
                // pinch centroid keeps its position, so zooming into a corner keeps
                // that corner in place instead of drifting off screen.
                function zoomAt(mx, my, factor) {
                    const next = Math.max(0.25, Math.min(12.0, userZoom * factor))
                    if (next === userZoom)
                        return
                    const w = fitWidth
                    const h = fitHeight
                    const fx = w > 0 ? (mx - ((width - w) / 2 + panX)) / w : 0.5
                    const fy = h > 0 ? (my - ((height - h) / 2 + panY)) / h : 0.5
                    const nw = baseWidth * cropZoom * next
                    const nh = baseHeight * cropZoom * next
                    userZoom = next
                    panX = (mx - fx * nw) - (width - nw) / 2
                    panY = (my - fy * nh) - (height - nh) / 2
                }

                Behavior on cropZoom {
                    NumberAnimation { duration: Theme.durationBase; easing.type: Theme.easingInOut }
                }

                // Two-finger zoom + pan. Declared on the viewport rather than on the
                // canvas so it keeps working once the canvas has been zoomed past the
                // edges, and so it outlives whichever overlay is on top.
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

                Rectangle {
                    id: canvasRect
                    width: viewport.fitWidth
                    height: viewport.fitHeight
                    x: (viewport.width - width) / 2 + viewport.panX
                    y: (viewport.height - height) / 2 + viewport.panY
                    color: Theme.overlayColor
                    border.width: Theme.borderWidth
                    border.color: Theme.border
                    clip: true

                    PreviewItem {
                        id: preview
                        anchors.fill: parent
                        playback: EditorState.playback

                        function updateRenderSize() {
                            EditorState.playback.setPreviewRenderSize(
                                Math.round(width), Math.round(height))
                        }

                        Component.onCompleted: updateRenderSize()
                        onWidthChanged: updateRenderSize()
                        onHeightChanged: updateRenderSize()
                    }

                    // Composition guides. The switch and the type live in the
                    // Settings tab; this is the layer that reads them.
                    Item {
                        anchors.fill: parent
                        visible: EditorState.guidesEnabled

                        Repeater {
                            model: EditorState.guideType === "thirds" ? 2 : 0
                            Rectangle {
                                width: 1
                                height: parent.height
                                x: parent.width * (index + 1) / 3
                                color: Theme.guideMedium
                            }
                        }
                        Repeater {
                            model: EditorState.guideType === "thirds" ? 2 : 0
                            Rectangle {
                                height: 1
                                width: parent.width
                                y: parent.height * (index + 1) / 3
                                color: Theme.guideMedium
                            }
                        }

                        Rectangle {
                            visible: EditorState.guideType === "crosshair"
                            width: 1
                            height: parent.height
                            x: parent.width / 2
                            color: Theme.guideMedium
                        }
                        Rectangle {
                            visible: EditorState.guideType === "crosshair"
                            height: 1
                            width: parent.width
                            y: parent.height / 2
                            color: Theme.guideMedium
                        }

                        Rectangle {
                            visible: EditorState.guideType === "safe"
                            x: parent.width * 0.05
                            y: parent.height * 0.05
                            width: parent.width * 0.90
                            height: parent.height * 0.90
                            color: "transparent"
                            border.width: 1
                            border.color: Theme.guideMedium
                        }
                        Rectangle {
                            visible: EditorState.guideType === "safe"
                            x: parent.width * 0.025
                            y: parent.height * 0.025
                            width: parent.width * 0.95
                            height: parent.height * 0.95
                            color: "transparent"
                            border.width: 1
                            border.color: Theme.guideWeak
                        }
                    }

                    Text {
                        anchors.centerIn: parent
                        visible: opacity > 0
                        // Only a gap message: a compositor that never came up shows
                        // the explanation below instead of blaming the timeline.
                        opacity: EditorState.playback.hasFrame
                                 || !EditorState.playback.gpuCompositorReady ? 0 : 1
                        text: EditorState.activeAudioClipAtPlayhead().path
                              ? qsTr("Audio only") : qsTr("No clip at the current time")
                        color: Theme.guideMedium
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSizeSm

                        Behavior on opacity {
                            NumberAnimation { duration: Theme.durationBase; easing.type: Theme.easing }
                        }
                    }

                    Text {
                        anchors.centerIn: parent
                        width: parent.width - Theme.spacingXl
                        visible: EditorState.playback.gpuCompositorStatus !== "unknown"
                                 && !EditorState.playback.gpuCompositorReady
                        text: qsTr("GPU preview unavailable")
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.Wrap
                        color: Theme.guideMedium
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSizeSm
                    }
                }

                AndroidTransformOverlay {
                    id: transformOverlay
                    // Sits outside the (clipped) canvas rect, mirroring its geometry,
                    // so resize and rotate grips on a clip that runs past a canvas
                    // edge stay drawn and grabbable instead of being cut away.
                    x: canvasRect.x
                    y: canvasRect.y
                    width: canvasRect.width
                    height: canvasRect.height
                    z: 100
                    visible: !root.playing && EditorState.projectWidth() > 0
                             && !EditorState.canvasCropMode
                    // Disables every DragHandler / TapHandler / MouseArea in the
                    // overlay: PointerHandler::wantsEvent walks isEnabled() on
                    // ancestors. Hiding would also work, but the boxes should stay
                    // drawn under the scrim so the project does not appear to jump.
                    enabled: !root.overlayBlocksPreview
                }

                AndroidCropOverlay {
                    id: cropOverlay
                    anchors.fill: parent
                    visible: EditorState.canvasCropMode
                    enabled: visible && !root.overlayBlocksPreview
                    z: 200
                    previewViewport: viewport
                    previewCanvas: canvasRect
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
                opacity: root.optionsOpen && !EditorState.canvasCropMode ? 1 : 0

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

                    ThemedChip {
                        // Doubles as the "back to fit" control: once the canvas has
                        // been pinched away from its resting position, getting it back
                        // is the only thing this button could usefully do.
                        selected: viewport.viewMoved || !viewport.fitMode
                        text: viewport.viewMoved
                              ? Math.round(viewport.userZoom * 100) + "%"
                              : (viewport.fitMode ? qsTr("Fit") : qsTr("Fill"))
                        onClicked: {
                            if (viewport.viewMoved)
                                viewport.resetView()
                            else
                                viewport.fitMode = !viewport.fitMode
                        }
                    }

                    ThemedChip {
                        readonly property var values: ["full", "half", "quarter"]
                        readonly property var labels: [qsTr("Full"), qsTr("Half"), qsTr("Quarter")]
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
                        // Quality mode steps one frame per completed render and never
                        // opens the audio sink, so there is no real-time rate for a
                        // speed to be a multiple of.
                        enabled: EditorState.playback.playbackMode !== "quality"
                        text: labels[currentIndex]
                        onClicked: EditorState.playback.playbackRate =
                                   values[(currentIndex + 1) % values.length]
                    }

                    ThemedChip {
                        readonly property var values: ["fast", "quality"]
                        readonly property var labels: [qsTr("Fast"), qsTr("Quality")]
                        readonly property int currentIndex:
                            Math.max(0, values.indexOf(EditorState.playback.playbackMode))
                        text: labels[currentIndex]
                        onClicked: EditorState.playback.playbackMode =
                                   values[(currentIndex + 1) % values.length]
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

    Connections {
        target: EditorState
        function onPlayheadSecondsChanged() {
            if (!EditorState.playing)
                EditorState.playback.refreshFrame()
        }
        function onTracksChanged() {
            EditorState.playback.refreshFrame()
        }
        function onPlayingChanged() {
            if (!EditorState.playing)
                EditorState.playback.refreshFrame()
        }
    }

    Component.onCompleted: EditorState.playback.refreshFrame()
}
