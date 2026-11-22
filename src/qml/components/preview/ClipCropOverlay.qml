import QtQuick
import Drift
// Crop gizmo for the selected clip's source frame, for the phone's crop page and the desktop's
// on-canvas Crop tool. The frame is drawn over the clip's whole
// source image (the visible box extended back out to the uncropped extent), in source pixels, so
// a crop made earlier can be grown again. The box and rotation come from the same overlay model the
// transform gizmo uses. Edits land on release through EditorState.setClipSourceFrame, the call the
// inspector's crop fields make, one undo step per drag. The overlay spans the whole viewport, so grips on a source frame that
// runs past the canvas edges still take the pointer; the frame itself is laid out on `canvas`.
Item {
    id: root

    property Item canvas
    // The PreviewViewport on the desktop, whose zoom and pan the frame then also drives.
    property var viewport: null
    property bool touch: Theme.touchInput

    property var box: null
    property int clipRevision: 0
    readonly property var clipData: { void clipRevision; return EditorState.selectedClipData }

    property alias aspect: frame.aspect
    readonly property alias cropFrame: frame
    // The phone crop page shows the ratio presets in its own controls.
    property bool hostedByPage: false

    readonly property int sourceW: {
        if (!clipData || !clipData.sourceWidth)
            return 0
        return (Math.abs(clipData.sourceRotation || 0) % 180) === 90 ? clipData.sourceHeight : clipData.sourceWidth
    }
    readonly property int sourceH: {
        if (!clipData || !clipData.sourceHeight)
            return 0
        return (Math.abs(clipData.sourceRotation || 0) % 180) === 90 ? clipData.sourceWidth : clipData.sourceHeight
    }
    readonly property rect sourceFrame: {
        const f = clipData && clipData.sourceFrame
        return Qt.rect(f ? Number(f.x || 0) : 0, f ? Number(f.y || 0) : 0,
                       f ? Number(f.width || 1) : 1, f ? Number(f.height || 1) : 1)
    }
    property real cropX: 0
    property real cropY: 0
    property real cropW: 0
    property real cropH: 0

    readonly property bool changed: sourceW > 0 && (cropW < sourceW - 1 || cropH < sourceH - 1)

    function refresh() {
        if (EditorState.playing)
            return
        let found = null
        for (const b of EditorState.preview.clipsAtPlayhead()) {
            if (b.track === EditorState.selectedTrack && b.clip === EditorState.selectedClip) {
                found = b
                break
            }
        }
        root.box = found
    }

    function reset() {
        frame.aspect = 0
        cropX = 0
        cropY = 0
        cropW = root.sourceW
        cropH = root.sourceH
        commit()
    }

    function syncFromClip() {
        cropX = sourceFrame.x * sourceW
        cropY = sourceFrame.y * sourceH
        cropW = sourceFrame.width * sourceW
        cropH = sourceFrame.height * sourceH
    }

    function finish() {
        EditorState.preview.clipCropMode = false
    }

    function commit() {
        const sw = root.sourceW
        const sh = root.sourceH
        if (!root.clipData || !root.clipData.id || sw <= 0 || sh <= 0)
            return
        const w = Math.max(1, Math.min(sw, cropW))
        const h = Math.max(1, Math.min(sh, cropH))
        const x = Math.max(0, Math.min(sw - w, cropX))
        const y = Math.max(0, Math.min(sh - h, cropY))
        cropX = x
        cropY = y
        cropW = w
        cropH = h
        EditorState.setClipSourceFrame(root.clipData.id, x / sw, y / sh, w / sw, h / sh)
    }

    Component.onCompleted: {
        refresh()
        syncFromClip()
    }

    onSourceWChanged: syncFromClip()
    onSourceHChanged: syncFromClip()

    Connections {
        target: EditorState
        function onTracksChanged() { root.clipRevision++; root.refresh() }
        function onSelectedClipDataChanged() { root.clipRevision++ }
        function onSelectionChanged() { root.clipRevision++; root.refresh(); root.syncFromClip() }
        function onClipPropertiesPreviewed() { root.refresh() }
        function onPlayheadSecondsChanged() {
            if (!EditorState.scrubbing)
                root.refresh()
        }
        function onPlayingChanged() { root.refresh() }
    }

    CropFrame {
        id: frame
        anchors.fill: parent
        touch: root.touch
        viewport: root.viewport
        visible: root.box !== null && root.sourceW > 0
        spaceSize: Qt.size(root.sourceW, root.sourceH)
        crop: Qt.rect(root.cropX, root.cropY, root.cropW, root.cropH)
        minSize: 16
        showPresets: true
        touchPresets: !root.hostedByPage
        undoText: qsTr("Crop clip")

        readonly property var box: root.box
        readonly property real sx: box && root.canvas ? root.canvas.width / Math.max(1, box.canvasWidth) : 1
        readonly property real sy: box && root.canvas ? root.canvas.height / Math.max(1, box.canvasHeight) : 1
        readonly property real fullW: box ? box.width / root.sourceFrame.width : 0
        readonly property real fullH: box ? box.height / root.sourceFrame.height : 0
        readonly property real canvasX: root.canvas ? root.canvas.x : 0
        readonly property real canvasY: root.canvas ? root.canvas.y : 0

        stageRect: box ? Qt.rect(canvasX + (box.x - root.sourceFrame.x * fullW) * sx,
                                 canvasY + (box.y - root.sourceFrame.y * fullH) * sy,
                                 fullW * sx, fullH * sy)
                       : Qt.rect(0, 0, 0, 0)
        // The pivot is the visible box's centre, as for the transform gizmo.
        stagePivot: box ? Qt.point((box.x + box.width / 2) * sx - (stageRect.x - canvasX),
                                   (box.y + box.height / 2) * sy - (stageRect.y - canvasY))
                        : Qt.point(0, 0)
        stageRotation: box ? box.rotation : 0

        onEdited: (c) => {
            root.cropX = c.x
            root.cropY = c.y
            root.cropW = c.width
            root.cropH = c.height
        }
        onCommitted: root.commit()
        onConfirmed: root.finish()
        onEscaped: root.finish()
        Component.onCompleted: if (!root.touch) forceActiveFocus()
    }

    Row {
        visible: !root.touch
        spacing: Theme.spacingMd
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: Theme.spacingMd

        ThemedButton {
            variant: "secondary"
            text: qsTr("Reset")
            tooltip: qsTr("Show the whole source again")
            enabled: root.changed
            onClicked: root.reset()
        }

        ThemedButton {
            variant: "primary"
            text: qsTr("Done")
            tooltip: qsTr("Leave the crop tool (Enter)")
            onClicked: root.finish()
        }
    }
}
