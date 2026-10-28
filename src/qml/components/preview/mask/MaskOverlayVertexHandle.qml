import QtQuick
import Drift

// One polygon vertex. Drawn in the clip frame rather than the mask box, because the
// box is only their bounding rect — dragging a vertex changes that rect, which
// would otherwise move every other vertex under the cursor.
Rectangle {
    id: vertex

    required property int index
    required property var mask
    required property var frame
    required property var overlay

    // Deliberately larger than the resize grips: a vertex is a point target
    // rather than an edge to catch, and it is the only way to reshape a
    // freeform, so it has to be easy to hit.
    readonly property real vs: Theme.spacingLg * 1.25
    width: vs
    height: vs
    radius: vs / 2
    color: vertexArea.containsMouse || vertexArea.pressed
           ? Theme.c.maskOverlayVertexHandle.backgroundHover : Theme.c.maskOverlayVertexHandle.background
    border.width: Theme.c.maskOverlayVertexHandle.borderWidth
    border.color: Theme.c.maskOverlayVertexHandle.border

    readonly property var pt: vertex.mask.displayPoints[vertex.index]
    readonly property real vx: pt ? pt.x : 0
    readonly property real vy: pt ? pt.y : 0

    x: vx * vertex.frame.width - vs / 2
    y: vy * vertex.frame.height - vs / 2
    z: 200

    // The removal rebuilds the vertex Repeater, which would destroy this delegate while its
    // MouseArea is still inside the handler that asked for it.
    function removeDeferred() {
        const id = vertex.mask.layerId
        const i = vertex.index
        Qt.callLater(() => EditorState.masking.editSession.removePoint(id, i))
    }

    MouseArea {
        id: vertexArea
        anchors.fill: parent
        anchors.margins: -Theme.spacingMd
        hoverEnabled: true
        cursorShape: Qt.SizeAllCursor
        preventStealing: true
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        onWheel: (wheel) => { wheel.accepted = false }

        property bool pressedByTouch: false
        property bool dragging: false
        property bool moved: false

        onPressed: (mouse) => {
            pressedByTouch = mouse.source !== Qt.MouseEventNotSynthesized
            if (mouse.button === Qt.RightButton) {
                vertex.removeDeferred()
                return
            }
            if (!EditorState.masking.editSession.begin(vertex.mask.layerId, vertex.frame.width,
                                                       vertex.frame.height))
                return
            dragging = true
            moved = false
            vertex.overlay.interacting = true
            vertex.mask.forceActiveFocus()
        }

        onPositionChanged: (mouse) => {
            if (!pressed || !dragging || mouse.buttons !== Qt.LeftButton)
                return
            moved = true
            const p = mapToItem(vertex.frame, mouse.x, mouse.y)
            EditorState.masking.editSession.movePoint(vertex.index,
                                                      p.x / Math.max(1, vertex.frame.width),
                                                      p.y / Math.max(1, vertex.frame.height))
        }

        function finish() {
            if (!dragging)
                return
            dragging = false
            vertex.overlay.endInteraction()
        }

        onReleased: finish()
        onCanceled: finish()

        // Touch has no right button: hold a vertex still to delete it.
        onPressAndHold: (mouse) => {
            if (!pressedByTouch || !dragging || moved || !(mouse.buttons & Qt.LeftButton))
                return
            dragging = false
            EditorState.masking.editSession.cancel()
            vertex.overlay.interacting = false
            vertex.removeDeferred()
        }
    }
}
