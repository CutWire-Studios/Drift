import QtQuick
import QtQuick.Window
import Drift
// The canvas and the view onto it, shared by the desktop panel and the phone preview: aspect fit,
// zoom and pan, the composited picture, and the interaction tools laid over it. Children declared
// on an instance land on this item, which is where navigation input (mode views, pinch) belongs:
// it keeps working once the canvas is zoomed past the edges, and outlives whichever tool is on top.
Item {
    id: viewport

    // Which preview stream this shows: 0 the scene camera, 1 the editor's free viewpoint. The editor
    // stream fills the whole viewport around the project frame. Desktop only.
    property int stream: 0
    property bool mode3d: stream === 1
    // Canvas size is derived from this, so it has to be real screen pixels: item geometry is in
    // logical units, and on a scaled display a canvas built from those is upscaled by the ratio
    // before it ever reaches the screen. The phone renders at item size and passes 1.
    property real pixelRatio: Screen.devicePixelRatio
    // The phone's terse status text instead of the desktop's explanations.
    property bool compactStatus: false

    readonly property alias canvas: canvasRect
    readonly property alias toolHost: toolHost
    readonly property alias textEditor: textEditor

    readonly property real aspect: {
        void EditorState.tracksRevision
        const w = EditorState.projectFile.projectWidth()
        const h = EditorState.projectFile.projectHeight()
        return (w > 0 && h > 0) ? (w / h) : (16 / 9)
    }
    // Crop mode pulls the canvas in so there is room around it to drag an edge outward and grow
    // the frame.
    property real cropZoom: EditorState.preview.canvasCropMode || EditorState.preview.clipCropMode ? 0.72 : 1.0
    // View navigation. Both reset when crop mode starts or ends, so the view is never entered already scrolled
    // off-centre.
    property real userZoom: 1.0
    property real panX: 0
    property real panY: 0

    readonly property real baseWidth: Math.min(width, height * aspect)
    readonly property real baseHeight: baseWidth / aspect
    readonly property real fitWidth: baseWidth * cropZoom * userZoom
    readonly property real fitHeight: baseHeight * cropZoom * userZoom

    readonly property bool viewMoved: userZoom !== 1.0 || panX !== 0 || panY !== 0

    function resetView() {
        userZoom = 1.0
        panX = 0
        panY = 0
    }

    // Scales about (mx, my) in viewport coords: the point under the cursor keeps its position, so
    // zooming into a crop corner keeps that corner in place instead of drifting off screen.
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

    Connections {
        target: EditorState.preview
        function onCanvasCropModeChanged() { viewport.resetView() }
        function onClipCropModeChanged() { viewport.resetView() }
    }

    ThemeSlot {
        anchors.fill: parent
        z: -2
        slot: "PreviewLetterbox"
        control: viewport
        fallback: Component {
            Rectangle {
                anchors.fill: parent
                color: Theme.c.previewViewport.letterbox
            }
        }
    }

    Rectangle {
        id: canvasRect
        width: viewport.fitWidth
        height: viewport.fitHeight
        x: (viewport.width - width) / 2 + viewport.panX
        y: (viewport.height - height) / 2 + viewport.panY
        // The 3D view draws the whole panel, with its own outline of the stage; this rect only
        // keeps the project frame's place for the tools.
        // Explicit backing for the composited frame. The window clear color is
        // not a backstop once the surface has an alpha channel, and the frame
        // itself stays opaque (the compositor clears it at alpha 1) except when
        // the project background is transparent — that case is the checkerboard.
        color: {
            const projectBackground = EditorState.projectFile.background
            const transparentProject = projectBackground && projectBackground.kind === "transparent"
            if (viewport.mode3d || transparentProject)
                return "transparent"
            const token = Theme.c.previewViewport.background
            if (WindowBackdrop.translucencyActive && token.a < 1)
                return Qt.rgba(token.r, token.g, token.b, 1)
            return token
        }
        border.width: viewport.mode3d ? 0 : Theme.c.previewViewport.borderWidth
        border.color: Theme.c.previewViewport.border
        clip: !viewport.mode3d

        Checkerboard {
            anchors.fill: parent
            visible: !viewport.mode3d
        }

        PreviewItem {
            stream: viewport.stream
            x: viewport.mode3d ? -canvasRect.x : 0
            y: viewport.mode3d ? -canvasRect.y : 0
            width: viewport.mode3d ? viewport.width : canvasRect.width
            height: viewport.mode3d ? viewport.height : canvasRect.height
            // Not decoration: the engine only binds the window's frame cadence — afterAnimating,
            // frameSwapped and the screen's refresh rate — once a preview names it, and it is what
            // pulls each composited frame across.
            playback: EditorState.playback

            readonly property real pixelRatio: viewport.pixelRatio

            function updateRenderSize() {
                EditorState.playback.setPreviewRenderSize(viewport.stream, Math.round(width * pixelRatio),
                                                          Math.round(height * pixelRatio))
                EditorState.preview.notifyResized(viewport.stream)
            }

            Component.onCompleted: updateRenderSize()
            onWidthChanged: updateRenderSize()
            onHeightChanged: updateRenderSize()
            onPixelRatioChanged: updateRenderSize()
        }

        // Above anything a mode view adds to the canvas, such as the guides.
        PreviewStatusLayer {
            anchors.fill: parent
            z: 1
            compact: viewport.compactStatus
        }
    }

    PreviewToolHost {
        id: toolHost
        anchors.fill: parent
        z: 100
        canvas: canvasRect
        // The editor takes the pointer and the compositor draws the text; the grips would sit on it.
        visible: !textEditor.active
    }

    InlineTextEditor {
        id: textEditor
        z: 150
        canvas: canvasRect
    }
}
