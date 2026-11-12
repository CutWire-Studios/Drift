import QtQuick
import Drift

Rectangle {
    id: knob

    required property var mask
    required property var frame
    required property real scaleX

    readonly property bool onEllipse: mask.maskData.shape === "ellipse"
    readonly property real feather: mask.maskData.feather || 0
    readonly property real diag: Math.SQRT1_2

    visible: mask.isSelected && mask.maskData.enabled !== false && !EditorState.playing
             && mask.width > 0
    width: Theme.spacingMd
    height: Theme.spacingMd
    radius: width / 2
    z: 1
    color: area.containsMouse || area.pressed
           ? Theme.c.maskOverlayRotateHandle.backgroundHover : Theme.c.maskOverlayRotateHandle.background
    border.width: Theme.c.maskOverlayRotateHandle.borderWidth
    border.color: area.containsMouse || area.pressed
                  ? Theme.c.maskOverlayRotateHandle.borderHover : Theme.c.maskOverlayRotateHandle.border
    x: (onEllipse ? mask.width / 2 + (mask.width / 2 + feather * scaleX) * diag
                  : mask.width + feather * scaleX) - width / 2
    y: (onEllipse ? mask.height / 2 - (mask.height / 2 + feather * scaleX) * diag
                  : mask.height * 0.25) - height / 2

    function setFrom(mouse) {
        const p = area.mapToItem(knob.mask, mouse.x, mouse.y)
        const edge = knob.onEllipse
                     ? ((p.x - knob.mask.width / 2) / knob.diag - knob.mask.width / 2)
                     : p.x - knob.mask.width
        EditorState.previewSetClipKeyframe(knob.mask.layerTrack, knob.mask.layerClip, "mask.feather",
                                           EditorState.playheadSeconds,
                                           Math.round(Math.max(0, Math.min(64, edge / Math.max(0.01, knob.scaleX)))))
    }

    MouseArea {
        id: area
        anchors.fill: parent
        anchors.margins: -Theme.spacingMd
        hoverEnabled: true
        cursorShape: Qt.SizeHorCursor
        preventStealing: true
        onWheel: (wheel) => { wheel.accepted = false }

        onPressed: EditorState.beginPreviewDrag(qsTr("Edit %1").arg(qsTr("Feather")))
        onPositionChanged: (mouse) => { if (pressed) knob.setFrom(mouse) }
        onReleased: EditorState.commitPreviewDrag()
        onCanceled: EditorState.commitPreviewDrag()
    }
}
