import QtQuick
import Drift

// Rotation handle above the box (a 3D layer turns with the gizmo's rings instead).
Item {
    id: rotateHandle

    // The clip box delegate, and the TransformOverlay that owns snap and interaction.
    required property var target
    required property var overlay

    visible: rotateHandle.target.selected && !rotateHandle.overlay.mode3d && !rotateHandle.overlay.groupMode
    width: 14
    height: 14
    x: rotateHandle.target.width / 2 - width / 2
    y: -28

    Rectangle {
        anchors.top: parent.bottom
        anchors.horizontalCenter: parent.horizontalCenter
        width: 1
        height: 16
        color: Theme.c.transformOverlayRotateHandle.background
    }

    Rectangle {
        anchors.fill: parent
        radius: width / 2
        color: Theme.c.transformOverlayRotateHandle.background
        border.width: Theme.c.transformOverlayRotateHandle.borderWidth
        border.color: Theme.c.transformOverlayRotateHandle.border
    }

    DragHandler {
        id: rotateDrag
        target: null
        cursorShape: Qt.CrossCursor

        // Under the camera: the pointer read in the box's own plane, as it stood at
        // the grab.
        property var toLocal: null

        // Angle from the box centre to the pointer, in the same frame
        // as the clip rotation (the grip sits above the box, hence +90).
        function pointerAngle() {
            if (rotateDrag.toLocal) {
                const raw = rotateHandle.overlay.mapFromItem(null, rotateDrag.centroid.scenePosition.x,
                                                             rotateDrag.centroid.scenePosition.y)
                const l = rotateDrag.toLocal(raw.x, raw.y)
                return Math.atan2(l.y - rotateHandle.target.layoutH * rotateHandle.target.sy / 2,
                                  l.x - rotateHandle.target.layoutW * rotateHandle.target.sx / 2) * 180 / Math.PI + 90
            }
            // Measured in the box's own frame: a parent's turn is not the clip's.
            const p = rotateHandle.target.toParentLocal(rotateHandle.overlay.mapFromItem(null, rotateDrag.centroid.scenePosition.x,
                                                                            rotateDrag.centroid.scenePosition.y))
            // A tilted box spins about its projected centre.
            const c = rotateHandle.target.is3d
                    ? rotateHandle.target.toParentLocal(rotateHandle.target.mapToItem(rotateHandle.overlay, rotateHandle.target.layoutW * rotateHandle.target.sx / 2,
                                                                    rotateHandle.target.layoutH * rotateHandle.target.sy / 2))
                    : Qt.point(rotateHandle.target.centerX, rotateHandle.target.centerY)
            const ang = Math.atan2(p.y - c.y, p.x - c.x)
            return ang * 180 / Math.PI + 90
        }

        onActiveChanged: {
            if (active) {
                rotateHandle.overlay.interacting = true
                if (rotateHandle.target.isTransform)
                    rotateHandle.overlay.movingLayerTrack = rotateHandle.target.box.track
                rotateHandle.target.rotating = true
                rotateHandle.target.liveRotation = rotateHandle.target.box.rotation
                rotateDrag.toLocal = rotateHandle.target.viaCamera || rotateHandle.target.is3d
                        ? rotateHandle.target.overlayToLocalMapper() : null
                // Turning starts from where the grip was taken, so
                // grabbing it does not snap the clip to the pointer.
                rotateHandle.target.rotateGrabOffset = rotateHandle.target.box.rotation
                        - rotateDrag.pointerAngle()
                EditorState.selectClip(rotateHandle.target.box.track, rotateHandle.target.box.clip)
                rotateHandle.target.forceActiveFocus()
                EditorState.beginPreviewDrag()
            } else {
                rotateHandle.target.rotating = false
                rotateHandle.target.liveRotation = 1e9
                rotateDrag.toLocal = null
                rotateHandle.overlay.endInteraction()
            }
        }
        onCentroidChanged: {
            if (!active)
                return
            let deg = rotateHandle.overlay.normalizeDeg(rotateDrag.pointerAngle()
                                                        + rotateHandle.target.rotateGrabOffset)
            if (!(rotateDrag.centroid.modifiers & Qt.ControlModifier))
                deg = rotateHandle.overlay.snapAngle(deg)
            rotateHandle.target.liveRotation = deg
            EditorState.previewSetClipRotation(
                rotateHandle.target.box.track,
                rotateHandle.target.box.clip,
                deg)
        }
    }
}
