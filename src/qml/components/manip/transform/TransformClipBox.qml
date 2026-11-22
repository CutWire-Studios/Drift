pragma ComponentBehavior: Bound
import QtQuick
import Drift

// One clip's frame on the transform overlay, and every gesture that edits it: move, resize,
// rotate and moving the anchor. The pointer surfaces (the body, TransformGrip, TransformRotate,
// TransformAnchor) only report presses; the maths lives here, in the box's own pose. `manip` owns
// the drag session, the snap lines and the HUD.
Item {
    id: handle

    required property var modelData
    required property int index
    required property var manip

    readonly property Item stage: manip.stage
    // Latest geometry; the model itself is only swapped when the set of clips changes.
    readonly property var box: manip.overlayClips[index] || modelData

    readonly property bool selected: EditorState.selectedTrack === box.track
                                     && EditorState.selectedClip === box.clip
    // A transform layer's own box: the group's frame. Only there while it is selected; the rest of
    // the time picking goes straight through it to the clips it moves.
    readonly property bool isTransform: box.kind === "transform"
    // Moved by a transform layer: drawn and read through that parent.
    readonly property bool hasParent: box.parentActive === true
    readonly property var parentInverse: hasParent
        ? EditorState.preview.parentOverlayMatrix(box, handle.sx).inverted() : null
    // Overlay point (in the parented frame) -> the box's own overlay frame.
    function toParentLocal(p) {
        if (!handle.hasParent)
            return p
        const v = handle.parentInverse.times(Qt.vector4d(p.x, p.y, 0, 1))
        return Qt.point(v.x / v.w, v.y / v.w)
    }
    // Covered by the transform clip that is selected: outlined in its colour.
    readonly property bool coveredBySelectedLayer: {
        for (const p of (box.parents || [])) {
            if (p.track === EditorState.selectedTrack && p.clip === EditorState.selectedClip)
                return true
        }
        return false
    }
    readonly property bool childOfMovingLayer: {
        if (manip.hiddenChildrenOf < 0)
            return false
        for (const p of (box.parents || [])) {
            if (p.track === manip.hiddenChildrenOf)
                return true
        }
        return false
    }
    // A member of the multi-selection: outlined as selected, moved through the group frame.
    readonly property bool inGroup: manip.groupMode && !isTransform
                                    && EditorState.selectionContains(box.track, box.clip)
    // Dragged with the 3D group gizmo: the model is not rebuilt mid-drag, so it would hang behind.
    readonly property bool movedByGroupGizmo: manip.groupDragging && box.layer3d === true && !isTransform
                                              && EditorState.selectionContains(box.track, box.clip)
    visible: (!isTransform || selected) && !childOfMovingLayer && !movedByGroupGizmo
    enabled: visible
    readonly property bool isText: box.kind === "text"
    // The gizmo's live pose while it drags this clip: the model is not rebuilt mid-drag.
    readonly property var gizmoPose: manip.gizmoLive && manip.gizmoLive.track === box.track
                                     && manip.gizmoLive.clip === box.clip ? manip.gizmoLive : null
    readonly property real poseRotX: gizmoPose ? gizmoPose.rotationX : (box.rotationX || 0)
    readonly property real poseRotY: gizmoPose ? gizmoPose.rotationY : (box.rotationY || 0)
    readonly property real poseZ: gizmoPose ? gizmoPose.z : (box.z || 0)
    readonly property real posePerspective: box.perspective || 2000
    // Tipped or pushed in depth: the box is resized about its centre, which is also the tilt pivot,
    // since holding an edge still would drag it through depth.
    readonly property bool tilted: poseRotX !== 0 || poseRotY !== 0 || poseZ !== 0
    // Drawn through the clip's pose matrix, so its outline and grips land on the rendered quad:
    // tilted, or turning about an anchor that is not the centre.
    readonly property bool is3d: tilted || box.anchorX !== undefined || box.anchorY !== undefined
    // A camera reframes every box, flat ones included, so the outline and grips have to go through
    // the pose matrix rather than straight onto the canvas.
    readonly property bool viaCamera: box.cameraActive === true
    readonly property bool usesPose: is3d || hasParent || viaCamera || livePivotX > -1e11

    readonly property real canvasW: Math.max(1, box.canvasWidth)
    readonly property real canvasH: Math.max(1, box.canvasHeight)
    readonly property real sx: parent.width / canvasW
    readonly property real sy: parent.height / canvasH

    // Live overrides applied during a drag so the box tracks the pointer without rebuilding the
    // (stale) model.
    property real liveX: -1e12
    property real liveY: -1e12
    property real liveW: -1
    property real liveH: -1
    property real liveRotation: 1e9
    // Where the pivot sits in the frame, in layout px from its top-left, while a drag moves it.
    property real livePivotX: -1e12
    property real livePivotY: -1e12

    readonly property real layoutX: gizmoPose ? gizmoPose.x : liveX > -1e11 ? liveX : box.x
    readonly property real layoutY: gizmoPose ? gizmoPose.y : liveY > -1e11 ? liveY : box.y
    readonly property real layoutW: gizmoPose ? gizmoPose.width : liveW >= 0 ? liveW : box.width
    readonly property real layoutH: gizmoPose ? gizmoPose.height : liveH >= 0 ? liveH : box.height
    readonly property real layoutRotation: gizmoPose ? gizmoPose.rotation
                                           : liveRotation < 1e8 ? liveRotation : box.rotation
    readonly property real pivotX: livePivotX > -1e11 ? livePivotX : box.positionX - box.x
    readonly property real pivotY: livePivotY > -1e11 ? livePivotY : box.positionY - box.y
    readonly property real centerX: (layoutX + layoutW * 0.5) * sx
    readonly property real centerY: (layoutY + layoutH * 0.5) * sy

    x: layoutX * sx
    y: layoutY * sy
    width: Math.max(24, layoutW * sx)
    height: Math.max(24, layoutH * sy)
    // Front-most track (lowest index) sits on top so it wins click hit-testing over boxes behind
    // it. The clip selected on the timeline is raised above all of them, so a box that lies under
    // a full-frame clip on an upper track is still the one the pointer reaches.
    z: handle.selected ? 900 : -box.track
    transformOrigin: Item.Center
    rotation: usesPose ? 0 : layoutRotation
    transform: Matrix4x4 {
        id: poseTransform
        matrix: (void EditorState.preview.viewRevision, handle.usesPose)
                ? EditorState.preview.clipPoseMatrix({
                                                         "canvasWidth": handle.box.canvasWidth,
                                                         "canvasHeight": handle.box.canvasHeight,
                                                         "rotationX": handle.poseRotX,
                                                         "rotationY": handle.poseRotY,
                                                         "z": handle.poseZ,
                                                         "perspective": handle.posePerspective,
                                                         "anchorX": handle.livePivotX > -1e11 ? handle.livePivotX
                                                                                              : handle.box.anchorX,
                                                         "anchorY": handle.livePivotY > -1e11 ? handle.livePivotY
                                                                                              : handle.box.anchorY,
                                                         "anchorZ": handle.box.anchorZ,
                                                         "parentActive": handle.box.parentActive,
                                                         "parent": handle.box.parent
                                                     }, handle.layoutX, handle.layoutY,
                                                     handle.layoutW, handle.layoutH,
                                                     handle.layoutRotation, handle.sx, handle.sy, handle.manip.viewport)
                : Qt.matrix4x4()
    }

    // Overlay point -> this box's own (untilted) layout px, through the pose as it stands now.
    // Taken at a grab and reused for the whole drag, since the pose follows the drag.
    function overlayToLocalMapper() {
        const m = Qt.matrix4x4(1, 0, 0, handle.x, 0, 1, 0, handle.y, 0, 0, 1, 0, 0, 0, 0, 1)
                .times(poseTransform.matrix).inverted()
        return function(px, py) {
            const v = m.times(Qt.vector4d(px, py, 0, 1))
            return Qt.point(v.x / v.w, v.y / v.w)
        }
    }

    // Layout rect at the grab, for the maths that run from where the box stood.
    property real dragStartX: 0
    property real dragStartY: 0
    property real dragStartW: 1
    property real dragStartH: 1
    // What the pointer is doing to this box, or null: {kind, device, ...values taken at the grab}.
    property var g: null
    readonly property bool resizing: g !== null && g.kind === "resize"
    readonly property bool rotating: g !== null && g.kind === "rotate"
    readonly property bool anchoring: g !== null && g.kind === "anchor"
    readonly property bool moving: g !== null && g.kind === "move"
    readonly property bool busy: g !== null
    // A grip is held: the body handlers stand down so they cannot take the grab from it.
    readonly property bool grabbing: resizing || rotating || anchoring

    // A rotated box has no edges along the screen axes to line up, so a move uses the bounds of its
    // corners and a resize does not snap. Tilts, parents and the camera are fine: the snap works on
    // the projected edges. The 3D view's screen is not the canvas, so there is nothing to snap to.
    readonly property bool canSnapMove: !manip.mode3d
    readonly property bool canSnapResize: Math.abs(layoutRotation) < 0.01 && !manip.mode3d

    // Boxes that exist to be reshaped (text, shapes) resize freely and Shift locks the ratio;
    // footage has an aspect to protect, so its corners hold it and Shift frees them.
    readonly property bool lockedByDefault: manip.freeKinds.indexOf(box.kind) < 0

    readonly property string typedUnit: g && g.kind === "rotate" ? "°" : "px"

    // The box as it stood at the grab, in the shape the snapping calls take.
    function grabBox() {
        return Object.assign({}, handle.box, {
            "x": handle.dragStartX, "y": handle.dragStartY,
            "width": handle.dragStartW, "height": handle.dragStartH,
            "rotation": handle.layoutRotation,
            "rotationX": handle.poseRotX, "rotationY": handle.poseRotY, "z": handle.poseZ
        })
    }

    function scenePoint(scenePos) {
        return stage.mapFromItem(null, scenePos.x, scenePos.y)
    }

    function clearGesture() {
        g = null
        liveX = -1e12
        liveY = -1e12
        liveW = -1
        liveH = -1
        liveRotation = 1e9
        livePivotX = -1e12
        livePivotY = -1e12
    }

    function begin(kind, device, text, p) {
        manip.beginGesture(kind, device, text, handle)
        manip.pointerAt(p)
        dragStartX = box.x
        dragStartY = box.y
        dragStartW = layoutW
        dragStartH = layoutH
        liveX = dragStartX
        liveY = dragStartY
        EditorState.selectClip(box.track, box.clip)
    }

    function showHud(text, p) {
        manip.pointerAt(p)
        EditorState.drag.update(text)
    }

    // ---- move ----------------------------------------------------------------------------------

    function moveBegin(p, device) {
        begin("move", device, qsTr("Move clip"), p)
        g = {
            "kind": "move", "device": device,
            "press": Qt.point(p.x / sx, p.y / sy),
            "startBox": grabBox(),
            "targets": manip.otherTargets(box),
            "last": null
        }
    }

    function moveTo(p, mods) {
        if (!g || g.kind !== "move")
            return
        const m = manip.modifiersFor(mods)
        let nx = p.x / sx
        let ny = p.y / sy
        let lock = ""
        // Shift keeps the move on whichever axis the pointer has gone further along.
        if (Manip.fromQtFlags(mods).constrain) {
            if (Math.abs(nx - g.press.x) >= Math.abs(ny - g.press.y)) {
                ny = g.press.y
                lock = "x"
            } else {
                nx = g.press.x
                lock = "y"
            }
        }
        const moved = EditorState.preview.snapMove(g.startBox, g.press.x, g.press.y, nx, ny,
                                                   Manip.snapTolerancePx / sx, canSnapMove && !m.noSnap,
                                                   manip.viewport,
                                                   { "x": g.targets.x, "y": g.targets.y, "lock": lock })
        // Seen edge-on, the plane has no point under the pointer: hold still.
        if (!moved.valid)
            return
        manip.publishGuides(moved.guideX, moved.guideY, sx, sy)
        liveX = moved.x
        liveY = moved.y
        const posX = moved.x + box.positionX - box.x
        const posY = moved.y + box.positionY - box.y
        g.last = Qt.point(posX, posY)
        EditorState.previewSetClipPosition(box.track, box.clip, posX, posY)
        showHud(qsTr("X %1   Y %2").arg(Math.round(posX)).arg(Math.round(posY)), p)
    }

    function moveTyped(value) {
        const start = g.startBox
        const last = g.last || Qt.point(start.positionX, start.positionY)
        const alongX = Math.abs(last.x - start.positionX) >= Math.abs(last.y - start.positionY)
        const posX = alongX ? value : last.x
        const posY = alongX ? last.y : value
        EditorState.previewSetClipPosition(box.track, box.clip, posX, posY)
    }

    // ---- resize --------------------------------------------------------------------------------

    function resizeBegin(grip, p, device) {
        begin("resize", device, qsTr("Resize clip"), p)
        const throughPose = is3d || viaCamera
        const pp = throughPose ? p : toParentLocal(p)
        const toLocal = throughPose ? overlayToLocalMapper() : null
        g = {
            "kind": "resize", "device": device,
            "dx": grip.dx, "dy": grip.dy, "isCorner": grip.dx !== 0 && grip.dy !== 0,
            "startPx": pp.x, "startPy": pp.y,
            "toLocal": toLocal,
            "startLocal": toLocal ? toLocal(pp.x, pp.y) : Qt.point(0, 0),
            "startBox": grabBox(),
            "targets": manip.otherTargets(box),
            "startPosX": box.positionX, "startPosY": box.positionY,
            "startScaleX": box.scaleX, "startScaleY": box.scaleY,
            "startFrameW": box.frameW, "startFrameH": box.frameH,
            "w": layoutW, "h": layoutH, "fromCenter": false, "locked": false
        }
    }

    function resizeMove(p, mods) {
        if (!g || g.kind !== "resize")
            return
        const m = manip.modifiersFor(mods)
        const aboutPosition = m.fromCenter
        const a = layoutRotation * Math.PI / 180
        let lx = 0
        let ly = 0
        if (g.toLocal) {
            // Through the pose, so the pointer is read in the box's own plane. A tipped box resizes
            // about its centre, so the pointer's travel in the box plane is doubled to keep the grip
            // under it.
            const l = g.toLocal(p.x, p.y)
            const k = tilted || aboutPosition ? 2 : 1
            lx = k * (l.x - g.startLocal.x) / sx
            ly = k * (l.y - g.startLocal.y) / sy
        } else {
            const pp = toParentLocal(p)
            const k = aboutPosition ? 2 : 1
            const ddx = k * (pp.x - g.startPx) / sx
            const ddy = k * (pp.y - g.startPy) / sy
            // Canvas axes -> box axes: the inverse of the rotation the body drag applies.
            lx = ddx * Math.cos(a) + ddy * Math.sin(a)
            ly = -ddx * Math.sin(a) + ddy * Math.cos(a)
        }

        let locked = false
        if (g.isCorner) {
            if (g.device !== DragSession.Mouse && manip.touchBarItem)
                locked = manip.touchBarItem.lockRatio
            else
                locked = lockedByDefault !== Manip.fromQtFlags(mods).constrain
        }

        let w = Math.max(1, dragStartW + lx * g.dx)
        let h = Math.max(1, dragStartH + ly * g.dy)
        if (locked) {
            // The scale comes from whichever axis the pointer moved further along, measured as the
            // bigger departure from the original size so it reads the same growing or shrinking.
            const rw = w / dragStartW
            const rh = h / dragStartH
            const s = Math.abs(rw - 1) >= Math.abs(rh - 1) ? rw : rh
            w = Math.max(1, dragStartW * s)
            h = Math.max(1, dragStartH * s)
        }

        let guideX = -1
        let guideY = -1
        // Only the moving edge sticks; the opposite one is already fixed.
        if (canSnapResize && !m.noSnap) {
            const snapped = EditorState.preview.snapResize(g.startBox, w, h, g.dx, g.dy,
                                                           tilted || aboutPosition,
                                                           Manip.snapTolerancePx / sx, manip.viewport,
                                                           { "x": g.targets.x, "y": g.targets.y })
            if (locked) {
                // The axes are tied, so only the closer of the two snaps wins and it sets the scale
                // for both.
                const rx = snapped.distX >= 0 ? snapped.distX : Infinity
                const ry = snapped.distY >= 0 ? snapped.distY : Infinity
                let s = -1
                if (rx <= ry && rx < Infinity) {
                    s = snapped.width / dragStartW
                    guideX = snapped.guideX
                } else if (ry < Infinity) {
                    s = snapped.height / dragStartH
                    guideY = snapped.guideY
                }
                if (s > 0) {
                    w = Math.max(1, dragStartW * s)
                    h = Math.max(1, dragStartH * s)
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
        manip.publishGuides(guideX, guideY, sx, sy)
        g.locked = locked
        applySize(w, h, aboutPosition)
        showHud(Math.round(w) + "×" + Math.round(h), p)
    }

    // Writes a size reached by the pointer or typed in. The opposite edge or corner stays put unless
    // `aboutPosition` scales about the pivot instead (the pivot is the centre until it is moved).
    function applySize(w, h, aboutPosition) {
        const a = layoutRotation * Math.PI / 180
        const kw = w / dragStartW
        const kh = h / dragStartH
        const offX = g.startPosX - dragStartX
        const offY = g.startPosY - dragStartY
        let posX = g.startPosX
        let posY = g.startPosY
        if (!tilted && !aboutPosition) {
            const oppX = g.dx > 0 ? 0 : g.dx < 0 ? dragStartW : 0
            const oppY = g.dy > 0 ? 0 : g.dy < 0 ? dragStartH : 0
            const newOppX = g.dx > 0 ? 0 : g.dx < 0 ? w : 0
            const newOppY = g.dy > 0 ? 0 : g.dy < 0 ? h : 0
            const ddx = (oppX - offX) - (newOppX - offX * kw)
            const ddy = (oppY - offY) - (newOppY - offY * kh)
            posX += ddx * Math.cos(a) - ddy * Math.sin(a)
            posY += ddx * Math.sin(a) + ddy * Math.cos(a)
        }
        liveX = posX - offX * kw
        liveY = posY - offY * kh
        liveW = w
        liveH = h
        // A pivot that is not the centre is part of the pose, so it has to follow the resize.
        if (box.anchorX !== undefined || box.anchorY !== undefined) {
            livePivotX = offX * kw
            livePivotY = offY * kh
        }
        g.w = w
        g.h = h
        g.fromCenter = aboutPosition
        if (isText && !g.isCorner) {
            // An edge drag re-wraps the text: the box is the wrap width, so the frame changes and the
            // scale stays.
            EditorState.previewSetTextRect(box.track, box.clip, posX, posY,
                                           g.startFrameW * kw, g.startFrameH * kh)
        } else {
            EditorState.previewSetClipPlacement(box.track, box.clip, posX, posY,
                                                g.startScaleX * kw, g.startScaleY * kh)
        }
    }

    function resizeTyped(value) {
        let w = g.w
        let h = g.h
        if (g.dx !== 0) {
            w = Math.max(1, value)
            if (g.dy !== 0 && g.locked)
                h = Math.max(1, dragStartH * w / dragStartW)
        } else {
            h = Math.max(1, value)
        }
        applySize(w, h, g.fromCenter)
    }

    // ---- rotate --------------------------------------------------------------------------------

    function pointerAngle(p, toLocal) {
        if (toLocal) {
            const l = toLocal(p.x, p.y)
            const c = g.pivotLocal
            return Math.atan2(l.y - c.y, l.x - c.x) * 180 / Math.PI + 90
        }
        // Measured in the box's own frame: a parent's turn is not the clip's.
        const q = toParentLocal(p)
        return Math.atan2(q.y - g.pivotStage.y, q.x - g.pivotStage.x) * 180 / Math.PI + 90
    }

    function rotateBegin(p, device) {
        begin("rotate", device, qsTr("Rotate clip"), p)
        const toLocal = viaCamera || is3d ? overlayToLocalMapper() : null
        g = {
            "kind": "rotate", "device": device,
            "toLocal": toLocal, "pivotLocal": Qt.point(pivotX * sx, pivotY * sy),
            "pivotStage": Qt.point(box.positionX * sx, box.positionY * sy),
            "startRotation": box.rotation, "grab": 0, "detent": null
        }
        // Turning starts from where the grip was taken, so grabbing it does not snap the clip to the
        // pointer.
        g.grab = box.rotation - pointerAngle(p, toLocal)
        liveRotation = box.rotation
    }

    function normalizeDeg(deg) {
        let d = deg % 360
        if (d > 180)
            d -= 360
        if (d <= -180)
            d += 360
        return d
    }

    function rotateTo(p, mods) {
        if (!g || g.kind !== "rotate")
            return
        let deg = normalizeDeg(pointerAngle(p, g.toLocal) + g.grab)
        if (Manip.fromQtFlags(mods).constrain) {
            deg = normalizeDeg(Manip.constrainAngle(deg, 15))
        } else if (g.device !== DragSession.Mouse && !manip.modifiersFor(mods).noSnap) {
            // No Shift on a finger: a step pulls the angle in as it passes, with a tick.
            const nearest = normalizeDeg(Manip.constrainAngle(deg, 15))
            if (Math.abs(normalizeDeg(nearest - deg)) <= 4) {
                deg = nearest
                if (g.detent !== nearest)
                    Haptics.detent()
                g.detent = nearest
            } else {
                g.detent = null
            }
        }
        applyRotation(deg)
        showHud(Math.round(deg) + "°", p)
    }

    function applyRotation(deg) {
        liveRotation = deg
        EditorState.previewSetClipRotation(box.track, box.clip, deg)
    }

    function rotateTyped(value) {
        applyRotation(normalizeDeg(value))
    }

    // ---- anchor --------------------------------------------------------------------------------

    function anchorBegin(p, device) {
        begin("anchor", device, qsTr("Move anchor"), p)
        const scaleX = Math.abs(box.scaleX) > 1e-6 ? box.scaleX : 1
        const scaleY = Math.abs(box.scaleY) > 1e-6 ? box.scaleY : 1
        g = {
            "kind": "anchor", "device": device,
            "scaleX": scaleX, "scaleY": scaleY,
            "frameW": box.frameW, "frameH": box.frameH,
            "pos0": Qt.point(box.positionX, box.positionY),
            // The anchor in frame px from the frame's top-left, as the clip stores it.
            "a0": Qt.point((box.positionX - box.x) / scaleX, (box.positionY - box.y) / scaleY),
            "grab": Qt.point(p.x / sx - box.positionX, p.y / sy - box.positionY),
            "last": null, "detent": null
        }
        g.last = g.a0
    }

    // Moves the anchor to `ax`, `ay` (frame px) and the position by what keeps the picture where it
    // is: the clip turns and scales about the anchor, so the position follows it through R * S.
    function applyAnchor(ax, ay) {
        const a = layoutRotation * Math.PI / 180
        const dx = (ax - g.a0.x) * g.scaleX
        const dy = (ay - g.a0.y) * g.scaleY
        const posX = g.pos0.x + dx * Math.cos(a) - dy * Math.sin(a)
        const posY = g.pos0.y + dx * Math.sin(a) + dy * Math.cos(a)
        livePivotX = ax * g.scaleX
        livePivotY = ay * g.scaleY
        liveX = posX - livePivotX
        liveY = posY - livePivotY
        g.last = Qt.point(ax, ay)
        EditorState.previewSetClipAnchor(box.track, box.clip, ax, ay, posX, posY)
    }

    function anchorTo(p, mods) {
        if (!g || g.kind !== "anchor")
            return
        // Where the pivot should land on the canvas, then the anchor that puts it there.
        const qx = p.x / sx - g.grab.x - g.pos0.x
        const qy = p.y / sy - g.grab.y - g.pos0.y
        const a = layoutRotation * Math.PI / 180
        const lx = qx * Math.cos(a) + qy * Math.sin(a)
        const ly = -qx * Math.sin(a) + qy * Math.cos(a)
        let ax = g.a0.x + lx / g.scaleX
        let ay = g.a0.y + ly / g.scaleY
        // Shift lands it on one of the box's nine points: corners, edge middles and centre. A finger
        // has no Shift, so the points pull it in as it passes, with a tick.
        const byFinger = g.device !== DragSession.Mouse
        if (Manip.fromQtFlags(mods).constrain || (byFinger && !manip.modifiersFor(mods).noSnap)) {
            let best = null
            let bestDist = Infinity
            for (const fx of [0, 0.5, 1]) {
                for (const fy of [0, 0.5, 1]) {
                    const px = fx * g.frameW
                    const py = fy * g.frameH
                    const d = Math.hypot((px - ax) * Math.abs(g.scaleX) * sx, (py - ay) * Math.abs(g.scaleY) * sy)
                    if (d < bestDist) {
                        bestDist = d
                        best = Qt.point(px, py)
                    }
                }
            }
            if (best && (!byFinger || bestDist <= Manip.snapTolerancePx * 1.5)) {
                ax = best.x
                ay = best.y
                if (byFinger && (!g.detent || g.detent.x !== best.x || g.detent.y !== best.y))
                    Haptics.detent()
                g.detent = best
            } else {
                g.detent = null
            }
        }
        applyAnchor(ax, ay)
        showHud(qsTr("Anchor %1, %2").arg(Math.round(ax)).arg(Math.round(ay)), p)
    }

    function anchorTyped(value) {
        const alongX = Math.abs(g.last.x - g.a0.x) >= Math.abs(g.last.y - g.a0.y)
        applyAnchor(alongX ? value : g.last.x, alongX ? g.last.y : value)
    }

    // The HUD's typed value, applied to the property the gesture is moving most.
    function applyTyped(value) {
        if (!g)
            return
        if (g.kind === "move")
            moveTyped(value)
        else if (g.kind === "resize")
            resizeTyped(value)
        else if (g.kind === "rotate")
            rotateTyped(value)
        else if (g.kind === "anchor")
            anchorTyped(value)
    }

    // ---- drawing and picking -------------------------------------------------------------------

    Rectangle {
        anchors.fill: parent
        color: "transparent"
        radius: Theme.c.transformOverlayBoxBorder.radius
        border.width: handle.selected || handle.inGroup ? Theme.c.transformOverlayBoxBorder.borderWidthSelected
                                                         : Theme.c.transformOverlayBoxBorder.borderWidth
        border.color: handle.selected || handle.inGroup
                      ? (handle.isTransform ? Theme.c.transformOverlayBoxBorder.borderSelectedTransform
                                            : Theme.c.transformOverlayBoxBorder.borderSelected)
                      : handle.coveredBySelectedLayer ? Theme.c.transformOverlayBoxBorder.borderCovered
                      : hover.hovered ? Theme.c.transformOverlayBoxBorder.borderHover
                      : handle.manip.touch ? Theme.c.androidTransformClipOutline.border : "transparent"

        HoverHandler {
            id: hover
            acceptedDevices: PointerDevice.Mouse | PointerDevice.Stylus
        }

        Behavior on border.width {
            NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
        }
        Behavior on border.color {
            ColorAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
        }
    }

    TapHandler {
        id: boxTap
        property int pressMods: 0
        onPressedChanged: {
            if (pressed)
                boxTap.pressMods = boxTap.point.modifiers
        }
        onTapped: (eventPoint) => {
            handle.manip.forceActiveFocus()
            const additive = !handle.isTransform
                             && ((boxTap.pressMods & (Qt.ShiftModifier | Qt.ControlModifier)) !== 0
                                 || handle.manip.addToSelection)
            if (additive) {
                handle.manip.toggleSelected(handle.box.track, handle.box.clip)
                return
            }
            // Tapping inside a selected layer's frame picks the clip under the pointer, so the
            // group's children stay one tap away.
            if (handle.isTransform) {
                const p = handle.scenePoint(eventPoint.scenePosition)
                const hit = EditorState.preview.clipAtCanvasPoint(p.x / handle.sx, p.y / handle.sy,
                                                                  handle.manip.viewport)
                if (hit && hit.track !== undefined) {
                    EditorState.selectClip(hit.track, hit.clip)
                    Haptics.select()
                    return
                }
            }
            EditorState.selectClip(handle.box.track, handle.box.clip)
            Haptics.select()
            if (tapCount === 2 && handle.isText && !handle.manip.mode3d)
                handle.manip.textEditRequested(handle.box.track, handle.box.clip)
        }
    }

    // "Name · n clips" over a selected layer's frame.
    Rectangle {
        visible: handle.isTransform && handle.selected
        x: 0
        y: -height - Theme.spacingSm
        width: layerLabel.implicitWidth + Theme.spacingLg
        height: layerLabel.implicitHeight + Theme.spacingSm
        radius: Theme.c.transformOverlayLayerChip.radius
        color: Theme.c.transformOverlayLayerChip.background

        Text {
            id: layerLabel
            anchors.centerIn: parent
            text: qsTr("%1 · %n clip(s)", "", handle.box.childCount || 0)
                      .arg(handle.box.name || qsTr("Transform"))
            color: Theme.c.transformOverlayLayerChip.foreground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeXs
        }
    }

    // Only the clip selected on the timeline moves: dragging a box that merely happens to be under
    // the pointer used to shift the wrong clip, so an unselected box is tap-to-select only. Off while
    // a grip is held too: a handler on the parent item can otherwise take the grab from the grip once
    // the drag threshold is passed, turning a resize into a move. One handler per kind of device, so
    // each press says what pressed it.
    DragHandler {
        id: mouseDrag
        target: null
        acceptedDevices: PointerDevice.Mouse | PointerDevice.Stylus | PointerDevice.TouchPad
        enabled: handle.selected && !handle.grabbing && !handle.manip.mode3d && !handle.manip.groupMode
        cursorShape: Qt.SizeAllCursor
        onActiveChanged: {
            if (active)
                handle.moveBegin(handle.scenePoint(centroid.scenePosition), DragSession.Mouse)
            else if (handle.moving)
                handle.manip.endGesture()
        }
        onCentroidChanged: {
            if (active)
                handle.moveTo(handle.scenePoint(centroid.scenePosition), centroid.modifiers)
        }
    }

    DragHandler {
        id: touchDrag
        target: null
        acceptedDevices: PointerDevice.TouchScreen
        maximumPointCount: 1
        enabled: handle.selected && !handle.grabbing && !handle.manip.mode3d && !handle.manip.groupMode
        onActiveChanged: {
            if (active)
                handle.moveBegin(handle.scenePoint(centroid.scenePosition), DragSession.Touch)
            else if (handle.moving)
                handle.manip.endGesture()
        }
        onCentroidChanged: {
            if (active)
                handle.moveTo(handle.scenePoint(centroid.scenePosition), centroid.modifiers)
        }
    }

    TransformRotate {
        clipBox: handle
    }

    // 4 edges then 4 corners, the same frame the crop tool uses. `dx`/`dy` say which edges each grip
    // moves (-1 = left/top, +1 = right/bottom, 0 = stays put); the opposite edge or corner is held.
    Repeater {
        model: (handle.selected && !handle.manip.mode3d && !handle.manip.groupMode)
               ? [
                   { dx: -1, dy:  0 },
                   { dx:  1, dy:  0 },
                   { dx:  0, dy: -1 },
                   { dx:  0, dy:  1 },
                   { dx: -1, dy: -1 },
                   { dx:  1, dy:  1 },
                   { dx:  1, dy: -1 },
                   { dx: -1, dy:  1 }
               ]
               : []

        delegate: TransformGrip {
            clipBox: handle
        }
    }

    TransformAnchor {
        clipBox: handle
    }
}
