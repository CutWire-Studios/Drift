import QtQuick
import Drift

// One resize grip. The same frame the canvas crop tool and TransformOverlay use.
Rectangle {
    id: grip

    required property var modelData
    required property var mask
    required property var frame
    required property var overlay

    readonly property real hs: Theme.spacingLg
    readonly property bool isCorner: modelData.dx !== 0 && modelData.dy !== 0

    width: hs
    height: hs
    radius: Theme.c.maskOverlayResizeGrip.radius
    color: gripArea.containsMouse || gripArea.pressed
           ? Theme.c.maskOverlayResizeGrip.backgroundHover : Theme.c.maskOverlayResizeGrip.background
    border.width: Theme.c.maskOverlayResizeGrip.borderWidth
    border.color: gripArea.containsMouse || gripArea.pressed
                  ? Theme.c.maskOverlayResizeGrip.borderHover : Theme.c.maskOverlayResizeGrip.border

    x: (modelData.dx === 0 ? grip.mask.width / 2
                           : (modelData.dx < 0 ? 0 : grip.mask.width)) - hs / 2
    y: (modelData.dy === 0 ? grip.mask.height / 2
                           : (modelData.dy < 0 ? 0 : grip.mask.height)) - hs / 2

    MouseArea {
        id: gripArea
        anchors.fill: parent
        // Generous invisible margin: the visible dot stays small enough not to
        // hide the box edge it sits on.
        anchors.margins: -Theme.spacingMd
        hoverEnabled: true
        cursorShape: grip.modelData.cursor
        // The body DragHandler would otherwise take the grab once the drag
        // threshold is passed, turning a resize into a move.
        preventStealing: true
        // Zooming with the pointer on a grip should still zoom.
        onWheel: (wheel) => { wheel.accepted = false }

        onPressed: (mouse) => {
            const p = mapToItem(grip.frame, mouse.x, mouse.y)
            const session = EditorState.masking.editSession
            if (!session.begin(grip.mask.layerId, grip.frame.width, grip.frame.height))
                return
            session.beginResize(grip.modelData.dx, grip.modelData.dy,
                                p.x / Math.max(1, grip.frame.width), p.y / Math.max(1, grip.frame.height))
            grip.overlay.interacting = true
            grip.mask.forceActiveFocus()
        }

        onPositionChanged: (mouse) => {
            if (!pressed)
                return
            const p = mapToItem(grip.frame, mouse.x, mouse.y)
            EditorState.masking.editSession.resize(p.x / Math.max(1, grip.frame.width),
                                                   p.y / Math.max(1, grip.frame.height),
                                                   grip.isCorner && (mouse.modifiers & Qt.ShiftModifier) !== 0)
        }

        onReleased: grip.overlay.endInteraction()
        onCanceled: grip.overlay.endInteraction()
    }
}
