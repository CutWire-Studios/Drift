import QtQuick
import QtQuick.Controls.Basic
import Drift
// Touch canvas crop tool. Lives outside the (clipped) canvas rect so the crop
// frame can be dragged past the current edges to grow the output. Values are
// kept in project pixels; committing hands the rect to AppController, which
// rebases clip layout so nothing moves or rescales — content outside the new
// frame is simply lost.
//
// Navigation is not owned here: the viewport's pinch gesture zooms and pans
// underneath, so this overlay only claims the grips.
Item {
    id: root

    // The canvas rectangle whose on-screen geometry the crop frame maps against.
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

    readonly property real gripSize: 14
    readonly property real gripTouch: 44

    property bool didResize: false

    // Locked w / h of the frame; 0 leaves it free. Read by the grips.
    property real aspect: 0
    // The phone crop page owns Apply/Cancel and the readout, so the overlay shows only the frame.
    property bool hostedByPage: false

    function gripReleased() {
    }

    // The frame is re-fitted to the ratio about its centre, within the canvas-sized area at most.
    onAspectChanged: {
        if (aspect <= 0)
            return
        const cx = cropX + cropW / 2
        const cy = cropY + cropH / 2
        let w = Math.min(cropW, cropH * aspect)
        let h = w / aspect
        cropW = w
        cropH = h
        cropX = cx - w / 2
        cropY = cy - h / 2
    }

    readonly property bool changed: outW !== projW || outH !== projH
                                    || Math.round(cropX) !== 0 || Math.round(cropY) !== 0

    function reset() {
        cropX = 0
        cropY = 0
        cropW = projW
        cropH = projH
    }

    function commit() {
        if (changed)
            EditorState.applyCanvasCrop(cropX, cropY, cropW, cropH)
    }

    function apply() {
        commit()
        EditorState.preview.canvasCropMode = false
    }

    // Screen-space crop frame, relative to the viewport.
    readonly property real frameX: root.previewCanvas.x + cropX * pxScale
    readonly property real frameY: root.previewCanvas.y + cropY * pxScale
    readonly property real frameW: cropW * pxScale
    readonly property real frameH: cropH * pxScale

    AndroidCropDimMask {
        anchors.fill: parent
        frameX: root.frameX
        frameY: root.frameY
        frameW: root.frameW
        frameH: root.frameH
    }

    AndroidCropFrame {
        x: root.frameX
        y: root.frameY
        width: root.frameW
        height: root.frameH
    }

    // Eight drag handles: 4 edges then 4 corners. `dx`/`dy` say which edges each
    // handle moves (-1 = left/top, +1 = right/bottom).
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
            crop: root
        }
    }

    AndroidCropHint {
        visible: !root.hostedByPage
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        anchors.topMargin: Theme.spacingLg
        retired: root.didResize
    }

    // Readout + commit. Pinned to the bottom of the viewport rather than to the
    // frame: at a small crop the frame-following row landed under a fingertip.
    Row {
        visible: !root.hostedByPage
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: Theme.spacingMd
        spacing: Theme.spacingMd

        AndroidOnMediaReadout {
            anchors.verticalCenter: parent.verticalCenter
            text: root.outW + "×" + root.outH
        }

        ThemedButton {
            variant: "secondary"
            text: qsTr("Reset")
            enabled: root.changed
            onClicked: root.reset()
        }

        ThemedButton {
            variant: "secondary"
            text: qsTr("Cancel")
            onClicked: EditorState.preview.canvasCropMode = false
        }

        ThemedButton {
            variant: "primary"
            text: qsTr("Apply")
            onClicked: root.apply()
        }
    }
}
