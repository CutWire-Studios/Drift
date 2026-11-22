import QtQuick
import Drift

// Rotation handle, 28px above the box. Free; Shift steps 15 degrees. One EditorState.drag session,
// so Esc cancels it.
HandleKnob {
    id: knob

    // The mask box delegate, the clip frame it is normalized to, and the overlay.
    required property var mask
    required property var frame
    required property var overlay

    property bool running: false

    visible: knob.mask.editable && knob.mask.isSelected
    shape: "dot"
    space: knob.frame
    visualSize: Theme.touchInput ? 16 : 9
    cursor: "rotate:" + (-90 + (knob.overlay.editorState.rotation || 0) + knob.mask.rotation)
    x: parent.width / 2 - width / 2
    y: (mask.isSplit ? mask.height / 2 - 64 : -28) - height / 2

    onPressed: (point, modifiers, device) => {
        const session = EditorState.masking.editSession
        EditorState.drag.begin("maskRotate", device, qsTr("Edit %1").arg(qsTr("Mask")))
        if (!session.begin(knob.mask.layerId, knob.frame.width, knob.frame.height, false)) {
            EditorState.drag.cancel()
            return
        }
        running = true
        knob.overlay.interacting = true
        knob.mask.forceActiveFocus()
    }

    // The angle is measured in the clip frame, which `space` already is.
    onMoved: (point, modifiers) => {
        if (!running)
            return
        EditorState.masking.editSession.rotate(point.x / Math.max(1, knob.frame.width),
                                               point.y / Math.max(1, knob.frame.height),
                                               Manip.fromQtFlags(modifiers).constrain)
    }

    onReleased: if (running) EditorState.drag.commit()

    Connections {
        target: EditorState.drag
        enabled: knob.running
        function onCommitted() {
            knob.running = false
            knob.overlay.endInteraction()
        }
        function onCancelled() {
            knob.running = false
            EditorState.masking.editSession.cancel()
            knob.overlay.interacting = false
            Qt.callLater(knob.overlay.refreshOverlay)
        }
    }
}
