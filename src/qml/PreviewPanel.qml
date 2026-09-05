import QtQuick
// .Basic, matching every other file. Plain QtQuick.Controls pulled in the
// platform style, so the inline text editor below was styled differently from
// the rest of the app.
import QtQuick.Controls.Basic
// Window was used (fullscreen toggle) without being imported.
import QtQuick.Window
import QtQuick.Layouts
import Drift
import "components"
import "components/preview"

PanelFrame {
    id: root

    readonly property real currentSeconds: EditorState.playheadSeconds
    readonly property real durationSeconds: EditorState.durationSeconds
    readonly property bool playing: EditorState.playing

    // Driven by Main, which owns the window and the panels that hide around it.
    property bool previewFullscreen: false
    signal fullscreenRequested()

    // Frame rate of the project, not a fixed 30 — the timecode readout showed
    // wrong frame numbers for every project that was not 30fps.
    readonly property int projectFps: {
        void EditorState.tracks
        const fps = EditorState.projectFps()
        return fps > 0 ? fps : 30
    }

    function formatTimecode(seconds) {
        const fps = root.projectFps;
        const totalFrames = Math.round(seconds * fps);
        const h = Math.floor(totalFrames / (fps * 3600));
        const m = Math.floor(totalFrames / (fps * 60)) % 60;
        const s = Math.floor(totalFrames / fps) % 60;
        const f = totalFrames % fps;
        function pad(n) { return n.toString().padStart(2, "0"); }
        return pad(h) + ":" + pad(m) + ":" + pad(s) + ":" + pad(f);
    }

    Column {
        anchors.fill: parent

        Item {
            id: viewportOuter
            width: parent.width
            height: parent.height - toolbar.height - scrubBar.height
            clip: true

            // `transformBlocked` is now handled centrally in Main.qml and shown
            // through the app-wide toast host, so the same block raised by a
            // timeline drag is reported too. This panel-local toast is gone.

            Item {
                id: viewport
                anchors.fill: parent
                // The inset is also the gutter the transform grips overflow into
                // when a clip sits flush against a canvas edge — viewportOuter
                // clips, so a zero margin would shear the bottom handles in half.
                anchors.margins: Theme.spacingLg

                property real aspect: {
                    void EditorState.tracks
                    const w = EditorState.projectWidth()
                    const h = EditorState.projectHeight()
                    return (w > 0 && h > 0) ? (w / h) : (16 / 9)
                }
                // Crop mode pulls the canvas in so there is room around it to drag
                // an edge outward and grow the frame.
                property real cropZoom: EditorState.canvasCropMode ? 0.72 : 1.0
                // View navigation: wheel zoom about the cursor, middle-drag pan.
                // Both reset when crop mode starts or ends, so neither view is
                // ever entered already scrolled off-centre.
                property real userZoom: 1.0
                property real panX: 0
                property real panY: 0

                readonly property real baseWidth: Math.min(width, height * aspect)
                readonly property real baseHeight: baseWidth / aspect
                property real fitWidth: baseWidth * cropZoom * userZoom
                property real fitHeight: baseHeight * cropZoom * userZoom

                function resetView() {
                    userZoom = 1.0
                    panX = 0
                    panY = 0
                }

                // Scales about (mx, my) in viewport coords: the point under the
                // cursor keeps its position, so zooming into a crop corner keeps
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

                // Zoom and pan for normal preview. Declared first so it sits
                // under the canvas and the transform grips, and takes only the
                // middle button, so left-drags still reach the clip handles.
                // In crop mode CropOverlay (z: 200) has the same gestures and
                // takes them first.
                MouseArea {
                    anchors.fill: parent
                    acceptedButtons: Qt.MiddleButton
                    cursorShape: pressed ? Qt.ClosedHandCursor : Qt.ArrowCursor

                    property real lastX: 0
                    property real lastY: 0

                    onPressed: (mouse) => {
                        lastX = mouse.x
                        lastY = mouse.y
                    }
                    onPositionChanged: (mouse) => {
                        if (!pressed)
                            return
                        viewport.panX += mouse.x - lastX
                        viewport.panY += mouse.y - lastY
                        lastX = mouse.x
                        lastY = mouse.y
                    }

                    // Ctrl-less scrolls are explicitly rejected so they keep
                    // propagating: a MouseArea accepts wheel events even with
                    // no onWheel bound.
                    onWheel: (wheel) => {
                        if (!(wheel.modifiers & Qt.ControlModifier)
                                || wheel.angleDelta.y === 0) {
                            wheel.accepted = false
                            return
                        }
                        viewport.zoomAt(wheel.x, wheel.y,
                                        wheel.angleDelta.y > 0 ? 1.15 : 1 / 1.15)
                        wheel.accepted = true
                    }
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

                        // Canvas size is derived from this, so it has to be real
                        // screen pixels: item geometry is in logical units, and
                        // on a scaled display a canvas built from those is upscaled
                        // by the ratio before it ever reaches the screen.
                        readonly property real pixelRatio: Screen.devicePixelRatio

                        function updateRenderSize() {
                            EditorState.playback.setPreviewRenderSize(Math.round(width * pixelRatio),
                                                                      Math.round(height * pixelRatio))
                        }

                        Component.onCompleted: updateRenderSize()
                        onWidthChanged: updateRenderSize()
                        onHeightChanged: updateRenderSize()
                        onPixelRatioChanged: updateRenderSize()
                    }

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

                    Connections {
                        target: EditorState.playback
                        function onCurrentFrameChanged() {
                            preview.textureSize = EditorState.playback.previewTextureSize
                            preview.textureId = EditorState.playback.previewTextureId
                        }
                    }

                    // On a brand-new project this — the largest, most central panel —
                    // said nothing at all, while the timeline below it explained what
                    // to do. The terse gap message below is right when a project has
                    // content and the playhead is simply over a gap; it is not an
                    // answer to "what do I do first".
                    EmptyState {
                        anchors.centerIn: parent
                        width: Math.min(parent.width - Theme.spacing3xl, 280)
                        visible: EditorState.tracks.length === 0
                        glyph: Theme.icons.film
                        title: qsTr("Nothing to preview yet")
                        // No CTA: importing and adding tracks both live in the panels
                        // either side, and this one should not compete with them.
                        hint: qsTr("Import media and drag it onto the timeline below to see it here.")
                    }

                    // A dead GPU compositor produces no frame at any playhead position,
                    // which for a long time read as "No clip at the current time" and sent
                    // people hunting through their timeline. Say what actually happened,
                    // and where the details are. Held back until the first probe answers,
                    // so a slow driver does not flash a failure during startup.
                    EmptyState {
                        anchors.centerIn: parent
                        width: Math.min(parent.width - Theme.spacing3xl, 280)
                        visible: EditorState.tracks.length > 0
                                 && EditorState.playback.gpuCompositorStatus !== "unknown"
                                 && !EditorState.playback.gpuCompositorReady
                        glyph: Theme.icons.warning
                        title: qsTr("GPU preview unavailable")
                        hint: EditorState.playback.gpuCompositorStatus === "version-too-low"
                              && EditorState.playback.gpuCompositorDetail
                              ? qsTr("Your graphics driver only provides %1. Drift's preview needs OpenGL 3.3.")
                                    .arg(EditorState.playback.gpuCompositorDetail)
                              : qsTr("Drift could not start its GPU renderer, so the preview cannot draw.")
                        actionText: qsTr("Debug info")
                        onActionTriggered: root.Window.window.openDebugInfo()
                    }

                    // Fades rather than popping, so scrubbing across a gap no
                    // longer flickers this text on and off.
                    Text {
                        anchors.centerIn: parent
                        visible: opacity > 0
                        // Only ever a gap message now: when the compositor is down the
                        // state above explains that instead.
                        opacity: EditorState.playback.hasFrame
                                 || EditorState.tracks.length === 0
                                 || !EditorState.playback.gpuCompositorReady ? 0 : 1
                        text: EditorState.activeAudioClipAtPlayhead().path
                              ? qsTr("Audio only") : qsTr("No clip at the current time")
                        // Drawn on the letterbox scrim, not a panel surface, so it
                        // follows the on-media tokens in both themes.
                        color: Theme.guideMedium
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSizeSm

                        Behavior on opacity {
                            NumberAnimation { duration: Theme.durationBase; easing.type: Theme.easing }
                        }
                    }
                }

                TransformOverlay {
                    id: transformOverlay
                    // Sits outside the (clipped) canvas rect, mirroring its
                    // geometry, so resize and rotate grips on a clip that runs
                    // past a canvas edge stay drawn and grabbable instead of
                    // being cut away with the frame.
                    x: canvasRect.x
                    y: canvasRect.y
                    width: canvasRect.width
                    height: canvasRect.height
                    z: 100
                    visible: !root.playing && EditorState.projectWidth() > 0
                             && !EditorState.canvasCropMode
                }

                // Canvas crop tool. Lives outside the (clipped) canvas rect so the
                // crop frame can be dragged past the current edges to grow the
                // output. Values are kept in project pixels; committing hands the
                // rect to AppController, which rebases clip layout so nothing
                // moves or rescales — content outside the new frame is simply lost.
                CropOverlay {
                    id: cropOverlay
                    anchors.fill: parent
                    visible: EditorState.canvasCropMode
                    enabled: visible
                    z: 200
                    previewViewport: viewport
                    previewCanvas: canvasRect
                }
            }
        }

        // Scrub bar. Only in fullscreen: the timeline panel is the seek surface
        // everywhere else, and it is hidden in this mode.
        Item {
            id: scrubBar
            width: parent.width
            visible: root.previewFullscreen
            height: visible ? Theme.controlHeight : 0

            ThemedSlider {
                id: scrubSlider
                label: qsTr("Seek")
                anchors.verticalCenter: parent.verticalCenter
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.leftMargin: Theme.spacing2xl + Theme.spacingSm
                anchors.rightMargin: Theme.spacing2xl + Theme.spacingSm

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

        PreviewToolbar {
            id: toolbar
            panel: root
            previewViewport: viewport
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
