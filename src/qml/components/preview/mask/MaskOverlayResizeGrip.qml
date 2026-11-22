import QtQuick
import Drift

// One resize grip, on HandleKnob like TransformManip's: Alt resizes about the centre, Shift flips
// the lock-aspect default on corners, Ctrl suspends snapping. The drag is one EditorState.drag
// session, so Esc cancels it whole.
HandleKnob {
    id: grip

    required property var modelData
    required property var mask
    required property var frame
    required property var overlay

    property bool running: false
    readonly property bool isCorner: modelData.dx !== 0 && modelData.dy !== 0
    readonly property real direction: isCorner ? (modelData.dx === modelData.dy ? 45 : 135)
                                               : (modelData.dx !== 0 ? 0 : 90)

    shape: isCorner ? "square" : "dot"
    space: grip.frame
    visualSize: Theme.touchInput ? 14 : 8
    cursor: "resize:" + (direction + (grip.overlay.editorState.rotation || 0) + grip.mask.rotation)
    x: (modelData.dx === 0 ? grip.mask.width / 2 : (modelData.dx < 0 ? 0 : grip.mask.width)) - width / 2
    y: (modelData.dy === 0 ? grip.mask.height / 2 : (modelData.dy < 0 ? 0 : grip.mask.height)) - height / 2

    function norm(point) {
        return Qt.point(point.x / Math.max(1, grip.frame.width), point.y / Math.max(1, grip.frame.height))
    }

    onPressed: (point, modifiers, device) => {
        const session = EditorState.masking.editSession
        EditorState.drag.begin("maskResize", device, qsTr("Edit %1").arg(qsTr("Mask")))
        if (!session.begin(grip.mask.layerId, grip.frame.width, grip.frame.height, false)) {
            EditorState.drag.cancel()
            return
        }
        const n = norm(point)
        session.beginResize(modelData.dx, modelData.dy, n.x, n.y)
        running = true
        grip.overlay.interacting = true
        grip.mask.forceActiveFocus()
    }

    onMoved: (point, modifiers) => {
        if (!running)
            return
        const m = Manip.fromQtFlags(modifiers)
        const n = norm(point)
        EditorState.masking.editSession.resize(n.x, n.y,
                                               isCorner && (!!grip.mask.maskData.lockAspect !== m.constrain),
                                               m.noSnap, m.fromCenter)
    }

    onReleased: if (running) EditorState.drag.commit()

    Connections {
        target: EditorState.drag
        enabled: grip.running
        function onCommitted() {
            grip.running = false
            grip.overlay.endInteraction()
        }
        function onCancelled() {
            grip.running = false
            EditorState.masking.editSession.cancel()
            grip.overlay.interacting = false
            Qt.callLater(grip.overlay.refreshOverlay)
        }
    }
}
