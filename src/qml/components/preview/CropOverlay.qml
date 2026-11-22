import QtQuick
import Drift

// Canvas crop tool. Lives outside the (clipped) canvas rect so the
// crop frame can be dragged past the current edges to grow the
// output. Values are kept in project pixels; committing hands the
// rect to AppController, which rebases clip layout so nothing
// moves or rescales — content outside the new frame is simply lost.
Item {
    id: root

    // The PreviewViewport (zoom/pan math + reset) and the canvas
    // rectangle whose on-screen geometry the crop frame maps against.
    property var previewViewport
    property var previewCanvas

    readonly property int projW: { void EditorState.tracksRevision; return Math.max(1, EditorState.projectFile.projectWidth()) }
    readonly property int projH: { void EditorState.tracksRevision; return Math.max(1, EditorState.projectFile.projectHeight()) }
    // Project px → viewport px.
    readonly property real pxScale: root.previewCanvas.width / projW

    property real cropX: 0
    property real cropY: 0
    property real cropW: projW
    property real cropH: projH

    readonly property int outW: Math.round(cropW)
    readonly property int outH: Math.round(cropH)

    // Hint bookkeeping. Set as each input is first used; once all
    // three are known the hint retires and does not come back, so
    // reopening the tool is not nagging.
    property bool didResize: false
    property bool didZoom: false
    property bool didPan: false
    property bool hintDismissed: false
    onDidResizeChanged: maybeDismissHint()
    onDidZoomChanged: maybeDismissHint()
    onDidPanChanged: maybeDismissHint()

    function maybeDismissHint() {
        if (didResize && didZoom && didPan)
            hintDismissed = true
    }
    readonly property bool changed: outW !== projW || outH !== projH
                                    || Math.round(cropX) !== 0 || Math.round(cropY) !== 0

    function reset() {
        frame.aspect = 0
        cropX = 0
        cropY = 0
        cropW = projW
        cropH = projH
    }

    function apply() {
        if (changed)
            EditorState.applyCanvasCrop(cropX, cropY, cropW, cropH)
        EditorState.preview.canvasCropMode = false
    }

    // Screen-space crop frame, relative to the viewport.
    readonly property real frameX: root.previewCanvas.x + cropX * pxScale
    readonly property real frameY: root.previewCanvas.y + cropY * pxScale
    readonly property real frameW: cropW * pxScale
    readonly property real frameH: cropH * pxScale

    property alias aspect: frame.aspect

    // Everything outside the crop frame is discarded, so it is dimmed. The frame takes the
    // middle-drag pan, Ctrl+wheel zoom and pinch, and the middle and wheel only where no grip is.
    CropFrame {
        id: frame
        anchors.fill: parent
        viewport: root.previewViewport
        stageRect: Qt.rect(root.previewCanvas.x, root.previewCanvas.y,
                           root.previewCanvas.width, root.previewCanvas.height)
        spaceSize: Qt.size(root.projW, root.projH)
        crop: Qt.rect(root.cropX, root.cropY, root.cropW, root.cropH)
        minSize: 16
        overflow: true
        scrimFillsViewport: true
        showPresets: true
        undoText: qsTr("Crop canvas")
        onEdited: (c) => {
            root.cropX = c.x
            root.cropY = c.y
            root.cropW = c.width
            root.cropH = c.height
            root.didResize = true
        }
        onViewZoomed: root.didZoom = true
        onViewPanned: root.didPan = true
        onConfirmed: root.apply()
        onEscaped: EditorState.preview.canvasCropMode = false
    }

    Component.onCompleted: frame.forceActiveFocus()

    // First-run hint. The crop tool has three non-obvious inputs and
    // no menu surface to discover them from, so they are spelled out
    // while the tool is open. Fades once the user has driven all
    // three, and stays gone for the rest of the session.
    CropOverlayHint {
        anchors.topMargin: Math.max(Theme.spacingLg, frame.barBottom + Theme.spacingSm)
        hintDismissed: root.hintDismissed
        didResize: root.didResize
        didZoom: root.didZoom
        didPan: root.didPan
    }

    // Readout + commit, pinned under the crop frame but kept inside
    // the viewport so it stays reachable at any crop size.
    Row {
        spacing: Theme.spacingMd
        x: Math.max(0, Math.min(root.width - width,
                                root.frameX + (root.frameW - width) / 2))
        y: Math.max(0, Math.min(root.height - height,
                                root.frameY + root.frameH + Theme.spacingMd))

        CropOverlaySizeBadge {
            label: root.outW + "×" + root.outH
                   + "   " + Math.round(root.previewViewport.userZoom * 100) + "%"
        }

        ThemedButton {
            variant: "secondary"
            text: qsTr("Fit view")
            tooltip: qsTr("Recentre and reset zoom")
            enabled: root.previewViewport.userZoom !== 1.0 || root.previewViewport.panX !== 0 || root.previewViewport.panY !== 0
            onClicked: root.previewViewport.resetView()
        }

        ThemedButton {
            variant: "secondary"
            text: qsTr("Reset")
            tooltip: qsTr("Reset crop to the full video size")
            enabled: root.changed
            onClicked: root.reset()
        }

        ThemedButton {
            variant: "primary"
            text: qsTr("Apply")
            onClicked: root.apply()
        }
    }
}
