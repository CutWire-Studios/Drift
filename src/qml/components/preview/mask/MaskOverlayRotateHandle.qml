import QtQuick
import Drift

// Rotation handle, 28px above the box.
Rectangle {
    id: knob

    // The mask box delegate, the clip frame it is normalized to, and the overlay.
    required property var mask
    required property var frame
    required property var overlay

    visible: knob.mask.editable && knob.mask.isSelected
    width: Theme.spacingLg
    height: Theme.spacingLg
    radius: width / 2
    color: rotateArea.containsMouse || rotateArea.pressed
           ? Theme.c.maskOverlayRotateHandle.backgroundHover : Theme.c.maskOverlayRotateHandle.background
    border.width: Theme.c.maskOverlayRotateHandle.borderWidth
    border.color: rotateArea.containsMouse || rotateArea.pressed
                  ? Theme.c.maskOverlayRotateHandle.borderHover : Theme.c.maskOverlayRotateHandle.border
    x: parent.width / 2 - width / 2
    y: -28 - height / 2

    MouseArea {
        id: rotateArea
        anchors.fill: parent
        anchors.margins: -Theme.spacingMd
        hoverEnabled: true
        cursorShape: Qt.CrossCursor
        preventStealing: true
        onWheel: (wheel) => { wheel.accepted = false }

        onPressed: {
            if (!EditorState.masking.editSession.begin(knob.mask.layerId, knob.frame.width,
                                                       knob.frame.height))
                return
            knob.overlay.interacting = true
            knob.mask.forceActiveFocus()
        }

        onPositionChanged: (mouse) => {
            if (!pressed)
                return
            // Angle is measured in the clip frame, so map through it rather than
            // through the rotating box.
            const p = mapToItem(knob.frame, mouse.x, mouse.y)
            EditorState.masking.editSession.rotate(p.x / Math.max(1, knob.frame.width),
                                                   p.y / Math.max(1, knob.frame.height),
                                                   (mouse.modifiers & Qt.ShiftModifier) !== 0)
        }

        onReleased: knob.overlay.endInteraction()
        onCanceled: knob.overlay.endInteraction()
    }
}
