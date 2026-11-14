pragma ComponentBehavior: Bound
import QtQuick
import Drift

// One resize grip on a clip box. dx/dy say which edges it moves
// (-1 = left/top, +1 = right/bottom, 0 = stays put). The opposite
// edge or corner is the anchor and does not move.
Item {
    id: grip

    required property var modelData
    // The clip box delegate, and the TransformOverlay that owns snap and interaction.
    required property var target
    required property var overlay

    readonly property real hs: Theme.spacingLg
    // Corners drive both axes, edges only one — so an edge drag
    // is the deliberate way to stretch and corners are free to
    // keep the ratio.
    readonly property bool isCorner: modelData.dx !== 0 && modelData.dy !== 0
    // Footage has a real aspect to protect, so its corners hold
    // the ratio unless Shift asks for a stretch. Boxes that exist
    // to be reshaped (text, subtitles, shapes) work the other way.
    readonly property bool lockByDefault: grip.target.box.kind === "video"
                                          || grip.target.box.kind === "image"
                                          || grip.target.box.kind === "composite"
                                          || grip.target.isText

    width: hs
    height: hs

    ThemeSlot {
        anchors.fill: parent
        slot: "TransformOverlayResizeGrip"
        control: grip
        fallback: Component {
            Rectangle {
                anchors.fill: parent
                radius: Theme.c.transformOverlayResizeGrip.radius
                color: gripArea.containsMouse || gripArea.pressed
                       ? Theme.c.transformOverlayResizeGrip.backgroundHover
                       : Theme.c.transformOverlayResizeGrip.background
                border.width: Theme.c.transformOverlayResizeGrip.borderWidth
                border.color: gripArea.containsMouse || gripArea.pressed
                              ? Theme.c.transformOverlayResizeGrip.borderHover
                              : Theme.c.transformOverlayResizeGrip.border

                Behavior on color {
                    ColorAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
                }
            }
        }
    }

    x: (modelData.dx === 0 ? grip.target.width / 2
                           : (modelData.dx < 0 ? 0 : grip.target.width)) - hs / 2
    y: (modelData.dy === 0 ? grip.target.height / 2
                           : (modelData.dy < 0 ? 0 : grip.target.height)) - hs / 2

    // Press point in overlay coordinates. The grip rides the box
    // as it resizes, so deltas are measured against the overlay,
    // which stands still.
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
    // fraction of the box. Alt scales about the position instead.
    function resizeTo(px, py, modifiers) {
        const dxSign = grip.modelData.dx
        const dySign = grip.modelData.dy
        const a = grip.target.layoutRotation * Math.PI / 180
        const aboutPosition = (modifiers & Qt.AltModifier) !== 0
        let lx = 0
        let ly = 0
        if (grip.toLocal) {
            // Through the pose, so the pointer is read in the box's own plane. A
            // tilted box resizes about its centre, which is also the tilt pivot:
            // holding an edge still would drag it through depth. Doubling the
            // pointer's travel in the box plane keeps the grip under the pointer.
            const l = grip.toLocal(px, py)
            const k = grip.target.is3d || aboutPosition ? 2 : 1
            lx = k * (l.x - grip.startLocal.x) / grip.target.sx
            ly = k * (l.y - grip.startLocal.y) / grip.target.sy
        } else {
            const k = aboutPosition ? 2 : 1
            const ddx = k * (px - grip.startPx) / grip.target.sx
            const ddy = k * (py - grip.startPy) / grip.target.sy
            // Canvas axes -> box axes: the inverse of the rotation the
            // body drag applies to its translation.
            lx = ddx * Math.cos(a) + ddy * Math.sin(a)
            ly = -ddx * Math.sin(a) + ddy * Math.cos(a)
        }

        const shift = (modifiers & Qt.ShiftModifier) !== 0
        const locked = grip.isCorner && (grip.lockByDefault !== shift)

        let w = Math.max(1, grip.target.dragStartW + lx * dxSign)
        let h = Math.max(1, grip.target.dragStartH + ly * dySign)
        if (locked) {
            // Take the scale from whichever axis the pointer moved
            // further along, measured as the bigger departure from
            // the original size so it reads the same growing or
            // shrinking.
            const rw = w / grip.target.dragStartW
            const rh = h / grip.target.dragStartH
            const s = Math.abs(rw - 1) >= Math.abs(rh - 1) ? rw : rh
            w = Math.max(1, grip.target.dragStartW * s)
            h = Math.max(1, grip.target.dragStartH * s)
        }

        // Only the moving edge sticks; the anchor is already fixed.
        let guideX = -1
        let guideY = -1
        if (grip.target.canSnap && !(modifiers & Qt.ControlModifier)) {
            // Each moving edge is snapped as it projects, through the pose,
            // parents and camera.
            const snapped = EditorState.preview.snapResize(grip.startBox, w, h, dxSign, dySign,
                                                          grip.target.is3d,
                                                          grip.overlay.snapTolPx / grip.target.sx)
            if (locked) {
                // The axes are tied, so only the closer of the two
                // snaps wins and it sets the scale for both.
                const rx = snapped.distX >= 0 ? snapped.distX : Infinity
                const ry = snapped.distY >= 0 ? snapped.distY : Infinity
                let s = -1
                if (rx <= ry && rx < Infinity) {
                    s = snapped.width / grip.target.dragStartW
                    guideX = snapped.guideX
                } else if (ry < Infinity) {
                    s = snapped.height / grip.target.dragStartH
                    guideY = snapped.guideY
                }
                if (s > 0) {
                    w = Math.max(1, grip.target.dragStartW * s)
                    h = Math.max(1, grip.target.dragStartH * s)
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
        grip.target.publishGuides(guideX, guideY)

        const kw = w / grip.target.dragStartW
        const kh = h / grip.target.dragStartH
        const offX = grip.startPosX - grip.target.dragStartX
        const offY = grip.startPosY - grip.target.dragStartY
        // Scaling about the position leaves it where it is; otherwise the position moves so
        // the edge or corner opposite the grip stays put.
        let posX = grip.startPosX
        let posY = grip.startPosY
        if (!grip.target.is3d && !aboutPosition) {
            const oppX = dxSign > 0 ? 0 : dxSign < 0 ? grip.target.dragStartW : 0
            const oppY = dySign > 0 ? 0 : dySign < 0 ? grip.target.dragStartH : 0
            const newOppX = dxSign > 0 ? 0 : dxSign < 0 ? w : 0
            const newOppY = dySign > 0 ? 0 : dySign < 0 ? h : 0
            const ddx = (oppX - offX) - (newOppX - offX * kw)
            const ddy = (oppY - offY) - (newOppY - offY * kh)
            posX += ddx * Math.cos(a) - ddy * Math.sin(a)
            posY += ddx * Math.sin(a) + ddy * Math.cos(a)
        }

        grip.target.liveX = posX - offX * kw
        grip.target.liveY = posY - offY * kh
        grip.target.liveW = w
        grip.target.liveH = h
        if (grip.target.isText && !grip.isCorner) {
            // An edge drag re-wraps the text: the box is the wrap width, so the frame changes
            // and the scale stays.
            EditorState.previewSetTextRect(
                grip.target.box.track,
                grip.target.box.clip,
                posX, posY, grip.startFrameW * kw, grip.startFrameH * kh)
        } else {
            EditorState.previewSetClipPlacement(
                grip.target.box.track,
                grip.target.box.clip,
                posX, posY, grip.startScaleX * kw, grip.startScaleY * kh)
        }
    }

    MouseArea {
        id: gripArea
        anchors.fill: parent
        // Generous invisible margin: the visible dot stays small
        // enough not to hide the box edge it sits on.
        anchors.margins: -Theme.spacingMd
        hoverEnabled: true
        cursorShape: grip.modelData.cursor
        // The delegate's body DragHandler would otherwise take the
        // grab once the drag threshold is passed, turning a resize
        // into a move.
        preventStealing: true

        // Zooming with the pointer on a grip should still zoom.
        onWheel: (wheel) => { wheel.accepted = false }

        onPressed: (mouse) => {
            const raw = mapToItem(grip.overlay, mouse.x, mouse.y)
            const throughPose = grip.target.is3d || grip.target.viaCamera
            const p = throughPose ? raw : grip.target.toParentLocal(raw)
            grip.startPx = p.x
            grip.startPy = p.y
            grip.toLocal = throughPose ? grip.target.overlayToLocalMapper() : null
            if (grip.toLocal)
                grip.startLocal = grip.toLocal(p.x, p.y)
            if (grip.target.isTransform)
                grip.overlay.movingLayerTrack = grip.target.box.track
            grip.target.dragStartX = grip.target.layoutX
            grip.target.dragStartY = grip.target.layoutY
            grip.target.dragStartW = grip.target.layoutW
            grip.target.dragStartH = grip.target.layoutH
            grip.startPosX = grip.target.box.positionX
            grip.startPosY = grip.target.box.positionY
            grip.startScaleX = grip.target.box.scaleX
            grip.startScaleY = grip.target.box.scaleY
            grip.startFrameW = grip.target.box.frameW
            grip.startFrameH = grip.target.box.frameH
            grip.startBox = grip.target.grabBox()
            grip.target.liveX = grip.target.dragStartX
            grip.target.liveY = grip.target.dragStartY
            grip.target.liveW = grip.target.dragStartW
            grip.target.liveH = grip.target.dragStartH
            grip.target.resizing = true
            grip.overlay.interacting = true
            EditorState.selectClip(grip.target.box.track, grip.target.box.clip)
            grip.target.forceActiveFocus()
            EditorState.beginPreviewDrag()
        }

        onPositionChanged: (mouse) => {
            if (!pressed)
                return
            const raw = mapToItem(grip.overlay, mouse.x, mouse.y)
            const p = grip.toLocal ? raw : grip.target.toParentLocal(raw)
            grip.resizeTo(p.x, p.y, mouse.modifiers)
        }

        onReleased: grip.finishResize()
        onCanceled: grip.finishResize()
    }

    function finishResize() {
        if (!grip.target.resizing)
            return
        grip.target.resizing = false
        grip.target.liveX = -1e12
        grip.target.liveY = -1e12
        grip.target.liveW = -1
        grip.target.liveH = -1
        grip.overlay.endInteraction()
    }
}
