import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Shapes
import Drift
// Touch transform overlay: move/scale/rotate the clips visible at the playhead.
// Same maths as the desktop TransformOverlay, but
// the grips carry a fingertip-sized hit area instead of a mouse-sized one, and
// the mouse-only affordances (hover, wheel pass-through, arrow-key nudging,
// Shift/Ctrl modifiers) are gone — a phone has none of them. Corner grips
// therefore keep footage aspect unconditionally and snapping is always on.
// Geometry and visibility are driven by PreviewToolHost.
Item {
    id: root

    // The preview canvas this overlay mirrors; the overlay itself spans the whole viewport.
    property Item canvas
    readonly property alias stage: stage

    property var overlayClips: []
    // True while a handle is being dragged. Rebuilding the model mid-drag would
    // destroy the delegate that owns the active grab, so refreshes are
    // suppressed until the drag ends.
    property bool interacting: false

    // A text clip was double-tapped: the preview opens its inline editor.
    signal textEditRequested(int track, int clip)

    // Phones only have the camera output view, which edits with the flat grips: no 3D gizmo.
    readonly property var gizmoBox: null
    readonly property var gizmoLive: gizmo.livePose

    // A transform layer being moved: the clips under it are drawn from a model that is not
    // rebuilt mid-drag, so their outlines would hang in the old place and are hidden meanwhile.
    property int movingLayerTrack: -1
    readonly property int hiddenChildrenOf: movingLayerTrack >= 0 ? movingLayerTrack
        : (gizmoLive && gizmoBox && gizmoBox.kind === "transform" ? gizmoBox.track : -1)

    readonly property var selectedBox: {
        for (const b of overlayClips) {
            if (b.track === EditorState.selectedTrack && b.clip === EditorState.selectedClip)
                return b
        }
        return null
    }
    // The transform clip over the selected clip (innermost), for its dashed frame and chip.
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

    // Visible dot; the touch target around it is gripTouch wide.
    readonly property real gripSize: 14
    readonly property real gripTouch: 44

    // True when two overlay models describe the same boxes. Text/name updates
    // still reach delegates via liveStyle, so rebuilding for every
    // keystroke is unnecessary.
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
                    || x.anchorX !== y.anchorX || x.anchorY !== y.anchorY || x.anchorZ !== y.anchorZ
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
        if (EditorState.playing)
            return
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
    readonly property real snapTolPx: 10

    // Engaged guide lines, in overlay px, or -1 when nothing is snapped.
    property real snapGuideX: -1
    property real snapGuideY: -1

    onSnapGuideXChanged: if (snapGuideX >= 0) Haptics.detent()
    onSnapGuideYChanged: if (snapGuideY >= 0) Haptics.detent()

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

    // The canvas rect, inside a viewport-sized root: grips and handles of a clip that runs past
    // an edge stay inside the root, and so inside the area that takes the pointer.
    Item {
        id: stage
        x: root.canvas ? root.canvas.x : 0
        y: root.canvas ? root.canvas.y : 0
        width: root.canvas ? root.canvas.width : root.width
        height: root.canvas ? root.canvas.height : root.height

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
                // A transform layer's own box: only there while selected, so taps otherwise reach
                // the clips it moves.
                readonly property bool isTransform: box.kind === "transform"
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
                readonly property bool coveredBySelectedLayer: {
                    for (const p of (box.parents || [])) {
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
                visible: (!isTransform || selected) && !childOfMovingLayer
                enabled: visible
                readonly property bool isText: box.kind === "text"
                // Tilted or pushed in depth: the box is drawn through the clip's perspective
                // transform, so its outline and grips land on the rendered quad.
                // The gizmo's live pose while it drags this clip: the model is not rebuilt mid-drag.
                readonly property var gizmoPose: root.gizmoLive && root.gizmoLive.track === box.track
                                                 && root.gizmoLive.clip === box.clip ? root.gizmoLive : null
                readonly property var pose3d: gizmoPose || box
                readonly property bool is3d: ((pose3d.rotationX || 0) !== 0
                                                             || (pose3d.rotationY || 0) !== 0
                                                             || (pose3d.z || 0) !== 0
                                                             || box.anchorX !== undefined || box.anchorY !== undefined)
                // A camera reframes every box, flat ones included.
                readonly property bool viaCamera: box.cameraActive === true

                readonly property real canvasW: Math.max(1, box.canvasWidth)
                readonly property real canvasH: Math.max(1, box.canvasHeight)
                readonly property real sx: parent.width / canvasW
                readonly property real sy: parent.height / canvasH

                // Live overrides applied during a drag so the box tracks the finger
                // without rebuilding the (stale) model.
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
                // Front-most track (lowest index) sits on top so it wins tap
                // hit-testing over boxes behind it. The clip selected on the timeline
                // is raised above all of them, so a box that lies under a full-frame
                // clip on an upper track is still the one the tap reaches.
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
                                                                    "rotationX": handle.pose3d.rotationX || 0,
                                                                    "rotationY": handle.pose3d.rotationY || 0,
                                                                    "z": handle.pose3d.z || 0,
                                                                    "perspective": handle.box.perspective || 2000,
                                                                    "anchorX": handle.box.anchorX,
                                                                    "anchorY": handle.box.anchorY,
                                                                    "anchorZ": handle.box.anchorZ,
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

                // A rotated box has no axis-aligned edges to stick with, so it does
                // not snap — pulling its bounding box would move it sideways. Tilts, parents and
                // the camera are fine: the snap works on the projected edges, and skips any that
                // do not run along the screen axes.
                // The 3D view's screen is not the canvas, so there is nothing there to snap to.
                readonly property bool canSnap: Math.abs(handle.layoutRotation) < 0.01

                // The box as it stood at the grab, in the shape the snapping calls take.
                function grabBox() {
                    return Object.assign({}, handle.box, {
                        "x": handle.dragStartX, "y": handle.dragStartY,
                        "width": handle.dragStartW, "height": handle.dragStartH,
                        "rotation": handle.layoutRotation,
                        "rotationX": handle.pose3d.rotationX || 0, "rotationY": handle.pose3d.rotationY || 0,
                        "z": handle.pose3d.z || 0
                    })
                }

                // Guides are published in overlay px so they can be drawn once, at
                // root level, spanning the whole canvas rather than the clip box.
                function publishGuides(gx, gy) {
                    root.snapGuideX = gx >= 0 ? gx * handle.sx : -1
                    root.snapGuideY = gy >= 0 ? gy * handle.sy : -1
                }

                AndroidTransformClipOutline {
                    selected: handle.selected
                    transformLayer: handle.isTransform
                    coveredBySelectedLayer: handle.coveredBySelectedLayer
                }

                TapHandler {
                    onTapped: (eventPoint) => {
                        // A tap inside a selected layer's frame picks the clip under the finger.
                        if (handle.isTransform) {
                            const p = stage.mapFromItem(null, eventPoint.scenePosition.x,
                                                       eventPoint.scenePosition.y)
                            const hit = EditorState.preview.clipAtCanvasPoint(p.x / handle.sx, p.y / handle.sy)
                            if (hit && hit.track !== undefined) {
                                EditorState.selectClip(hit.track, hit.clip)
                                Haptics.select()
                                return
                            }
                        }
                        EditorState.selectClip(handle.box.track, handle.box.clip)
                        handle.forceActiveFocus()
                        Haptics.select()
                        if (tapCount === 2 && handle.isText)
                            root.textEditRequested(handle.box.track, handle.box.clip)
                    }
                }

                AndroidTransformLayerChip {
                    visible: handle.isTransform && handle.selected
                    x: 0
                    layerName: handle.box.name || ""
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
                    enabled: handle.selected && !handle.resizing
                    // The press point in canvas px, and the box as it stood then: the drag is solved
                    // through the box's pose, parents and the camera.
                    property point pressCanvas: Qt.point(0, 0)
                    property var startBox: null
                    onActiveChanged: {
                        if (active) {
                            root.interacting = true
                            if (handle.isTransform)
                                root.movingLayerTrack = handle.box.track
                            const p = stage.mapFromItem(null, centroid.scenePosition.x, centroid.scenePosition.y)
                            pressCanvas = Qt.point(p.x / handle.sx, p.y / handle.sy)
                            handle.dragStartX = handle.box.x
                            handle.dragStartY = handle.box.y
                            handle.dragStartW = handle.layoutW
                            handle.dragStartH = handle.layoutH
                            startBox = handle.grabBox()
                            handle.liveX = handle.dragStartX
                            handle.liveY = handle.dragStartY
                            EditorState.selectClip(handle.box.track, handle.box.clip)
                            handle.forceActiveFocus()
                            EditorState.beginPreviewDrag()
                            Haptics.pickUp()
                        } else {
                            handle.liveX = -1e12
                            handle.liveY = -1e12
                            root.endInteraction()
                            Haptics.drop()
                        }
                    }
                    onTranslationChanged: {
                        if (!active || !startBox)
                            return
                        const p = stage.mapFromItem(null, centroid.scenePosition.x, centroid.scenePosition.y)
                        // Both edges and the centre stick, so a clip can be landed
                        // flush against a canvas edge or dead-centre by feel.
                        const moved = EditorState.preview.snapMove(startBox, pressCanvas.x, pressCanvas.y,
                                                                  p.x / handle.sx, p.y / handle.sy,
                                                                  root.snapTolPx / handle.sx, handle.canSnap)
                        // Seen edge-on, the plane has no point under the finger: hold still.
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

                // Resize grips: 4 edges then 4 corners, the same frame the canvas crop
                // tool uses. `dx`/`dy` say which edges each grip moves (-1 = left/top,
                // +1 = right/bottom, 0 = stays put). The opposite edge or corner is the
                // anchor and does not move.
                Repeater {
                    // A 3D layer is sized with the gizmo's scale tool instead.
                    model: (handle.selected)
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
                    delegate: AndroidTransformResizeGrip {
                        frameItem: handle
                        overlay: root
                    }
                }

                AndroidTransformSizeReadout {
                    frameItem: handle
                    overlay: root
                }

                AndroidTransformRotateHandle {
                    frameItem: handle
                    overlay: root
                }
            }
        }

        AndroidTransformParentFrame {
            overlay: root
        }

        TransformGizmo {
            id: gizmo
            anchors.fill: parent
            z: 950
            touch: true
            sx: stage.width / Math.max(1, root.gizmoBox ? root.gizmoBox.canvasWidth : 1)
            // Hidden while the box itself is dragged, which it would not follow.
            box: root.interacting && dragging === "" ? null : root.gizmoBox
            onDragStarted: {
                root.interacting = true
                EditorState.selectClip(box.track, box.clip)
                Haptics.pickUp()
            }
            onDragFinished: {
                root.endInteraction()
                Haptics.drop()
            }
        }

        // Alignment guides for whichever snap is currently engaged. Drawn once at
        // root level so a guide spans the whole canvas, not just the clip box.
        AndroidTransformSnapLine {
            vertical: true
            guide: root.snapGuideX
        }
        AndroidTransformSnapLine {
            vertical: false
            guide: root.snapGuideY
        }
    }
}
