import QtQuick
import Drift

// Transform overlay: resize/rotate grips for the clips visible at the playhead.
// Sits outside the (clipped) canvas rect,
// mirroring its geometry, so grips on a clip that runs past a canvas edge stay
// drawn and grabbable instead of being cut away with the frame. Geometry
// and visibility are driven by PreviewToolHost.
Item {
    id: root

    property var overlayClips: []
    // True while a handle is being dragged. Rebuilding the model
    // mid-drag would destroy the delegate that owns the active
    // grab, so refreshes are suppressed until the drag ends.
    property bool interacting: false

    // A text clip was double-clicked: the panel opens its inline editor.
    signal textEditRequested(int track, int clip)

    // In 3D mode the scene camera, as a gizmo box at its eye (see previewCameraBox).
    property var cameraBox: null

    // The two views edit differently: the camera output takes the flat grips (move, resize,
    // rotate) on every clip, the 3D scene only the 3D gizmo, and only on 3D layers and the camera.
    readonly property bool mode3d: EditorState.preview.mode === "3d"

    // The selected clip's gizmo, when it is a 3D layer, and its pose while a handle is dragged.
    readonly property var gizmoBox: {
        if (!mode3d)
            return null
        if (group3dBoxes.length >= 2)
            return groupPivot
        if (cameraBox && cameraBox.track === EditorState.selectedTrack
                && cameraBox.clip === EditorState.selectedClip)
            return cameraBox
        for (const b of overlayClips) {
            if (b.layer3d
                    && b.track === EditorState.selectedTrack
                    && b.clip === EditorState.selectedClip)
                return b
        }
        return null
    }
    readonly property var gizmoLive: gizmo.livePose

    // Several 3D layers selected: one gizmo at their centroid moves, turns and scales them together.
    // Global axes only, since a group has no frame of its own to be "local" to. Cameras and lights
    // are not members.
    readonly property var group3dBoxes: {
        void EditorState.selectionRevision
        if (!mode3d)
            return []
        const out = []
        for (const b of overlayClips) {
            if (b.layer3d && b.kind !== "transform" && b.kind !== "camera"
                    && EditorState.selectionContains(b.track, b.clip))
                out.push(b)
        }
        return out
    }
    readonly property var groupPivot: group3dBoxes.length >= 2
        ? EditorState.preview.groupGizmoPivot(group3dBoxes) : null
    readonly property bool groupDragging: !!gizmoLive && !!gizmoBox && gizmoBox.kind === "group"

    // A transform layer being moved: the clips under it are drawn from a model that is not
    // rebuilt mid-drag, so their outlines would hang in the old place and are hidden meanwhile.
    property int movingLayerTrack: -1
    readonly property int hiddenChildrenOf: movingLayerTrack >= 0 ? movingLayerTrack
        : (gizmoLive && gizmoBox && gizmoBox.kind === "transform" ? gizmoBox.track : -1)

    // Several clips selected in the flat view: one frame moves, scales and turns them together.
    // The transform layers' own boxes are not members; their children are picked individually.
    readonly property var groupBoxes: {
        void EditorState.selectionRevision
        if (mode3d)
            return []
        const out = []
        for (const b of overlayClips) {
            if (b.kind !== "transform" && EditorState.selectionContains(b.track, b.clip))
                out.push(b)
        }
        return out
    }
    readonly property bool groupMode: groupBoxes.length >= 2

    // The selected clip's box, and the transform clip over it (innermost) when there is one.
    readonly property var selectedBox: {
        for (const b of overlayClips) {
            if (b.track === EditorState.selectedTrack && b.clip === EditorState.selectedClip)
                return b
        }
        return null
    }
    readonly property var parentFrameBox: {
        const sel = selectedBox
        if (!sel || sel.kind === "transform" || !sel.parents || sel.parents.length === 0)
            return null
        const p = sel.parents[0]
        for (const b of overlayClips) {
            if (b.kind === "transform" && b.track === p.track && b.clip === p.clip)
                return b
        }
        return null
    }

    // True when two overlay models describe the same boxes. Text/name
    // updates still reach delegates via liveStyle, so rebuilding
    // for every keystroke is unnecessary — and recreating a selected
    // handle with focus:true steals focus from the properties panel.
    function clipsOverlayEqual(a, b) {
        if (!a || !b || a.length !== b.length)
            return false
        for (let i = 0; i < a.length; ++i) {
            const x = a[i]
            const y = b[i]
            if (x.track !== y.track || x.clip !== y.clip || x.kind !== y.kind
                    || x.x !== y.x || x.y !== y.y
                    || x.width !== y.width || x.height !== y.height
                    || x.rotation !== y.rotation
                    || x.layer3d !== y.layer3d
                    || x.cameraActive !== y.cameraActive
                    || x.rotationX !== y.rotationX || x.rotationY !== y.rotationY
                    || x.z !== y.z || x.perspective !== y.perspective
                    || x.canvasWidth !== y.canvasWidth
                    || x.canvasHeight !== y.canvasHeight
                    || x.parentSig !== y.parentSig)
                return false
        }
        return true
    }

    function sameOverlayClips(a, b) {
        if (!a || !b || a.length !== b.length)
            return false
        for (let i = 0; i < a.length; ++i) {
            if (a[i].track !== b[i].track || a[i].clip !== b[i].clip || a[i].kind !== b[i].kind)
                return false
        }
        return true
    }

    function refreshOverlay() {
        if (interacting)
            return
        // Playhead ticks ~60 Hz; rebuilding grips every tick during playback
        // is wasted work and stalls the UI on long timelines.
        if (EditorState.playing)
            return
        const camera = EditorState.preview.cameraBox()
        cameraBox = camera && camera.kind === "camera" ? camera : null
        const next = EditorState.preview.clipsAtPlayhead()
        if (clipsOverlayEqual(overlayClips, next))
            return
        const sameClips = sameOverlayClips(overlayClips, next)
        overlayClips = next
        // Only geometry moved (a slider or another handle dragging this clip): the delegates
        // follow through `box`, and recreating them would drop a selected handle's focus.
        if (sameClips)
            return
        // Set model imperatively. Binding `model: overlayClips` re-enters when
        // tracksChanged fires during delegate setup (binding loop on model).
        clipRepeater.model = next
    }

    // Marquee on empty canvas, in overlay px.
    property bool marqueeActive: false
    property rect marqueeRect: Qt.rect(0, 0, 0, 0)

    function pointInQuad(q, x, y) {
        let sign = 0
        for (let i = 0; i < 4; ++i) {
            const a = q[i]
            const b = q[(i + 1) % 4]
            const cross = (b.x - a.x) * (y - a.y) - (b.y - a.y) * (x - a.x)
            if (cross !== 0) {
                if (sign !== 0 && (cross > 0) !== (sign > 0))
                    return false
                sign = cross
            }
        }
        return true
    }

    // True when a press at this canvas point lands on a clip box or the group frame, which own it.
    function pressHitsBox(cx, cy) {
        if (groupMode) {
            for (const b of groupBoxes) {
                if (b.quad && b.quad.length === 4 && pointInQuad(b.quad, cx, cy))
                    return true
            }
        }
        for (const b of overlayClips) {
            if (b.kind === "transform" && !(b.track === EditorState.selectedTrack && b.clip === EditorState.selectedClip))
                continue
            if (b.quad && b.quad.length === 4 && pointInQuad(b.quad, cx, cy))
                return true
        }
        return false
    }

    function quadHitsRect(q, r) {
        let l = Infinity, t = Infinity, rr = -Infinity, bb = -Infinity
        for (const p of q) {
            l = Math.min(l, p.x); rr = Math.max(rr, p.x)
            t = Math.min(t, p.y); bb = Math.max(bb, p.y)
        }
        if (rr < r.x || l > r.x + r.width || bb < r.y || t > r.y + r.height)
            return false
        // Bounding boxes meet: a corner of either shape inside the other, or crossing edges.
        const rc = [{x: r.x, y: r.y}, {x: r.x + r.width, y: r.y},
                    {x: r.x + r.width, y: r.y + r.height}, {x: r.x, y: r.y + r.height}]
        for (const p of q) {
            if (p.x >= r.x && p.x <= r.x + r.width && p.y >= r.y && p.y <= r.y + r.height)
                return true
        }
        for (const p of rc) {
            if (pointInQuad(q, p.x, p.y))
                return true
        }
        const cross = (a, b, c) => (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x)
        for (let i = 0; i < 4; ++i) {
            const a = q[i], b = q[(i + 1) % 4]
            for (let j = 0; j < 4; ++j) {
                const c = rc[j], d = rc[(j + 1) % 4]
                if (cross(a, b, c) * cross(a, b, d) < 0 && cross(c, d, a) * cross(c, d, b) < 0)
                    return true
            }
        }
        return false
    }

    function finishMarquee(additive) {
        const pairs = []
        const r = Qt.rect(marqueeRect.x, marqueeRect.y, marqueeRect.width, marqueeRect.height)
        for (const b of overlayClips) {
            if (b.kind === "transform" || !b.quad || b.quad.length !== 4)
                continue
            const sx = width / Math.max(1, b.canvasWidth)
            const sy = height / Math.max(1, b.canvasHeight)
            const cr = Qt.rect(r.x / sx, r.y / sy, r.width / sx, r.height / sy)
            if (quadHitsRect(b.quad, cr))
                pairs.push({ track: b.track, clip: b.clip })
        }
        if (additive) {
            for (const p of EditorState.selection) {
                if (!pairs.some(q => q.track === p.track && q.clip === p.clip))
                    pairs.unshift(p)
            }
        }
        if (pairs.length > 0)
            EditorState.setSelection(pairs)
        else if (!additive)
            EditorState.setSelection([])
    }

    // Shift or Ctrl+click: the clip joins the selection, or leaves it when it is already in.
    function toggleSelected(track, clip) {
        if (!EditorState.selectionContains(track, clip)) {
            EditorState.addToSelection(track, clip)
            return
        }
        const rest = EditorState.selection.filter(p => !(p.track === track && p.clip === clip))
        EditorState.setSelection(rest)
    }

    function endInteraction() {
        EditorState.commitPreviewDrag()
        interacting = false
        movingLayerTrack = -1
        snapGuideX = -1
        snapGuideY = -1
        Qt.callLater(refreshOverlay)
    }

    // Stickiness: while a clip is moved or resized its edges and centre pull to
    // the canvas edges, centre lines and visible guides. The tolerance is a screen distance, so
    // the pull feels the same whatever the project resolution or preview zoom.
    readonly property real snapTolPx: 8

    // Engaged guide lines, in overlay px (the overlay mirrors the canvas rect),
    // or -1 when nothing is snapped.
    property real snapGuideX: -1
    property real snapGuideY: -1

    // Rotation sticks to each 15° step as it passes. The tolerance is in degrees,
    // so the pull feels the same whatever the preview zoom. Ctrl passes straight
    // through, as it does for the move and resize snaps above.
    readonly property real rotateSnapStepDeg: 15
    readonly property real rotateSnapTolDeg: 5

    // Angles are written back into the same (-180, 180] range the inspector's
    // rotation slider and setClipRotationSnap use.
    function normalizeDeg(deg) {
        let d = deg % 360
        if (d > 180)
            d -= 360
        if (d <= -180)
            d += 360
        return d
    }

    function snapAngle(deg) {
        const nearest = Math.round(deg / rotateSnapStepDeg) * rotateSnapStepDeg
        return Math.abs(nearest - deg) <= rotateSnapTolDeg ? normalizeDeg(nearest) : deg
    }

    Component.onCompleted: refreshOverlay()

    Connections {
        target: EditorState
        function onTracksChanged() { root.refreshOverlay() }
        function onClipPropertiesPreviewed(trackIndex, clipIndex, keys) {
            for (const k of keys) {
                if (k === "x" || k === "y" || k === "width" || k === "height" || k === "rotation"
                        || k === "rotationX" || k === "rotationY" || k === "z" || k === "perspective") {
                    root.refreshOverlay()
                    return
                }
            }
        }
        function onSelectionChanged() { root.refreshOverlay() }
        // A scrub moves the playhead per scroll event; catch up once when it ends.
        function onPlayheadSecondsChanged() {
            if (!EditorState.scrubbing)
                root.refreshOverlay()
        }
        function onScrubbingChanged() {
            if (!EditorState.scrubbing)
                root.refreshOverlay()
        }
        function onPlayingChanged() {
            if (!EditorState.playing)
                root.refreshOverlay()
        }
    }

    Connections {
        target: EditorState.preview
        function onViewChanged() { root.refreshOverlay() }
    }

    // Presses that no box takes: a drag across the canvas box-selects. Below every box handler.
    DragHandler {
        id: marqueeDrag
        target: null
        enabled: EditorState.preview.mode !== "3d" && !root.interacting
        property point press: Qt.point(0, 0)
        property bool started: false

        onActiveChanged: {
            if (active) {
                const p = root.mapFromItem(null, marqueeDrag.centroid.scenePressPosition.x,
                                           marqueeDrag.centroid.scenePressPosition.y)
                const c = root.overlayClips.length > 0 ? root.overlayClips[0] : null
                const sx = c ? root.width / Math.max(1, c.canvasWidth) : 1
                const sy = c ? root.height / Math.max(1, c.canvasHeight) : 1
                marqueeDrag.press = p
                marqueeDrag.started = !root.pressHitsBox(p.x / sx, p.y / sy)
                root.marqueeActive = marqueeDrag.started
                root.marqueeRect = Qt.rect(p.x, p.y, 0, 0)
            } else if (marqueeDrag.started) {
                marqueeDrag.started = false
                root.marqueeActive = false
                root.finishMarquee((marqueeDrag.centroid.modifiers & (Qt.ShiftModifier | Qt.ControlModifier)) !== 0)
            }
        }
        onCentroidChanged: {
            if (!active || !marqueeDrag.started)
                return
            const p = root.mapFromItem(null, marqueeDrag.centroid.scenePosition.x,
                                       marqueeDrag.centroid.scenePosition.y)
            root.marqueeRect = Qt.rect(Math.min(p.x, marqueeDrag.press.x), Math.min(p.y, marqueeDrag.press.y),
                                       Math.abs(p.x - marqueeDrag.press.x), Math.abs(p.y - marqueeDrag.press.y))
        }
    }

    Repeater {
        id: clipRepeater

        delegate: Item {
            id: handle
            required property var modelData
            required property int index
            // Latest geometry; the model itself is only swapped when the set of clips changes.
            readonly property var box: root.overlayClips[index] || modelData

            readonly property bool selected: EditorState.selectedTrack === box.track
                                                    && EditorState.selectedClip === box.clip
            // A transform layer's own box: the group's frame. Only there while it is selected;
            // the rest of the time picking goes straight through it to the clips it moves.
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
                const parents = box.parents || []
                for (const p of parents) {
                    if (p.track === EditorState.selectedTrack && p.clip === EditorState.selectedClip)
                        return true
                }
                return false
            }
            readonly property bool childOfMovingLayer: {
                if (root.hiddenChildrenOf < 0)
                    return false
                for (const p of (box.parents || [])) {
                    if (p.track === root.hiddenChildrenOf)
                        return true
                }
                return false
            }
            // A member of the multi-selection: outlined as selected, moved through the group frame.
            readonly property bool inGroup: root.groupMode && !isTransform
                                            && EditorState.selectionContains(box.track, box.clip)
            // Dragged with the 3D group gizmo: the model is not rebuilt mid-drag, so it would hang behind.
            readonly property bool movedByGroupGizmo: root.groupDragging && box.layer3d === true
                                                      && !isTransform
                                                      && EditorState.selectionContains(box.track, box.clip)
            visible: (!isTransform || selected) && !childOfMovingLayer && !movedByGroupGizmo
            enabled: visible
            readonly property bool isText: box.kind === "text"
            // The gizmo's live pose while it drags this clip: the model is not rebuilt mid-drag.
            readonly property var gizmoPose: root.gizmoLive && root.gizmoLive.track === box.track
                                             && root.gizmoLive.clip === box.clip ? root.gizmoLive : null
            readonly property real poseRotX: gizmoPose ? gizmoPose.rotationX : (box.rotationX || 0)
            readonly property real poseRotY: gizmoPose ? gizmoPose.rotationY : (box.rotationY || 0)
            readonly property real poseZ: gizmoPose ? gizmoPose.z : (box.z || 0)
            readonly property real posePerspective: box.perspective || 2000
            // Tilted or pushed in depth: the box is drawn through the clip's perspective
            // transform, so its outline and grips land on the rendered quad.
            readonly property bool is3d: (poseRotX !== 0 || poseRotY !== 0 || poseZ !== 0)
            // A camera reframes every box, flat ones included, so the outline and grips have to go
            // through the pose matrix rather than straight onto the canvas.
            readonly property bool viaCamera: box.cameraActive === true

            readonly property real canvasW: Math.max(1, box.canvasWidth)
            readonly property real canvasH: Math.max(1, box.canvasHeight)
            readonly property real sx: parent.width / canvasW
            readonly property real sy: parent.height / canvasH

            // Live overrides applied during a drag so the box tracks
            // the cursor without rebuilding the (stale) model.
            property real liveX: -1e12
            property real liveY: -1e12
            property real liveW: -1
            property real liveH: -1
            property real liveRotation: 1e9

            readonly property real layoutX: gizmoPose ? gizmoPose.x : liveX > -1e11 ? liveX : box.x
            readonly property real layoutY: gizmoPose ? gizmoPose.y : liveY > -1e11 ? liveY : box.y
            readonly property real layoutW: gizmoPose ? gizmoPose.width : liveW >= 0 ? liveW : box.width
            readonly property real layoutH: gizmoPose ? gizmoPose.height : liveH >= 0 ? liveH : box.height
            readonly property real centerX: (layoutX + layoutW * 0.5) * sx
            readonly property real centerY: (layoutY + layoutH * 0.5) * sy

            x: layoutX * sx
            y: layoutY * sy
            width: Math.max(24, layoutW * sx)
            height: Math.max(24, layoutH * sy)
            // Front-most track (lowest index) sits on top so it
            // wins click hit-testing over boxes behind it. The clip
            // selected on the timeline is raised above all of them, so a
            // box that lies under a full-frame clip on an upper track is
            // still the one the pointer reaches.
            z: handle.selected ? 900 : -box.track
            transformOrigin: Item.Center
            readonly property real layoutRotation: gizmoPose ? gizmoPose.rotation
                                                   : liveRotation < 1e8 ? liveRotation : box.rotation
            rotation: is3d || hasParent || viaCamera ? 0 : layoutRotation
            transform: Matrix4x4 {
                id: poseTransform
                matrix: (void EditorState.preview.viewRevision, handle.is3d || handle.hasParent || handle.viaCamera)
                        ? EditorState.preview.clipPoseMatrix({
                                                                "canvasWidth": handle.box.canvasWidth,
                                                                "canvasHeight": handle.box.canvasHeight,
                                                                "rotationX": handle.poseRotX,
                                                                "rotationY": handle.poseRotY,
                                                                "z": handle.poseZ,
                                                                "perspective": handle.posePerspective,
                                                                "parentActive": handle.box.parentActive,
                                                                "parent": handle.box.parent
                                                            }, handle.layoutX, handle.layoutY,
                                                            handle.layoutW, handle.layoutH,
                                                            handle.layoutRotation, handle.sx, handle.sy)
                        : Qt.matrix4x4()
            }

            // Overlay point -> this box's own (untilted) layout px, through the pose as it stands
            // now. Taken at a grab and reused for the whole drag, since the pose follows the drag.
            function overlayToLocalMapper() {
                const m = Qt.matrix4x4(1, 0, 0, handle.x, 0, 1, 0, handle.y, 0, 0, 1, 0, 0, 0, 0, 1)
                        .times(poseTransform.matrix).inverted()
                return function(px, py) {
                    const v = m.times(Qt.vector4d(px, py, 0, 1))
                    return Qt.point(v.x / v.w, v.y / v.w)
                }
            }

            property real dragStartX: 0
            property real dragStartY: 0
            property real dragStartW: 1
            property real dragStartH: 1
            property int dragStartPixelSize: 64
            // True while a resize grip is held, for the size readout.
            property bool resizing: false
            // True while the rotate grip is held, for the angle readout.
            property bool rotating: false
            // Clip angle minus pointer angle when the rotate grip was taken.
            property real rotateGrabOffset: 0

            // A rotated box has no axis-aligned edges to stick with, so it does
            // not snap — pulling its bounding box would move it sideways. Tilts, parents and
            // the camera are fine: the snap works on the projected edges, and skips any that
            // do not run along the screen axes.
            // The 3D view's screen is not the canvas, so there is nothing there to snap to.
            readonly property bool canSnap: Math.abs(handle.layoutRotation) < 0.01
                                            && EditorState.preview.mode !== "3d"

            // The box as it stood at the grab, in the shape the snapping calls take.
            function grabBox() {
                return Object.assign({}, handle.box, {
                    "x": handle.dragStartX, "y": handle.dragStartY,
                    "width": handle.dragStartW, "height": handle.dragStartH,
                    "rotation": handle.layoutRotation,
                    "rotationX": handle.poseRotX, "rotationY": handle.poseRotY, "z": handle.poseZ
                })
            }

            // Guides are published in overlay px so they can be drawn once, at
            // root level, spanning the whole canvas rather than the clip box.
            function publishGuides(gx, gy) {
                root.snapGuideX = gx >= 0 ? gx * handle.sx : -1
                root.snapGuideY = gy >= 0 ? gy * handle.sy : -1
            }

            // Arrow keys move the selected clip; Shift makes the step coarse.
            // Do not bind `focus` to selection — recreating this delegate after a
            // tracksChanged refresh would steal focus from property-panel fields.
            // Focus is taken explicitly when the user clicks or drags the box.
            Keys.onPressed: function(event) {
                if (!(handle.selected || handle.inGroup) || root.mode3d)
                    return
                const step = (event.modifiers & Qt.ShiftModifier) ? 10 : 1
                let dx = 0
                let dy = 0
                switch (event.key) {
                case Qt.Key_Left:  dx = -step; break
                case Qt.Key_Right: dx = step; break
                case Qt.Key_Up:    dy = -step; break
                case Qt.Key_Down:  dy = step; break
                default: return
                }
                EditorState.beginPreviewDrag()
                if (root.groupMode) {
                    EditorState.preview.applyGroupTransform(root.groupBoxes, 0, 0, 1, 1, 0, dx, dy)
                } else {
                    EditorState.previewSetClipPosition(
                        handle.box.track,
                        handle.box.clip,
                        handle.box.x + dx,
                        handle.box.y + dy)
                }
                EditorState.commitPreviewDrag()
                event.accepted = true
            }

            TransformOverlayBoxBorder {
                selected: handle.selected || handle.inGroup
                transformClip: handle.isTransform
                coveredBySelectedLayer: handle.coveredBySelectedLayer
            }

            TapHandler {
                id: boxTap
                property int pressMods: 0
                onPressedChanged: {
                    if (pressed)
                        boxTap.pressMods = boxTap.point.modifiers
                }
                onTapped: (eventPoint) => {
                    if (!handle.isTransform && (boxTap.pressMods & (Qt.ShiftModifier | Qt.ControlModifier))) {
                        root.toggleSelected(handle.box.track, handle.box.clip)
                        handle.forceActiveFocus()
                        return
                    }
                    // Tapping inside a selected layer's frame picks the clip under the pointer,
                    // so the group's children stay one tap away.
                    if (handle.isTransform) {
                        const p = root.mapFromItem(null, eventPoint.scenePosition.x,
                                                   eventPoint.scenePosition.y)
                        const hit = EditorState.preview.clipAtCanvasPoint(p.x / handle.sx, p.y / handle.sy)
                        if (hit && hit.track !== undefined) {
                            EditorState.selectClip(hit.track, hit.clip)
                            return
                        }
                    }
                    EditorState.selectClip(handle.box.track, handle.box.clip)
                    handle.forceActiveFocus()
                    if (tapCount === 2 && handle.isText && !root.mode3d)
                        root.textEditRequested(handle.box.track, handle.box.clip)
                }
            }

            // "Name · n clips" over a selected layer's frame.
            TransformOverlayLayerChip {
                visible: handle.isTransform && handle.selected
                clipName: handle.box.name || ""
                childCount: handle.box.childCount || 0
            }

            DragHandler {
                id: bodyDrag
                target: null
                // Only the clip selected on the timeline moves: dragging a box
                // that merely happens to be under the pointer used to shift the
                // wrong clip, so an unselected box is tap-to-select only.
                // Off while a grip is held too: a handler on the parent item can
                // otherwise take the grab from the grip once the drag threshold
                // is passed, turning a resize into a move.
                enabled: handle.selected && !handle.resizing && !root.mode3d && !root.groupMode
                cursorShape: Qt.SizeAllCursor

                // Press point in canvas px. The box rides the cursor as it moves, so the
                // drag is solved against the overlay, which stands still.
                property real pressPx: 0
                property real pressPy: 0
                property var startBox: null

                onActiveChanged: {
                    if (active) {
                        root.interacting = true
                        if (handle.isTransform)
                            root.movingLayerTrack = handle.box.track
                        // Taken at activation rather than at the press, so the drag
                        // threshold is not replayed as a jump.
                        const p = root.mapFromItem(null, bodyDrag.centroid.scenePosition.x,
                                                   bodyDrag.centroid.scenePosition.y)
                        bodyDrag.pressPx = p.x / handle.sx
                        bodyDrag.pressPy = p.y / handle.sy
                        handle.dragStartX = handle.box.x
                        handle.dragStartY = handle.box.y
                        handle.dragStartW = handle.layoutW
                        handle.dragStartH = handle.layoutH
                        bodyDrag.startBox = handle.grabBox()
                        handle.liveX = handle.dragStartX
                        handle.liveY = handle.dragStartY
                        EditorState.selectClip(handle.box.track, handle.box.clip)
                        handle.forceActiveFocus()
                        EditorState.beginPreviewDrag()
                    } else {
                        handle.liveX = -1e12
                        handle.liveY = -1e12
                        root.endInteraction()
                    }
                }
                onCentroidChanged: {
                    if (!active)
                        return
                    const p = root.mapFromItem(null, bodyDrag.centroid.scenePosition.x,
                                               bodyDrag.centroid.scenePosition.y)
                    // Solved through the box's pose, parents and the camera, so the grabbed point
                    // stays under the pointer. Both edges and the centre stick, so a clip can be
                    // landed flush against a canvas edge or dead-centre by feel. Ctrl passes
                    // straight through (Alt is the window drag on most Linux desktops, so it is
                    // not usable here).
                    const snap = handle.canSnap && !(bodyDrag.centroid.modifiers & Qt.ControlModifier)
                    const moved = EditorState.preview.snapMove(bodyDrag.startBox, bodyDrag.pressPx, bodyDrag.pressPy,
                                                              p.x / handle.sx, p.y / handle.sy,
                                                              root.snapTolPx / handle.sx, snap)
                    // Seen edge-on, the plane has no point under the pointer: hold still.
                    if (!moved.valid)
                        return
                    const xPx = moved.x
                    const yPx = moved.y
                    handle.publishGuides(moved.guideX, moved.guideY)
                    handle.liveX = xPx
                    handle.liveY = yPx
                    EditorState.previewSetClipPosition(
                        handle.box.track,
                        handle.box.clip,
                        xPx,
                        yPx)
                }
            }

            // Resize grips: 4 edges then 4 corners, the same frame the canvas
            // crop tool uses. `dx`/`dy` say which edges each grip moves
            // (-1 = left/top, +1 = right/bottom, 0 = stays put). The opposite
            // edge or corner is the anchor and does not move.
            Repeater {
                model: (handle.selected && !root.mode3d && !root.groupMode)
                       ? [
                           { dx: -1, dy:  0, cursor: Qt.SizeHorCursor },
                           { dx:  1, dy:  0, cursor: Qt.SizeHorCursor },
                           { dx:  0, dy: -1, cursor: Qt.SizeVerCursor },
                           { dx:  0, dy:  1, cursor: Qt.SizeVerCursor },
                           { dx: -1, dy: -1, cursor: Qt.SizeFDiagCursor },
                           { dx:  1, dy:  1, cursor: Qt.SizeFDiagCursor },
                           { dx:  1, dy: -1, cursor: Qt.SizeBDiagCursor },
                           { dx: -1, dy:  1, cursor: Qt.SizeBDiagCursor }
                       ]
                       : []

                delegate: TransformOverlayResizeGrip {
                    target: handle
                    overlay: root
                }
            }

            TransformOverlaySizeReadout {
                resizing: handle.resizing
                rotating: handle.rotating
                layoutRotation: handle.layoutRotation
                layoutW: handle.layoutW
                layoutH: handle.layoutH
                spin: handle.rotation
            }

            // Rotation handle above the box (a 3D layer turns with the gizmo's rings instead)
            TransformOverlayRotateHandle {
                target: handle
                overlay: root
            }
        }
    }

    TransformOverlayGroupBox {
        id: groupBox
        visible: root.groupMode
        enabled: root.groupMode
        z: 890
        overlay: root
        boxes: root.groupBoxes
    }

    Rectangle {
        visible: root.marqueeActive && (root.marqueeRect.width > 2 || root.marqueeRect.height > 2)
        x: root.marqueeRect.x
        y: root.marqueeRect.y
        width: root.marqueeRect.width
        height: root.marqueeRect.height
        z: 890
        color: Qt.rgba(Theme.c.timelineMarqueeRect.background.r, Theme.c.timelineMarqueeRect.background.g, Theme.c.timelineMarqueeRect.background.b, 0.12)
        border.width: Theme.c.timelineMarqueeRect.borderWidth
        border.color: Theme.c.timelineMarqueeRect.border
    }

    // With a clip inside a transform layer selected: the layer's frame, dashed, and a chip that
    // selects it (Shift+G does the same). Alt+click is not used: Alt drags the window on Linux.
    Item {
        id: parentFrame
        anchors.fill: parent
        z: 880
        visible: root.parentFrameBox !== null && !root.interacting
        readonly property var quad: root.parentFrameBox ? (root.parentFrameBox.quad || []) : []
        readonly property real qsx: root.width / Math.max(1, root.parentFrameBox ? root.parentFrameBox.canvasWidth : 1)
        readonly property real qsy: root.height / Math.max(1, root.parentFrameBox ? root.parentFrameBox.canvasHeight : 1)
        function corner(i) {
            const q = parentFrame.quad[i]
            return q ? Qt.point(q.x * qsx, q.y * qsy) : Qt.point(0, 0)
        }

        TransformOverlayParentOutline {
            frame: parentFrame
        }

        TransformOverlayParentChip {
            frame: parentFrame
            layerName: root.parentFrameBox ? (root.parentFrameBox.name || "") : ""
        }
    }

    TransformGizmo {
        id: gizmo
        anchors.fill: parent
        z: 950
        sx: root.width / Math.max(1, root.gizmoBox ? root.gizmoBox.canvasWidth : 1)
        // Hidden while the box itself is dragged, which it would not follow.
        box: root.interacting && dragging === "" ? null : root.gizmoBox
        groupBoxes: root.group3dBoxes
        onDragStarted: {
            root.interacting = true
            // A group keeps its selection; picking one member would collapse it.
            if (box.kind !== "group")
                EditorState.selectClip(box.track, box.clip)
        }
        onDragFinished: root.endInteraction()
    }

    // Alignment guides for whichever snap is currently engaged. Drawn once at
    // root level so a guide spans the whole canvas, not just the clip box.
    TransformOverlaySnapGuide {
        alongX: true
        position: root.snapGuideX
    }
    TransformOverlaySnapGuide {
        alongX: false
        position: root.snapGuideY
    }
}
