import QtQuick
import Drift
// Phone crop gizmo for the selected clip's source frame. The frame is drawn over the clip's whole
// source image (the visible box extended back out to the uncropped extent), in source pixels, so
// a crop made earlier can be grown again. The box and rotation come from the same overlay model the
// transform gizmo uses. Edits land on release through EditorState.setClipSourceFrame, the call the
// inspector's crop fields make.
Item {
    id: root

    property var box: null
    property int clipRevision: 0
    readonly property var clipData: { void clipRevision; return EditorState.selectedClipData }

    property alias aspect: stage.aspect
    property bool interacting: false

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
    readonly property real sourceAspect: sourceH > 0 ? sourceW / sourceH : 0
    readonly property rect sourceFrame: {
        const f = clipData && clipData.sourceFrame
        return Qt.rect(f ? Number(f.x || 0) : 0, f ? Number(f.y || 0) : 0,
                       f ? Number(f.width || 1) : 1, f ? Number(f.height || 1) : 1)
    }
    readonly property bool changed: sourceW > 0 && (stage.cropW < sourceW - 1 || stage.cropH < sourceH - 1)

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
        stage.cropX = 0
        stage.cropY = 0
        stage.cropW = root.sourceW
        stage.cropH = root.sourceH
        stage.commit()
    }

    function syncFromClip() {
        stage.cropX = sourceFrame.x * sourceW
        stage.cropY = sourceFrame.y * sourceH
        stage.cropW = sourceFrame.width * sourceW
        stage.cropH = sourceFrame.height * sourceH
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

    Item {
        id: stage

        readonly property real sx: box ? root.width / Math.max(1, box.canvasWidth) : 1
        readonly property real sy: box ? root.height / Math.max(1, box.canvasHeight) : 1
        readonly property real fullW: box ? box.width / root.sourceFrame.width : 0
        readonly property real fullH: box ? box.height / root.sourceFrame.height : 0
        readonly property var box: root.box

        visible: box !== null && root.sourceW > 0
        x: box ? (box.x - root.sourceFrame.x * fullW) * sx : 0
        y: box ? (box.y - root.sourceFrame.y * fullH) * sy : 0
        width: fullW * sx
        height: fullH * sy

        // The pivot is the visible box's centre, as for the transform gizmo.
        transform: Rotation {
            origin.x: stage.box ? (stage.box.x + stage.box.width / 2) * stage.sx - stage.x : 0
            origin.y: stage.box ? (stage.box.y + stage.box.height / 2) * stage.sy - stage.y : 0
            angle: stage.box ? stage.box.rotation : 0
        }

        // Interface the shared crop grips read, in source pixels.
        property real cropX: 0
        property real cropY: 0
        property real cropW: 0
        property real cropH: 0
        property real aspect: 0
        property bool didResize: false
        readonly property real gripSize: 14
        readonly property real gripTouch: 44
        readonly property real pxScale: root.sourceW > 0 ? width / root.sourceW : 1
        readonly property real frameX: cropX * pxScale
        readonly property real frameY: cropY * pxScale
        readonly property real frameW: cropW * pxScale
        readonly property real frameH: cropH * pxScale

        function gripReleased() {
            commit()
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

        onAspectChanged: {
            if (aspect <= 0 || root.sourceW <= 0)
                return
            const cx = cropX + cropW / 2
            const cy = cropY + cropH / 2
            const w = Math.min(cropW, cropH * aspect)
            cropW = w
            cropH = w / aspect
            cropX = cx - cropW / 2
            cropY = cy - cropH / 2
            commit()
        }

        AndroidCropDimMask {
            anchors.fill: parent
            frameX: stage.frameX
            frameY: stage.frameY
            frameW: stage.frameW
            frameH: stage.frameH
        }

        AndroidCropFrame {
            x: stage.frameX
            y: stage.frameY
            width: stage.frameW
            height: stage.frameH
        }

        Repeater {
            model: [
                { dx: -1, dy:  0 },
                { dx:  1, dy:  0 },
                { dx:  0, dy: -1 },
                { dx:  0, dy:  1 },
                { dx: -1, dy: -1 },
                { dx:  1, dy:  1 },
                { dx:  1, dy: -1 },
                { dx: -1, dy:  1 }
            ]

            delegate: AndroidCropGrip {
                crop: stage
            }
        }
    }
}
