import QtQuick
import Drift

// Rotation handle above a selected clip, with the stem that joins it to the box.
// `clip` owns the live rotation; `overlay` begins and ends the preview drag.
Item {
    required property var frameItem
    required property var overlay
    id: rotateGrip
    visible: frameItem.selected
    width: overlay.gripTouch
    height: overlay.gripTouch
    x: frameItem.width / 2 - width / 2

    // Above the box, unless that would leave the viewport (the handle would be cut off and
    // ungrabbable): then below it, and inside it when neither side has room.
    readonly property int placement: {
        void frameItem.x
        void frameItem.y
        void frameItem.width
        void frameItem.height
        void frameItem.rotation
        void overlay.stage.x
        void overlay.stage.y
        const half = rotateGrip.height / 2
        const gap = rotateGrip.height + Theme.spacingLg
        const above = frameItem.mapToItem(overlay, 0, -gap + half).y
        if (above - half >= 0)
            return 0
        const below = frameItem.mapToItem(overlay, 0, frameItem.height + Theme.spacingLg + half).y
        return below + half <= overlay.height ? 1 : 2
    }
    y: placement === 0 ? -height - Theme.spacingLg
       : placement === 1 ? frameItem.height + Theme.spacingLg : Theme.spacingLg

    Rectangle {
        visible: rotateGrip.placement !== 2
        y: rotateGrip.placement === 0 ? parent.height / 2 : parent.height / 2 - height
        anchors.horizontalCenter: parent.horizontalCenter
        width: Theme.c.androidTransformRotateHandle.stemWidth
        height: rotateGrip.height / 2 + Theme.spacingLg
        color: Theme.c.androidTransformRotateHandle.stem
    }

    Rectangle {
        anchors.centerIn: parent
        width: overlay.gripSize + 6
        height: width
        radius: width / 2
        color: rotateDrag.active ? Theme.c.androidTransformRotateHandle.backgroundActive
                               : Theme.c.androidTransformRotateHandle.background
        border.width: Theme.c.androidTransformRotateHandle.borderWidth
        border.color: rotateDrag.active ? Theme.c.androidTransformRotateHandle.borderActive
                                        : Theme.c.androidTransformRotateHandle.border
    }

    DragHandler {
        id: rotateDrag
        target: null
        // Under the camera: the finger read in the box's own plane as it stood at the grab.
        property var toLocal: null
        property real startRotation: 0
        onActiveChanged: {
            if (active) {
                overlay.interacting = true
                if (frameItem.isTransform)
                    overlay.movingLayerTrack = frameItem.box.track
                frameItem.liveRotation = frameItem.box.rotation
                rotateDrag.toLocal = frameItem.viaCamera || frameItem.is3d
                        ? frameItem.overlayToLocalMapper() : null
                rotateDrag.startRotation = frameItem.box.rotation
                EditorState.selectClip(frameItem.box.track, frameItem.box.clip)
                frameItem.forceActiveFocus()
                EditorState.beginPreviewDrag()
                Haptics.pickUp()
            } else {
                frameItem.liveRotation = 1e9
                overlay.endInteraction()
                Haptics.drop()
            }
        }
        onCentroidChanged: {
            if (!active)
                return
            let deg = 0
            if (rotateDrag.toLocal) {
                // Under the camera: read in the box's own plane as it stood at the grab,
                // whose frame is already turned by the starting rotation.
                const raw = overlay.stage.mapFromItem(null, rotateDrag.centroid.scenePosition.x,
                                             rotateDrag.centroid.scenePosition.y)
                const l = rotateDrag.toLocal(raw.x, raw.y)
                deg = Math.atan2(l.y - frameItem.layoutH * frameItem.sy / 2,
                                 l.x - frameItem.layoutW * frameItem.sx / 2) * 180 / Math.PI + 90
                      + rotateDrag.startRotation
                deg = ((deg + 180) % 360 + 360) % 360 - 180
            } else {
                const p = frameItem.toParentLocal(overlay.stage.mapFromItem(null, rotateDrag.centroid.scenePosition.x,
                                                                rotateDrag.centroid.scenePosition.y))
                // A tilted box spins about its projected centre.
                const c = frameItem.is3d
                        ? frameItem.toParentLocal(frameItem.mapToItem(overlay, frameItem.layoutW * frameItem.sx / 2,
                                                                frameItem.layoutH * frameItem.sy / 2))
                        : Qt.point(frameItem.centerX, frameItem.centerY)
                deg = Math.atan2(p.y - c.y, p.x - c.x) * 180 / Math.PI + 90
            }
            frameItem.liveRotation = deg
            EditorState.previewSetClipRotation(
                frameItem.box.track,
                frameItem.box.clip,
                deg)
        }
    }
}
