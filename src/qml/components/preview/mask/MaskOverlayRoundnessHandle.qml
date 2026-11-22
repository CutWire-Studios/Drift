import QtQuick
import Drift

HandleKnob {
    id: knob

    required property var mask
    required property var frame

    readonly property real minInset: Theme.spacingLg * 1.5
    readonly property real maxRadius: Math.min(mask.width, mask.height) / 2
    readonly property real roundness: mask.maskData.roundness || 0

    visible: mask.isSelected && mask.editable && mask.maskData.shape === "rectangle"
             && !EditorState.playing && maxRadius > minInset
    shape: "dot"
    space: knob.mask
    visualSize: Theme.touchInput ? 14 : 8
    cursor: "resize:" + (45 + knob.mask.rotation)
    x: minInset + roundness * maxRadius - width / 2
    y: x

    function setFrom(p) {
        const t = ((p.x + p.y) / 2 - knob.minInset) / Math.max(1, knob.maxRadius)
        EditorState.previewSetClipKeyframe(knob.mask.layerTrack, knob.mask.layerClip, "mask.roundness",
                                           EditorState.playheadSeconds, Math.max(0, Math.min(1, t)))
    }

    onPressed: (point, modifiers, device) =>
        EditorState.drag.begin("maskRoundness", device, qsTr("Edit %1").arg(qsTr("Roundness")))
    onMoved: (point, modifiers) => { if (EditorState.drag.kind === "maskRoundness") knob.setFrom(point) }
    onReleased: if (EditorState.drag.kind === "maskRoundness") EditorState.drag.commit()
}
