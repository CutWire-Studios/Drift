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
// underneath, so this overlay only claims the frame.
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

    property bool didResize: false

    // Locked w / h of the frame; 0 leaves it free. Set by the ratio chips and the crop page.
    property alias aspect: frame.aspect
    // The phone crop page owns Apply/Cancel and the readout, so the overlay shows only the frame.
    property bool hostedByPage: false

    readonly property bool changed: outW !== projW || outH !== projH
                                    || Math.round(cropX) !== 0 || Math.round(cropY) !== 0

    function reset() {
        frame.aspect = 0
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

    // The frame may be dragged and moved past the canvas edges: that grows the output.
    CropFrame {
        id: frame
        anchors.fill: parent
        touch: true
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
