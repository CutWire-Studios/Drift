import QtQuick
import Drift

Rectangle {
    id: knob

    required property var mask
    required property var frame

    readonly property real minInset: Theme.spacingLg * 1.5
    readonly property real maxRadius: Math.min(mask.width, mask.height) / 2
    readonly property real roundness: mask.maskData.roundness || 0

    visible: mask.isSelected && mask.editable && mask.maskData.shape === "rectangle"
             && !EditorState.playing && maxRadius > minInset
    width: Theme.spacingMd
    height: Theme.spacingMd
    radius: width / 2
    color: area.containsMouse || area.pressed
           ? Theme.c.maskOverlayRotateHandle.backgroundHover : Theme.c.maskOverlayRotateHandle.background
    border.width: Theme.c.maskOverlayRotateHandle.borderWidth
    border.color: area.containsMouse || area.pressed
                  ? Theme.c.maskOverlayRotateHandle.borderHover : Theme.c.maskOverlayRotateHandle.border
    x: minInset + roundness * maxRadius - width / 2
    y: x

    function setFrom(mouse) {
        const p = area.mapToItem(knob.mask, mouse.x, mouse.y)
        const t = ((p.x + p.y) / 2 - knob.minInset) / Math.max(1, knob.maxRadius)
        EditorState.previewSetClipKeyframe(knob.mask.layerTrack, knob.mask.layerClip, "mask.roundness",
                                           EditorState.playheadSeconds, Math.max(0, Math.min(1, t)))
    }

    MouseArea {
        id: area
        anchors.fill: parent
        anchors.margins: -Theme.spacingMd
        hoverEnabled: true
        cursorShape: Qt.SizeFDiagCursor
        preventStealing: true
        onWheel: (wheel) => { wheel.accepted = false }

        onPressed: EditorState.beginPreviewDrag(qsTr("Edit %1").arg(qsTr("Roundness")))
        onPositionChanged: (mouse) => { if (pressed) knob.setFrom(mouse) }
        onReleased: EditorState.commitPreviewDrag()
        onCanceled: EditorState.commitPreviewDrag()
    }
}
