import QtQuick
import Drift

// One edge or corner of a selected frameItem. `dx`/`dy` on modelData say which edges
// this grip moves. `clip` is the overlay delegate that owns the live rect;
// `overlay` is AndroidTransformOverlay (snap tolerance, endInteraction).
Item {
    required property var frameItem
    required property var overlay
    id: grip
    required property var modelData

    // The touch target is the whole delegate; the dot inside it
    // stays small enough not to hide the box edge it sits on.
    width: overlay.gripTouch
    height: overlay.gripTouch

    // Corners drive both axes, edges only one — so an edge drag is
    // the deliberate way to stretch and corners keep the ratio.
    readonly property bool isCorner: modelData.dx !== 0 && modelData.dy !== 0
    // Footage has a real aspect to protect. Boxes that exist to be
    // reshaped (text, subtitles, shapes) scale freely.
    readonly property bool lockRatio: isCorner
            && (frameItem.box.kind === "video"
                || frameItem.box.kind === "image"
                || frameItem.box.kind === "composite"
                || frameItem.isText)

    x: (modelData.dx === 0 ? frameItem.width / 2
                           : (modelData.dx < 0 ? 0 : frameItem.width)) - width / 2
    y: (modelData.dy === 0 ? frameItem.height / 2
                           : (modelData.dy < 0 ? 0 : frameItem.height)) - height / 2

    Rectangle {
        anchors.centerIn: parent
        width: overlay.gripSize
        height: overlay.gripSize
        radius: Theme.c.androidTransformResizeGrip.radius
        color: gripArea.pressed ? Theme.c.androidTransformResizeGrip.backgroundPressed
                                : Theme.c.androidTransformResizeGrip.background
        border.width: Theme.c.androidTransformResizeGrip.borderWidth
        border.color: gripArea.pressed ? Theme.c.androidTransformResizeGrip.borderPressed
                                       : Theme.c.androidTransformResizeGrip.border

        Behavior on color {
            ColorAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
        }
    }

    // Press point in overlay coordinates. The grip rides the box as
    // it resizes, so deltas are measured against the overlay, which
    // stands still.
    property real startPx: 0
    property real startPy: 0
    // Tilted or under the camera: the grab mapped into the box's own layout px.
    property var toLocal: null
    property point startLocal: Qt.point(0, 0)
    property var startBox: null
    // The clip's position (where its anchor lands), scale and frame at the grab.
    property real startPosX: 0
    property real startPosY: 0
    property real startScaleX: 1
    property real startScaleY: 1
    property real startFrameW: 1
    property real startFrameH: 1

    // Resize with the opposite edge or corner held still. The maths runs in the box's
    // own axes, so a rotated clip grows along the direction the grip points; the
    // position shift that keeps the held point still is rotated back to canvas axes at
    // the end. The clip rotates and scales about its position, which sits at a fixed
    // fraction of the box.
    function resizeTo(px, py) {
        const dxSign = grip.modelData.dx
        const dySign = grip.modelData.dy
        const a = frameItem.layoutRotation * Math.PI / 180
        let lx = 0
        let ly = 0
        if (grip.toLocal) {
            // Through the pose, so the finger is read in the box's own plane. A
            // tilted box resizes about its centre, which is also the tilt pivot:
            // holding an edge still would drag it through depth. Doubling the
            // finger's travel in the box plane keeps the grip under the finger.
            const l = grip.toLocal(px, py)
            const k = frameItem.is3d ? 2 : 1
            lx = k * (l.x - grip.startLocal.x) / frameItem.sx
            ly = k * (l.y - grip.startLocal.y) / frameItem.sy
        } else {
            const ddx = (px - grip.startPx) / frameItem.sx
            const ddy = (py - grip.startPy) / frameItem.sy
            // Canvas axes -> box axes: the inverse of the rotation the
            // body drag applies to its translation.
            lx = ddx * Math.cos(a) + ddy * Math.sin(a)
            ly = -ddx * Math.sin(a) + ddy * Math.cos(a)
        }

        const locked = grip.lockRatio

        let w = Math.max(1, frameItem.dragStartW + lx * dxSign)
        let h = Math.max(1, frameItem.dragStartH + ly * dySign)
        if (locked) {
            // Take the scale from whichever axis the finger moved
            // further along, measured as the bigger departure from
            // the original size so it reads the same growing or
            // shrinking.
            const rw = w / frameItem.dragStartW
            const rh = h / frameItem.dragStartH
            const s = Math.abs(rw - 1) >= Math.abs(rh - 1) ? rw : rh
            w = Math.max(1, frameItem.dragStartW * s)
            h = Math.max(1, frameItem.dragStartH * s)
        }

        // Only the moving edge sticks; the anchor is already fixed.
        let guideX = -1
        let guideY = -1
        if (frameItem.canSnap) {
            // Each moving edge is snapped as it projects, through the pose,
            // parents and camera.
            const snapped = EditorState.preview.snapResize(grip.startBox, w, h, dxSign, dySign,
                                                          frameItem.is3d,
                                                          overlay.snapTolPx / frameItem.sx)
            if (locked) {
                // The axes are tied, so only the closer of the two
                // snaps wins and it sets the scale for both.
                const rx = snapped.distX >= 0 ? snapped.distX : Infinity
                const ry = snapped.distY >= 0 ? snapped.distY : Infinity
                let s = -1
                if (rx <= ry && rx < Infinity) {
                    s = snapped.width / frameItem.dragStartW
                    guideX = snapped.guideX
                } else if (ry < Infinity) {
                    s = snapped.height / frameItem.dragStartH
                    guideY = snapped.guideY
                }
                if (s > 0) {
                    w = Math.max(1, frameItem.dragStartW * s)
                    h = Math.max(1, frameItem.dragStartH * s)
                }
            } else {
                if (snapped.distX >= 0) {
                    w = snapped.width
                    guideX = snapped.guideX
                }
                if (snapped.distY >= 0) {
                    h = snapped.height
                    guideY = snapped.guideY
                }
            }
        }
        frameItem.publishGuides(guideX, guideY)

        const kw = w / frameItem.dragStartW
        const kh = h / frameItem.dragStartH
        const offX = grip.startPosX - frameItem.dragStartX
        const offY = grip.startPosY - frameItem.dragStartY
        let posX = grip.startPosX
        let posY = grip.startPosY
        if (!frameItem.is3d) {
            const oppX = dxSign > 0 ? 0 : dxSign < 0 ? frameItem.dragStartW : 0
            const oppY = dySign > 0 ? 0 : dySign < 0 ? frameItem.dragStartH : 0
            const newOppX = dxSign > 0 ? 0 : dxSign < 0 ? w : 0
            const newOppY = dySign > 0 ? 0 : dySign < 0 ? h : 0
            const ddx = (oppX - offX) - (newOppX - offX * kw)
            const ddy = (oppY - offY) - (newOppY - offY * kh)
            posX += ddx * Math.cos(a) - ddy * Math.sin(a)
            posY += ddx * Math.sin(a) + ddy * Math.cos(a)
        }

        frameItem.liveX = posX - offX * kw
        frameItem.liveY = posY - offY * kh
        frameItem.liveW = w
        frameItem.liveH = h
        if (frameItem.isText && !grip.isCorner) {
            // An edge drag re-wraps the text: the box is the wrap width, so the frame changes
            // and the scale stays.
            EditorState.previewSetTextRect(
                frameItem.box.track,
                frameItem.box.clip,
                posX, posY, grip.startFrameW * kw, grip.startFrameH * kh)
        } else {
            EditorState.previewSetClipPlacement(
                frameItem.box.track,
                frameItem.box.clip,
                posX, posY, grip.startScaleX * kw, grip.startScaleY * kh)
        }
    }

    MouseArea {
        id: gripArea
        anchors.fill: parent
        // The delegate's body DragHandler would otherwise take the
        // grab once the drag threshold is passed, turning a resize
        // into a move.
        preventStealing: true

        onPressed: (mouse) => {
            const raw = mapToItem(overlay, mouse.x, mouse.y)
            const throughPose = frameItem.is3d || frameItem.viaCamera
            const p = throughPose ? raw : frameItem.toParentLocal(raw)
            grip.startPx = p.x
            grip.startPy = p.y
            grip.toLocal = throughPose ? frameItem.overlayToLocalMapper() : null
            if (grip.toLocal)
                grip.startLocal = grip.toLocal(p.x, p.y)
            if (frameItem.isTransform)
                overlay.movingLayerTrack = frameItem.box.track
            frameItem.dragStartX = frameItem.layoutX
            frameItem.dragStartY = frameItem.layoutY
            frameItem.dragStartW = frameItem.layoutW
            frameItem.dragStartH = frameItem.layoutH
            grip.startPosX = frameItem.box.positionX
            grip.startPosY = frameItem.box.positionY
            grip.startScaleX = frameItem.box.scaleX
            grip.startScaleY = frameItem.box.scaleY
            grip.startFrameW = frameItem.box.frameW
            grip.startFrameH = frameItem.box.frameH
            grip.startBox = frameItem.grabBox()
            frameItem.liveX = frameItem.dragStartX
            frameItem.liveY = frameItem.dragStartY
            frameItem.liveW = frameItem.dragStartW
            frameItem.liveH = frameItem.dragStartH
            frameItem.resizing = true
            overlay.interacting = true
            EditorState.selectClip(frameItem.box.track, frameItem.box.clip)
            frameItem.forceActiveFocus()
            EditorState.beginPreviewDrag()
            Haptics.pickUp()
        }

        onPositionChanged: (mouse) => {
            if (!pressed)
                return
            const raw = mapToItem(overlay, mouse.x, mouse.y)
            const p = grip.toLocal ? raw : frameItem.toParentLocal(raw)
            grip.resizeTo(p.x, p.y)
        }

        onReleased: grip.finishResize()
        onCanceled: grip.finishResize()
    }

    function finishResize() {
        if (!frameItem.resizing)
            return
        frameItem.resizing = false
        frameItem.liveX = -1e12
        frameItem.liveY = -1e12
        frameItem.liveW = -1
        frameItem.liveH = -1
        overlay.endInteraction()
        Haptics.drop()
    }
}
