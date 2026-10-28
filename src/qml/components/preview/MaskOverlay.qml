import QtQuick
import Drift

// Mask editor: direct manipulation of the selected clip's mask layers. Geometry is driven by
// PreviewToolHost, which mirrors the canvas rect here so grips on a mask that runs past a canvas
// edge stay drawn and grabbable.
//
// Mask coordinates are normalized to the *host clip's* frame, not the canvas: the coverage map is
// rasterized at the layer's size and sampled at the layer's UV, so a mask travels with the clip's
// transform. That is why everything below hangs off `clipFrame`, an Item placed and rotated to
// match the clip, rather than off this root.
Item {
    id: root

    // One resolved snapshot from AppController::maskEditorState(), so the host rect, the layer
    // list and which layer is selected can never be out of step with each other.
    property var editorState: ({})
    readonly property var layers: editorState.layers || []
    readonly property bool hasFrame: !!editorState.hasFrame

    // True while a grip or body drag is running. Refreshing mid-drag would destroy the delegate
    // that owns the active grab, exactly as in TransformOverlay.
    property bool interacting: false

    readonly property real canvasW: Math.max(1, editorState.canvasWidth || 1)
    readonly property real canvasH: Math.max(1, editorState.canvasHeight || 1)
    readonly property real sx: root.width / canvasW
    readonly property real sy: root.height / canvasH

    // Stickiness in normalized mask units. The tolerance is a screen distance so the pull feels
    // the same whatever the project resolution or preview zoom.
    readonly property real snapTolPx: 8

    function refreshOverlay() {
        // Hidden: onVisibleChanged catches up when shown.
        if (!visible || interacting)
            return
        // Playhead ticks ~60 Hz; rebuilding grips every tick during playback is wasted work.
        if (EditorState.playing)
            return
        const next = EditorState.preview.maskEditorState()
        editorState = next
        const layers = next.layers || []
        // Delegates read their values from here by index; the models below are only swapped
        // when the set of layers changes, so a value edit does not recreate the grips.
        layerValues = layers
        const signature = layers.map(l => l.track + ":" + l.clip + ":" + ((l.mask || {}).shape || ""))
                                .join("|")
        if (signature === layerSignature)
            return
        layerSignature = signature
        // Set imperatively for the same reason TransformOverlay does: binding the model
        // re-enters when tracksChanged fires during delegate setup.
        layerModel = layers
        maskRepeater.model = layers
    }

    property var layerValues: []
    property var layerModel: []
    property string layerSignature: ""

    function endInteraction() {
        EditorState.commitPreviewDrag()
        interacting = false
        Qt.callLater(refreshOverlay)
    }

    // Nearest target within `tol` of any candidate, returned as the delta to add to the moving
    // value. Mirrors TransformOverlay.snapAxis.
    function snapAxis(candidates, targets, tol) {
        let result = { delta: 0 }
        let best = tol
        for (const c of candidates) {
            for (const t of targets) {
                const d = t - c
                if (Math.abs(d) < best) {
                    best = Math.abs(d)
                    result = { delta: d }
                }
            }
        }
        return result
    }

    onVisibleChanged: if (visible) refreshOverlay()
    Component.onCompleted: refreshOverlay()

    Connections {
        target: EditorState
        function onTracksChanged() { root.refreshOverlay() }
        function onClipPropertiesPreviewed(trackIndex, clipIndex, keys) {
            for (const k of keys) {
                if (k.startsWith("mask.") || k === "x" || k === "y" || k === "width"
                        || k === "height" || k === "rotation" || k === "rotationX"
                        || k === "rotationY" || k === "z" || k === "perspective") {
                    root.refreshOverlay()
                    return
                }
            }
        }
        function onSelectionChanged() { root.refreshOverlay() }
        function onSelectedClipDataChanged() { root.refreshOverlay() }
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

    // The clip's destination rect on the canvas, rotated with it. Mask boxes are children so QML
    // composes the two rotations instead of this file doing it by hand.
    Item {
        id: clipFrame
        visible: root.hasFrame
        x: (root.editorState.x || 0) * root.sx
        y: (root.editorState.y || 0) * root.sy
        width: Math.max(1, (root.editorState.width || root.canvasW) * root.sx)
        height: Math.max(1, (root.editorState.height || root.canvasH) * root.sy)
        transformOrigin: Item.Center
        // A tilted clip frame goes through the clip's perspective instead of the flat spin.
        readonly property bool is3d: (root.editorState.rotationX || 0) !== 0
                                     || (root.editorState.rotationY || 0) !== 0
                                     || (root.editorState.z || 0) !== 0
        // Moved by a transform layer: composed through that parent as well, flat or not.
        readonly property bool hasParent: root.editorState.parentActive === true
        rotation: is3d || hasParent ? 0 : (root.editorState.rotation || 0)
        transform: Matrix4x4 {
            matrix: clipFrame.is3d || clipFrame.hasParent
                    ? EditorState.preview.clipPoseMatrix(root.editorState, root.editorState.x || 0,
                                                        root.editorState.y || 0,
                                                        root.editorState.width || root.canvasW,
                                                        root.editorState.height || root.canvasH,
                                                        root.editorState.rotation || 0,
                                                        root.sx, root.sy)
                    : Qt.matrix4x4()
        }

        // The clip's own bounds, so it is obvious what the mask coordinates are relative to.
        MaskOverlayClipBounds { }

        Repeater {
            id: maskRepeater

            delegate: Item {
                id: maskBox
                required property int index
                required property var modelData
                readonly property var layerEntry: root.layerValues[index] || modelData

                readonly property bool isSelected: !!layerEntry.selected
                readonly property var maskData: layerEntry.mask || ({})
                readonly property int layerTrack: layerEntry.track
                readonly property int layerClip: layerEntry.clip
                // Once any scalar has keys the drag has to write through the keyframe API, or it
                // would move the static value under an animation that immediately overrides it
                // again on the next frame.
                readonly property bool animated: !!layerEntry.animated

                // Bars ignores x/y/w entirely — it is two full-width bands whose height comes from
                // h — so a centred box with corner grips would be a lie. It draws its real bands
                // below and is edited from the inspector.
                readonly property bool isBars: maskData.shape === "bars"
                // Freeform ignores x/y/w/h too: the points are the shape. Its box is their
                // bounding rect, shown for reference, and it is edited vertex by vertex — bbox
                // grips would appear to do something and do nothing.
                readonly property bool isFreeform: maskData.shape === "freeform"
                readonly property bool editable: !isBars && !isFreeform && maskData.enabled !== false
                readonly property bool movable: editable
                                                || (isFreeform && maskData.enabled !== false)

                // Bounding rect of the polygon's vertices, in normalized clip-frame units.
                readonly property var pointBounds: {
                    const pts = maskBox.maskData.points || []
                    if (!maskBox.isFreeform || pts.length === 0)
                        return null
                    let minX = 1e9, minY = 1e9, maxX = -1e9, maxY = -1e9
                    for (const p of pts) {
                        minX = Math.min(minX, p.x); maxX = Math.max(maxX, p.x)
                        minY = Math.min(minY, p.y); maxY = Math.max(maxY, p.y)
                    }
                    return { x: (minX + maxX) / 2, y: (minY + maxY) / 2,
                             w: Math.max(0.001, maxX - minX), h: Math.max(0.001, maxY - minY) }
                }

                // Live overrides applied during a drag so the box tracks the cursor without
                // rebuilding the (stale) model.
                property real liveX: -1e12
                property real liveY: -1e12
                property real liveW: -1
                property real liveH: -1
                property real liveRotation: 1e9

                readonly property real mx: liveX > -1e11 ? liveX
                        : (pointBounds ? pointBounds.x : (maskData.x || 0))
                readonly property real my: liveY > -1e11 ? liveY
                        : (pointBounds ? pointBounds.y : (maskData.y || 0))
                readonly property real mw: liveW >= 0 ? liveW
                        : (pointBounds ? pointBounds.w : (maskData.w || 0))
                readonly property real mh: liveH >= 0 ? liveH
                        : (pointBounds ? pointBounds.h : (maskData.h || 0))

                // Drag anchors, captured on press.
                property real dragStartX: 0
                property real dragStartY: 0
                property real dragStartW: 0
                property real dragStartH: 0
                // Vertex positions captured on press, so a body drag stays absolute rather than
                // accumulating rounding from each incremental write.
                property var dragStartPoints: []

                visible: !maskBox.isBars
                x: (mx - mw / 2) * clipFrame.width
                y: (my - mh / 2) * clipFrame.height
                width: Math.max(1, mw * clipFrame.width)
                height: Math.max(1, mh * clipFrame.height)
                transformOrigin: Item.Center
                // A freeform's box is derived from its (already absolute) points, so rotating it
                // here would double-apply the rotation the rasterizer does.
                rotation: maskBox.isFreeform
                          ? 0 : (liveRotation < 1e8 ? liveRotation : (maskData.rotation || 0))
                // The selected entry sits on top so its grips win hit-testing over the outlines of
                // entries behind it.
                z: maskBox.isSelected ? 100 : 0

                // Normalized snap targets: the clip's edges and centre lines.
                readonly property var snapTargets: [0, 0.5, 1]
                readonly property real snapTolX: root.snapTolPx / Math.max(1, clipFrame.width)
                readonly property real snapTolY: root.snapTolPx / Math.max(1, clipFrame.height)
                // A rotated box has no axis-aligned edges to stick with.
                readonly property bool canSnap: Math.abs(maskBox.rotation) < 0.01

                function clearLive() {
                    liveX = -1e12
                    liveY = -1e12
                    liveW = -1
                    liveH = -1
                    liveRotation = 1e9
                }

                // Rebuild the mask map from the live values and hand it back. The copy is
                // load-bearing: maskFromMap resets anything the map omits.
                function writeLive() {
                    if (maskBox.animated) {
                        writeLiveKeyframes()
                        return
                    }
                    const mask = Object.assign({}, maskBox.maskData)
                    mask.x = maskBox.mx
                    mask.y = maskBox.my
                    mask.w = maskBox.mw
                    mask.h = maskBox.mh
                    if (maskBox.liveRotation < 1e8)
                        mask.rotation = maskBox.liveRotation
                    EditorState.previewSetClipMask(maskBox.layerTrack, maskBox.layerClip, mask)
                }

                // Writes go to the playhead. previewSetClipKeyframe honours autoKeyEnabled and the
                // retarget-nearest policy, so an existing key moves rather than a new one appearing
                // on every drag.
                function writeLiveKeyframes() {
                    const at = EditorState.playheadSeconds
                    const put = (param, value) => EditorState.previewSetClipKeyframe(
                        maskBox.layerTrack, maskBox.layerClip, "mask." + param, at, value)
                    put("x", maskBox.mx)
                    put("y", maskBox.my)
                    put("w", maskBox.mw)
                    put("h", maskBox.mh)
                    if (maskBox.liveRotation < 1e8)
                        put("rotation", maskBox.liveRotation)
                }

                // Freeform has no centre to write, so a body drag shifts every vertex instead.
                function writePointsTranslated(dx, dy) {
                    const mask = Object.assign({}, maskBox.maskData)
                    const points = []
                    for (const p of maskBox.dragStartPoints)
                        points.push({ "x": p.x + dx, "y": p.y + dy })
                    mask.points = points
                    if ((maskBox.maskData.pathKeyCount || 0) > 0)
                        EditorState.masking.previewSetMaskPathKey(maskBox.layerTrack, maskBox.layerClip,
                                                                  EditorState.playheadSeconds, points)
                    else
                        EditorState.previewSetClipMask(maskBox.layerTrack, maskBox.layerClip, mask)
                }

                function writePointAt(index, nx, ny) {
                    const mask = Object.assign({}, maskBox.maskData)
                    const points = []
                    const src = maskBox.maskData.points || []
                    for (let i = 0; i < src.length; ++i) {
                        points.push(i === index ? { "x": nx, "y": ny }
                                                : { "x": src[i].x, "y": src[i].y })
                    }
                    mask.points = points
                    if ((maskBox.maskData.pathKeyCount || 0) > 0)
                        EditorState.masking.previewSetMaskPathKey(maskBox.layerTrack, maskBox.layerClip,
                                                                  EditorState.playheadSeconds, points)
                    else
                        EditorState.previewSetClipMask(maskBox.layerTrack, maskBox.layerClip, mask)
                }

                // Arrow keys nudge the selected mask; Shift makes the step coarse. Focus is taken
                // explicitly on click, never bound to selection — a rebuild would otherwise steal
                // it from the properties panel.
                Keys.onPressed: function(event) {
                    if (!maskBox.isSelected || !maskBox.movable)
                        return
                    const stepX = ((event.modifiers & Qt.ShiftModifier) ? 10 : 1)
                                / Math.max(1, clipFrame.width)
                    const stepY = ((event.modifiers & Qt.ShiftModifier) ? 10 : 1)
                                / Math.max(1, clipFrame.height)
                    let dx = 0
                    let dy = 0
                    switch (event.key) {
                    case Qt.Key_Left:  dx = -stepX; break
                    case Qt.Key_Right: dx = stepX; break
                    case Qt.Key_Up:    dy = -stepY; break
                    case Qt.Key_Down:  dy = stepY; break
                    default: return
                    }
                    EditorState.beginPreviewDrag(qsTr("Mask changed"))
                    if (maskBox.isFreeform) {
                        maskBox.dragStartPoints = (maskBox.maskData.points || []).slice()
                        maskBox.writePointsTranslated(dx, dy)
                        maskBox.dragStartPoints = []
                    } else {
                        maskBox.dragStartX = maskBox.mx
                        maskBox.dragStartY = maskBox.my
                        maskBox.liveX = maskBox.mx + dx
                        maskBox.liveY = maskBox.my + dy
                        maskBox.writeLive()
                        maskBox.clearLive()
                    }
                    EditorState.commitPreviewDrag()
                    event.accepted = true
                }

                // The mask outline. Ellipse and the ornamental shapes are drawn as their bounding
                // box plus a hint, rather than reproducing the rasterizer's geometry in QML — the
                // point of the box is the handles, and the real shape is visible in the composite
                // behind it.
                MaskOverlayOutline {
                    selected: maskBox.isSelected
                    enabledMask: maskBox.maskData.enabled !== false
                    shape: maskBox.maskData.shape || ""
                }

                // Feather reaches beyond the shape's edge, so show how far. Drawn outside so it
                // reads as "the edge fades out to here".
                MaskOverlayFeatherRing {
                    selected: maskBox.isSelected
                    feather: maskBox.maskData.feather || 0
                    scaleX: root.sx
                    scaleY: root.sy
                    shape: maskBox.maskData.shape || ""
                }

                TapHandler {
                    onTapped: {
                        EditorState.selectClip(maskBox.layerTrack, maskBox.layerClip)
                        maskBox.forceActiveFocus()
                    }
                }

                // Body move. The pointer is read in scene coordinates and mapped into clipFrame,
                // which is the frame mask coordinates are normalized to. Going through the
                // transform chain is what makes this rotation-proof: there are two rotations
                // stacked here (the host clip's on clipFrame, the mask's own on this box), and
                // mapFromItem unwinds both. DragHandler.translation would arrive in the rotated
                // box's axes instead, and undoing that by hand sent a rotated mask sideways.
                //
                // The mask's own rotation must not enter the delta at all: dragging a rotated box
                // still means "follow the cursor".
                DragHandler {
                    id: bodyDrag
                    target: null
                    enabled: maskBox.movable && maskBox.isSelected

                    // Press point in clipFrame coordinates, which stands still while the box moves.
                    property real pressPx: 0
                    property real pressPy: 0

                    onActiveChanged: {
                        if (active) {
                            // Taken at activation rather than at the press, so the drag threshold
                            // is not replayed as a jump.
                            const p = clipFrame.mapFromItem(null,
                                                            bodyDrag.centroid.scenePosition.x,
                                                            bodyDrag.centroid.scenePosition.y)
                            bodyDrag.pressPx = p.x
                            bodyDrag.pressPy = p.y
                            maskBox.dragStartX = maskBox.mx
                            maskBox.dragStartY = maskBox.my
                            maskBox.liveX = maskBox.dragStartX
                            maskBox.liveY = maskBox.dragStartY
                            maskBox.dragStartPoints = (maskBox.maskData.points || []).slice()
                            root.interacting = true
                            maskBox.forceActiveFocus()
                            EditorState.beginPreviewDrag(qsTr("Mask changed"))
                        } else {
                            maskBox.clearLive()
                            maskBox.dragStartPoints = []
                            root.endInteraction()
                        }
                    }

                    onCentroidChanged: {
                        if (!active)
                            return
                        const p = clipFrame.mapFromItem(null,
                                                        bodyDrag.centroid.scenePosition.x,
                                                        bodyDrag.centroid.scenePosition.y)
                        let nx = maskBox.dragStartX
                               + (p.x - bodyDrag.pressPx) / Math.max(1, clipFrame.width)
                        let ny = maskBox.dragStartY
                               + (p.y - bodyDrag.pressPy) / Math.max(1, clipFrame.height)

                        // Ctrl bypasses snapping. Alt is unusable here — the window manager takes
                        // it for window drags on Linux.
                        const bypass = (bodyDrag.centroid.modifiers & Qt.ControlModifier) !== 0
                        if (maskBox.canSnap && !bypass) {
                            const halfW = maskBox.mw / 2
                            const halfH = maskBox.mh / 2
                            nx += root.snapAxis([nx - halfW, nx, nx + halfW],
                                                maskBox.snapTargets, maskBox.snapTolX).delta
                            ny += root.snapAxis([ny - halfH, ny, ny + halfH],
                                                maskBox.snapTargets, maskBox.snapTolY).delta
                        }

                        maskBox.liveX = nx
                        maskBox.liveY = ny
                        if (maskBox.isFreeform) {
                            maskBox.writePointsTranslated(nx - maskBox.dragStartX,
                                                          ny - maskBox.dragStartY)
                        } else {
                            maskBox.writeLive()
                        }
                    }
                }

                // Rotation handle, 28px above the box.
                MaskOverlayRotateHandle {
                    mask: maskBox
                    frame: clipFrame
                    overlay: root
                }

                // Polygon vertices. Drawn in the clip frame rather than the mask box, because the
                // box is only their bounding rect — dragging a vertex changes that rect, which
                // would otherwise move every other vertex under the cursor.
                // The Repeaters sit in holders parented to the clip frame, so their delegates stay
                // the Repeater's siblings; delegates reparenting themselves broke Repeater stacking.
                Item {
                    parent: clipFrame
                    anchors.fill: parent
                    z: 200

                    Repeater {
                        model: (maskBox.isSelected && maskBox.isFreeform
                                && maskBox.maskData.enabled !== false)
                               ? (maskBox.maskData.points || []).length : 0

                        delegate: MaskOverlayVertexHandle {
                            mask: maskBox
                            frame: clipFrame
                            overlay: root
                        }
                    }
                }

                // Edge midpoints: click one to split that edge.
                Item {
                    parent: clipFrame
                    anchors.fill: parent
                    z: 199

                    Repeater {
                        model: (maskBox.isSelected && maskBox.isFreeform
                                && maskBox.maskData.enabled !== false)
                               ? (maskBox.maskData.points || []).length : 0

                        delegate: MaskOverlayEdgeMidpoint {
                            mask: maskBox
                            frame: clipFrame
                        }
                    }
                }

                // Resize grips — the same frame the canvas crop tool and TransformOverlay use.
                Repeater {
                    model: (maskBox.editable && maskBox.isSelected)
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

                    delegate: MaskOverlayResizeGrip {
                        mask: maskBox
                        frame: clipFrame
                        overlay: root
                    }
                }
            }
        }

        // Bars, drawn as the two full-width bands it actually is. It has no bounding box to
        // grip: the rasterizer ignores x, y, w and rotation outright and derives both bands from
        // `h` alone, so a centred box with corner grips would be a lie. The one thing that does
        // mean something is the band edge, so that is what is draggable.
        Repeater {
            model: root.layerModel

            delegate: Item {
                id: barsEntry
                required property int index
                required property var modelData
                readonly property var layerEntry: root.layerValues[index] || modelData
                readonly property var maskData: layerEntry.mask || ({})
                readonly property bool isSelected: !!layerEntry.selected
                readonly property bool animated: !!layerEntry.animated
                anchors.fill: parent
                visible: maskData.shape === "bars" && maskData.enabled !== false

                // Live override during a drag, so the bands track the cursor without waiting for
                // the model to come back.
                property real liveH: -1
                readonly property real mh: liveH >= 0 ? liveH : (maskData.h || 0)
                // barH in the rasterizer is h * canvasHeight * 0.5, applied at both edges.
                readonly property real bandH: Math.max(1, mh * 0.5 * clipFrame.height)

                function writeHeight(value) {
                    barsEntry.liveH = Math.max(0.0, Math.min(2.0, value))
                    if (barsEntry.animated) {
                        EditorState.previewSetClipKeyframe(modelData.track, modelData.clip,
                                                           "mask.h",
                                                           EditorState.playheadSeconds,
                                                           barsEntry.liveH)
                        return
                    }
                    const mask = Object.assign({}, barsEntry.maskData)
                    mask.h = barsEntry.liveH
                    EditorState.previewSetClipMask(modelData.track, modelData.clip, mask)
                }

                TapHandler {
                    onTapped: EditorState.selectClip(modelData.track, modelData.clip)
                }

                Repeater {
                    model: 2
                    delegate: Item {
                        id: band
                        required property int index
                        readonly property bool atTop: band.index === 0
                        width: clipFrame.width
                        height: barsEntry.bandH
                        y: atTop ? 0 : clipFrame.height - barsEntry.bandH

                        MaskOverlayBarBand {
                            selected: barsEntry.isSelected
                        }

                        // The inner edge — the one `h` actually moves. Drawn as a bar rather than
                        // a dot because it spans the whole width, like the thing it resizes.
                        MaskOverlayBarEdge {
                            atTop: band.atTop
                            bars: barsEntry
                            frame: clipFrame
                            overlay: root
                        }
                    }
                }
            }
        }
    }

    // Nothing to manipulate: no clip under the playhead, or the clip has no mask layers.
    MaskOverlayEmptyHint {
        visible: !root.hasFrame || root.layers.length === 0
        hasFrame: root.hasFrame
    }

    MaskOverlayEmptyHint {
        visible: root.hasFrame && root.layers.some(l => l.selected && l.mask && l.mask.shape === "freeform")
        hasFrame: root.hasFrame
        message: Theme.touchInput
                 ? qsTr("Drag points to reshape · Tap ○ on an edge to add a point · Hold a point to delete")
                 : qsTr("Drag points to reshape · Click ○ on an edge to add a point · Right-click a point to delete")
    }
}
