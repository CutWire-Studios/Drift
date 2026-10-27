import QtQuick
import Drift

// One polygon vertex. Drawn in the clip frame rather than the mask box, because the
// box is only their bounding rect — dragging a vertex changes that rect, which
// would otherwise move every other vertex under the cursor.
Rectangle {
    id: vertex

    required property var modelData
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

    // Live position during a drag, so the dot tracks the cursor without
    // waiting for the model to come back.
    property real liveVx: -1e12
    property real liveVy: -1e12
    readonly property real vx: liveVx > -1e11 ? liveVx : modelData.x
    readonly property real vy: liveVy > -1e11 ? liveVy : modelData.y

    x: vx * vertex.frame.width - vs / 2
    y: vy * vertex.frame.height - vs / 2
    z: 200

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

        onPressed: (mouse) => {
            pressedByTouch = mouse.source !== Qt.MouseEventNotSynthesized
            if (mouse.button === Qt.RightButton) {
                EditorState.removeMaskPoint(vertex.mask.layerTrack,
                                            vertex.mask.layerClip, vertex.index)
                return
            }
            vertex.overlay.interacting = true
            vertex.mask.forceActiveFocus()
            EditorState.beginPreviewDrag(qsTr("Mask changed"))
        }

        onPositionChanged: (mouse) => {
            if (!pressed || mouse.buttons !== Qt.LeftButton)
                return
            const p = mapToItem(vertex.frame, mouse.x, mouse.y)
            vertex.liveVx = p.x / Math.max(1, vertex.frame.width)
            vertex.liveVy = p.y / Math.max(1, vertex.frame.height)
            vertex.mask.writePointAt(vertex.index, vertex.liveVx, vertex.liveVy)
        }

        function finish() {
            if (vertex.liveVx < -1e11)
                return
            vertex.liveVx = -1e12
            vertex.liveVy = -1e12
            vertex.overlay.endInteraction()
        }

        onReleased: finish()
        onCanceled: finish()

        // Touch has no right button: hold a vertex still to delete it.
        onPressAndHold: (mouse) => {
            if (!pressedByTouch || vertex.liveVx > -1e11
                    || !(mouse.buttons & Qt.LeftButton))
                return
            EditorState.cancelPreviewDrag()
            vertex.overlay.interacting = false
            EditorState.removeMaskPoint(vertex.mask.layerTrack,
                                        vertex.mask.layerClip, vertex.index)
        }
    }
}
