import QtQuick
import Drift
// The desktop preview: the shared viewport and tools, the on-screen controls that fade over it,
// and the transport bar underneath.
PanelFrame {
    id: root

    // The crop overlay's gesture hint, dismissed once per session rather than per crop.
    property bool cropHintDismissed: false

    HoverHandler {
        onHoveredChanged: ShellBridge.previewHovered = hovered
    }
    Component.onDestruction: ShellBridge.previewHovered = false

    readonly property real currentSeconds: EditorState.playheadSeconds
    readonly property real durationSeconds: EditorState.durationSeconds
    // Only the desktop shell hosts this panel (phones use AndroidPreview), so 3D mode is always
    // available here, including Android tablets and DeX.
    readonly property bool mode3d: EditorState.preview.mode === "3d"

    // Driven by Main, which owns the window and the panels that hide around it.
    property bool previewFullscreen: false
    signal fullscreenRequested()

    // Frame rate of the project, not a fixed 30 — the timecode readout showed
    // wrong frame numbers for every project that was not 30fps.
    readonly property int projectFps: {
        void EditorState.tracksRevision
        const fps = EditorState.projectFile.projectFps()
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

    // Fullscreen gives the whole screen to the picture: no gutter, and the transport joins the OSD
    // over it instead of taking a strip of its own.
    color: previewFullscreen ? Theme.c.previewPanel.backgroundFullscreen : Theme.c.previewPanel.background
    border.width: previewFullscreen ? Theme.c.previewPanel.borderWidthFullscreen : Theme.c.previewPanel.borderWidth
    radius: previewFullscreen ? Theme.c.previewPanel.radiusFullscreen : Theme.c.previewPanel.radius

    Item {
        id: viewportOuter
        anchors.top: parent.top
        anchors.topMargin: root.border.width
        anchors.horizontalCenter: parent.horizontalCenter
        width: parent.width - root.border.width * 2
        height: (root.previewFullscreen ? parent.height : parent.height - bottomBar.height) - root.border.width * 2
        clip: true

        PreviewViewport {
            id: panelViewport
            anchors.fill: parent
            // The inset is also the gutter the transform grips overflow into when a clip sits
            // flush against a canvas edge — viewportOuter clips, so a zero margin would shear
            // the bottom handles in half. Fullscreen is for watching, so the frame fills it.
            anchors.margins: root.previewFullscreen ? 0 : Theme.spacingLg
            mode3d: root.mode3d

            toolHost.tools: ({
                transform: transformTool,
                mask: maskTool,
                crop: cropTool,
                guideEdit: guideEditTool,
                maskDraw: maskDrawTool,
                maskBrush: maskBrushTool,
                maskAi: maskAiTool,
                tracker: trackerTool
            })
            toolHost.companion: transformCompanion

            // Key presses that bubble up from anything in the preview with focus.
            Keys.onPressed: (event) => {
                if (root.mode3d && modeView.view)
                    modeView.view.handleKey(event)
            }

            // Under the canvas, as navigation always was: it only takes the buttons and wheel
            // events the tools leave alone.
            Loader {
                id: modeView
                // Typed loosely, so the 3D view's handleKey can be reached.
                readonly property var view: item
                anchors.fill: parent
                z: -1
                sourceComponent: root.mode3d ? mode3dView : mode2dView
            }
        }

        PreviewOsd {
            id: osd
            anchors.fill: parent
            z: 1
            viewport: panelViewport
            interacting: panelViewport.toolHost.interacting
            held: root.previewFullscreen && (bottomHover.hovered || scrubSlider.pressed)
        }
    }

    // Shown while the grade is bypassed, so the picture is not taken for the graded one.
    Rectangle {
        id: originalBadge
        anchors.top: viewportOuter.top
        anchors.left: viewportOuter.left
        anchors.margins: Theme.spacingMd
        z: 2
        visible: EditorState.color.bypass
        height: 22
        width: originalText.implicitWidth + Theme.spacingLg * 2
        radius: height / 2
        color: Theme.c.previewOriginalBadge.background
        border.width: Theme.c.previewOriginalBadge.borderWidth
        border.color: Theme.c.previewOriginalBadge.border

        Text {
            id: originalText
            anchors.centerIn: parent
            text: qsTr("Original")
            color: Theme.c.previewOriginalBadge.foreground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeXs
            font.weight: Font.Medium
        }
    }

    // In fullscreen the bar sits over the picture, so it needs a surface of its own.
    PreviewFullscreenTransportScrim {
        anchors.fill: bottomBar
        z: 1
        shown: root.previewFullscreen && bottomBar.visible
        plateOpacity: bottomBar.opacity
    }

    // In fullscreen this is part of the OSD: it fades with it, and holds it up while in use.
    Column {
        id: bottomBar
        anchors.bottom: parent.bottom
        anchors.bottomMargin: root.border.width
        anchors.horizontalCenter: parent.horizontalCenter
        width: parent.width - root.border.width * 2
        z: 1
        opacity: !root.previewFullscreen || osd.shown ? 1 : 0
        visible: opacity > 0

        Behavior on opacity {
            NumberAnimation {
                duration: osd.shown ? Theme.durationFast : Theme.durationSlow
                easing.type: Theme.easing
            }
        }

        HoverHandler { id: bottomHover }

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

        PreviewTransportBar {
            id: transportBar
            width: parent.width
            height: implicitHeight
            projectFps: root.projectFps
            currentSeconds: root.currentSeconds
            durationSeconds: root.durationSeconds
            fullscreen: root.previewFullscreen
            formatTimecode: root.formatTimecode
            onFullscreenRequested: root.fullscreenRequested()
        }
    }

    Component {
        id: mode2dView
        Preview2DMode { viewport: panelViewport }
    }

    Component {
        id: mode3dView
        Preview3DMode { viewport: panelViewport }
    }

    Component {
        id: transformTool
        TransformOverlay {
            onTextEditRequested: (track, clip) => panelViewport.textEditor.start(track, clip, false)
        }
    }

    Component {
        id: maskTool
        MaskOverlay { }
    }

    Component {
        id: maskDrawTool
        MaskDrawOverlay { }
    }

    Component {
        id: maskBrushTool
        MaskBrushOverlay { }
    }

    Component {
        id: maskAiTool
        MaskAiOverlay { }
    }

    Component {
        id: trackerTool
        TrackerOverlay { }
    }

    // Built fresh each crop session, so the frame starts from the whole canvas every time.
    Component {
        id: cropTool
        CropOverlay {
            previewViewport: panelViewport
            previewCanvas: panelViewport.canvas
            hintDismissed: root.cropHintDismissed
            onHintDismissedChanged: root.cropHintDismissed = hintDismissed
        }
    }

    Component {
        id: guideEditTool
        GuideEditOverlay {
            previewCanvas: panelViewport.canvas
        }
    }

    Component {
        id: transformCompanion

        Item {
            // Light and focus handles for the depth effects. Above the transform gizmo, but only
            // the handles take the pointer, so the clip itself can still be dragged.
            DepthEffectOverlay {
                x: panelViewport.canvas.x
                y: panelViewport.canvas.y
                width: panelViewport.canvas.width
                height: panelViewport.canvas.height
                visible: EditorState.preview.handlesVisible && !root.mode3d
            }

            // The same lights in the 3D view, standing over the clip's relief.
            DepthLight3dOverlay {
                x: panelViewport.canvas.x
                y: panelViewport.canvas.y
                width: panelViewport.canvas.width
                height: panelViewport.canvas.height
                visible: EditorState.preview.handlesVisible && root.mode3d
            }

            // Assets dragged from the browsers land here as overlays at the playhead, or onto the
            // clip under the pointer. Above the transform handles so a drag passing over a
            // selected clip still reaches it.
            PreviewDropOverlay {
                id: previewDrop
                x: panelViewport.canvas.x
                y: panelViewport.canvas.y
                width: panelViewport.canvas.width
                height: panelViewport.canvas.height
                enabled: EditorState.projectFile.projectWidth() > 0 && !root.mode3d

                DropArea {
                    anchors.fill: parent
                    enabled: previewDrop.enabled
                    keys: AssetDrag.allKeys().concat(["text/uri-list"])

                    function isFileDrag(drop) {
                        return drop.keys.indexOf("text/uri-list") !== -1
                    }

                    function hoverDrop(drop) {
                        if (isFileDrag(drop)) {
                            previewDrop.hoverUrls(drop.hasUrls ? drop.urls : null, drop.x, drop.y)
                            return
                        }
                        const kind = kindOf(drop)
                        previewDrop.hover(kind, payloadOf(drop, kind), drop.x, drop.y)
                    }

                    function kindOf(drop) {
                        return AssetDrag.kindFromKeys(drop.keys)
                    }
                    function payloadOf(drop, kind) {
                        if (kind === "media" && EditorState.draggingAssetIndex >= 0)
                            return EditorState.draggingAssetIndex
                        return AssetDrag.payloadFromDrop(drop, kind)
                    }

                    onEntered: (drop) => hoverDrop(drop)
                    onPositionChanged: (drop) => hoverDrop(drop)
                    onExited: previewDrop.clear()
                    onDropped: (drop) => {
                        drop.accept(Qt.CopyAction)
                        if (isFileDrag(drop)) {
                            previewDrop.dropUrls(drop.hasUrls ? drop.urls : [], drop.x, drop.y)
                            return
                        }
                        const kind = kindOf(drop)
                        previewDrop.drop(kind, payloadOf(drop, kind), AssetDrag.labelFromDrop(drop, kind),
                                         drop.x, drop.y)
                    }
                }
            }
        }
    }

    // Seeks render from the engine itself (PlaybackEngine::setPlayheadUs).
    Connections {
        target: EditorState
        function onPlayingChanged() {
            if (!EditorState.playing)
                EditorState.playback.refreshFrame()
        }
    }

    Component.onCompleted: EditorState.playback.refreshFrame()
}
