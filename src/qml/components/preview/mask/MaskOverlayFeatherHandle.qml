import QtQuick
import Drift

HandleKnob {
    id: knob

    required property var mask
    required property var frame
    required property real scaleX

    readonly property bool onEllipse: mask.maskData.shape === "ellipse"
    readonly property real feather: mask.maskData.feather || 0
    readonly property bool onSplit: mask.isSplit
    readonly property real diag: Math.SQRT1_2
    readonly property real cornerR: mask.maskData.shape === "rectangle"
                                    ? (mask.maskData.roundness || 0) * Math.min(mask.width, mask.height) / 2 : 0
    readonly property real maxFeather: Math.max(64, 0.5 * Math.max(frame.width, frame.height) / Math.max(0.01, scaleX))
    readonly property real rowY: mask.height * 0.25
    readonly property bool inCorner: cornerR > rowY
    readonly property real cornerDy: cornerR - rowY

    visible: mask.isSelected && mask.maskData.enabled !== false && !EditorState.playing
             && mask.width > 0
    shape: "dot"
    space: knob.mask
    visualSize: Theme.touchInput ? 14 : 8
    cursor: "resize:" + knob.mask.rotation
    z: 1
    x: (onSplit ? mask.width / 2 + feather * scaleX : onEllipse ? mask.width / 2 + (mask.width / 2 + feather * scaleX) * diag
                  : mask.width + (inCorner
                                  ? Math.sqrt(Math.pow(cornerR + feather * scaleX, 2) - cornerDy * cornerDy) - cornerR
                                  : feather * scaleX)) - width / 2
    y: (onSplit ? mask.height / 2 + 40 : onEllipse ? mask.height / 2 - (mask.height / 2 + feather * scaleX) * diag
                  : mask.height * 0.25) - height / 2

    function setFrom(p) {
        const edge = knob.onSplit ? p.x - knob.mask.width / 2 : knob.onEllipse
                     ? ((p.x - knob.mask.width / 2) / knob.diag - knob.mask.width / 2)
                     : knob.inCorner
                       ? Math.sqrt(Math.pow(p.x - knob.mask.width + knob.cornerR, 2) + knob.cornerDy * knob.cornerDy) - knob.cornerR
                       : p.x - knob.mask.width
        EditorState.previewSetClipKeyframe(knob.mask.layerTrack, knob.mask.layerClip, "mask.feather",
                                           EditorState.playheadSeconds,
                                           Math.round(Math.max(0, Math.min(maxFeather, edge / Math.max(0.01, knob.scaleX)))))
    }

    onPressed: (point, modifiers, device) =>
        EditorState.drag.begin("maskFeather", device, qsTr("Edit %1").arg(qsTr("Feather")))
    onMoved: (point, modifiers) => { if (EditorState.drag.kind === "maskFeather") knob.setFrom(point) }
    onReleased: if (EditorState.drag.kind === "maskFeather") EditorState.drag.commit()
}
