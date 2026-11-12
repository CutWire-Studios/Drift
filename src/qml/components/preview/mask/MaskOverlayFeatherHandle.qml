import QtQuick
import Drift

Rectangle {
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
    readonly property real rowY: mask.height * 0.25
    readonly property bool inCorner: cornerR > rowY
    readonly property real cornerDy: cornerR - rowY

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
    x: (onSplit ? mask.width / 2 + feather * scaleX : onEllipse ? mask.width / 2 + (mask.width / 2 + feather * scaleX) * diag
                  : mask.width + (inCorner
                                  ? Math.sqrt(Math.pow(cornerR + feather * scaleX, 2) - cornerDy * cornerDy) - cornerR
                                  : feather * scaleX)) - width / 2
    y: (onSplit ? mask.height / 2 + 40 : onEllipse ? mask.height / 2 - (mask.height / 2 + feather * scaleX) * diag
                  : mask.height * 0.25) - height / 2

    function setFrom(mouse) {
        const p = area.mapToItem(knob.mask, mouse.x, mouse.y)
        const edge = knob.onSplit ? p.x - knob.mask.width / 2 : knob.onEllipse
                     ? ((p.x - knob.mask.width / 2) / knob.diag - knob.mask.width / 2)
                     : knob.inCorner
                       ? Math.sqrt(Math.pow(p.x - knob.mask.width + knob.cornerR, 2) + knob.cornerDy * knob.cornerDy) - knob.cornerR
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
