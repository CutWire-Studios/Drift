import QtQuick
import QtQuick.Window
import Drift
import ".."

// The canvas and the view onto it, shared by the desktop panel and the phone preview: aspect fit,
// zoom and pan, the composited picture, and the interaction tools laid over it. Children declared
// on an instance land on this item, which is where navigation input (mode views, pinch) belongs:
// it keeps working once the canvas is zoomed past the edges, and outlives whichever tool is on top.
Item {
    id: viewport

    // In 3D the picture fills the whole viewport around the project frame. Desktop only.
    property bool mode3d: false
    // Canvas size is derived from this, so it has to be real screen pixels: item geometry is in
    // logical units, and on a scaled display a canvas built from those is upscaled by the ratio
    // before it ever reaches the screen. The phone renders at item size and passes 1.
    property real pixelRatio: Screen.devicePixelRatio
    // The phone's terse status text instead of the desktop's explanations.
    property bool compactStatus: false

    readonly property alias canvas: canvasRect
    readonly property alias toolHost: toolHost

    readonly property real aspect: {
        void EditorState.tracksRevision
        const w = EditorState.projectFile.projectWidth()
        const h = EditorState.projectFile.projectHeight()
        return (w > 0 && h > 0) ? (w / h) : (16 / 9)
    }
    // Crop mode pulls the canvas in so there is room around it to drag an edge outward and grow
    // the frame.
    property real cropZoom: EditorState.preview.canvasCropMode ? 0.72 : 1.0
    // View navigation. Both reset when crop mode starts or ends and when the mode changes, so
    // neither view is ever entered already scrolled off-centre.
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

    // The 2D zoom and pan mean nothing to the 3D view.
    Connections {
        target: EditorState.preview
        function onModeChanged() { viewport.resetView() }
        function onCanvasCropModeChanged() { viewport.resetView() }
    }

    Rectangle {
        id: canvasRect
        width: viewport.fitWidth
        height: viewport.fitHeight
        x: (viewport.width - width) / 2 + viewport.panX
        y: (viewport.height - height) / 2 + viewport.panY
        // The 3D view draws the whole panel, with its own outline of the stage; this rect only
        // keeps the project frame's place for the tools.
        color: viewport.mode3d || (EditorState.projectFile.background && EditorState.projectFile.background.kind === "transparent")
               ? "transparent" : Theme.overlayColor
        border.width: viewport.mode3d ? 0 : Theme.borderWidth
        border.color: Theme.border
        clip: !viewport.mode3d

        Checkerboard {
            anchors.fill: parent
            visible: !viewport.mode3d
        }

        PreviewItem {
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
                EditorState.playback.setPreviewRenderSize(Math.round(width * pixelRatio),
                                                          Math.round(height * pixelRatio))
                EditorState.preview.notifyResized()
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
    }
}
