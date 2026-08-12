import QtQuick
import Drift
import ".."

// Mask overlay: direct manipulation of the selected clip's mask stack. Geometry
// (x/y/width/height/z/visible) is driven by the owning PreviewPanel, which mirrors the canvas
// rect here so grips on a mask that runs past a canvas edge stay drawn and grabbable.
//
// Mask coordinates are normalized to the *clip's own frame*, not the canvas: the coverage map is
// rasterized at the layer's size and sampled at the layer's UV, so a mask travels with the clip's
// transform. That is why everything below hangs off `clipFrame`, an Item placed and rotated to
// match the clip, rather than off this root.
Item {
    id: root

    // The clip whose masks are being edited, as published by previewClipsAtPlayhead().
    property var clipInfo: null
    property var masks: []
    // True while a grip or body drag is running. Refreshing mid-drag would destroy the delegate
    // that owns the active grab, exactly as in TransformOverlay.
    property bool interacting: false

    // Index into the owning track's mask list for the entry with handles.
    readonly property int selectedIndex: EditorState.selectedMaskIndex
    property int maskTrackIndex: -1
    readonly property real canvasW: Math.max(1, clipInfo ? clipInfo.canvasWidth : 1)
    readonly property real canvasH: Math.max(1, clipInfo ? clipInfo.canvasHeight : 1)
    readonly property real sx: root.width / canvasW
    readonly property real sy: root.height / canvasH

    // Stickiness in normalized mask units. The tolerance is a screen distance so the pull feels
    // the same whatever the project resolution or preview zoom.
    readonly property real snapTolPx: 8

    function refreshOverlay() {
        if (interacting)
            return
        // Playhead ticks ~60 Hz; rebuilding grips every tick during playback is wasted work.
        if (EditorState.playing)
            return

        // Which track's lane to edit: the selected cutout's, falling back to the selected clip's
        // so the overlay is useful the moment a clip is picked.
        const track = EditorState.selectedMaskTrack >= 0 ? EditorState.selectedMaskTrack
                                                         : EditorState.selectedTrack
        if (track < 0) {
            clipInfo = null
            masks = []
            maskRepeater.model = []
            return
        }

        // Mask coordinates are normalized to the host clip's frame, so the overlay needs whichever
        // of that track's clips is under the playhead to place its handles.
        let found = null
        const previews = EditorState.previewClipsAtPlayhead()
        for (const entry of previews) {
            if (entry.track === track) {
                found = entry
                break
            }
        }
        clipInfo = found

        // Only the cutouts covering this frame have handles to show; the rest are elsewhere on
        // the lane and editing them here would be editing something you cannot see.
        const all = EditorState.trackMasks(track)
        const at = EditorState.playheadSeconds
        const next = []
        for (let i = 0; i < all.length; ++i) {
            const m = all[i]
            if (at >= m.start && at < m.start + m.duration) {
                m.laneIndex = i
                next.push(m)
            }
        }
        maskTrackIndex = track
        masks = next
        // Set imperatively for the same reason TransformOverlay does: binding the model
        // re-enters when tracksChanged fires during delegate setup.
        maskRepeater.model = next
    }

    function endInteraction() {
        EditorState.commitPreviewDrag()
        interacting = false
        Qt.callLater(refreshOverlay)
    }

    // Nearest target within `tol` of any candidate, returned as the delta to add to the moving
    // value. `guide` is the target that won, or -1 for no snap.
    function snapAxis(candidates, targets, tol) {
        let result = { delta: 0, guide: -1 }
        let best = tol
        for (const c of candidates) {
            for (const t of targets) {
                const d = t - c
                if (Math.abs(d) < best) {
                    best = Math.abs(d)
                    result = { delta: d, guide: t }
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
        function onSelectionChanged() { root.refreshOverlay() }
        function onSelectedMaskChanged() { root.refreshOverlay() }
        function onSelectedClipDataChanged() { root.refreshOverlay() }
        function onPlayheadSecondsChanged() { root.refreshOverlay() }
        function onPlayingChanged() {
            if (!EditorState.playing)
                root.refreshOverlay()
        }
    }

    // The clip's destination rect on the canvas, rotated with it. Mask boxes are children so
    // QML composes the two rotations instead of this file doing it by hand.
    Item {
        id: clipFrame
        visible: !!root.clipInfo
        x: root.clipInfo ? root.clipInfo.x * root.sx : 0
        y: root.clipInfo ? root.clipInfo.y * root.sy : 0
        width: root.clipInfo ? Math.max(1, root.clipInfo.width * root.sx) : 1
        height: root.clipInfo ? Math.max(1, root.clipInfo.height * root.sy) : 1
        transformOrigin: Item.Center
        rotation: root.clipInfo ? root.clipInfo.rotation : 0

        // The clip's own bounds, so it is obvious what the mask coordinates are relative to.
        Rectangle {
            anchors.fill: parent
            color: "transparent"
            border.width: Theme.borderWidth
            border.color: Theme.mutedForeground
            opacity: 0.35
        }

        Repeater {
            id: maskRepeater

            delegate: Item {
                id: maskBox
                required property var modelData
                required property int index

                readonly property bool isSelected: root.selectedIndex === maskBox.modelData.laneIndex
                readonly property bool isMedia: modelData.shape === "media"
                // Bars ignores x/y/w entirely — it is two full-width bands whose height comes
                // from h — so a centred box with corner grips would be a lie. It draws its real
                // bands below and is edited from the inspector.
                readonly property bool isBars: modelData.shape === "bars"
                // Freeform ignores x/y/w/h too: the points are the shape. Its box is their
                // bounding rect, shown for reference, and it is edited vertex by vertex — bbox
                // grips would appear to do something and do nothing.
                readonly property bool isFreeform: modelData.shape === "freeform"
                // Media has a rect and a rotation of its own now, so it takes grips like the
                // parametric shapes; only Bars and Freeform have no meaningful bounding box.
                readonly property bool editable: !isBars && !isFreeform && modelData.enabled
                readonly property bool movable: editable || (isFreeform && modelData.enabled)

                // Bounding rect of the polygon's vertices, in normalized clip-frame units.
                readonly property var pointBounds: {
                    const pts = maskBox.modelData.points || []
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
                        : (pointBounds ? pointBounds.x : (modelData.x || 0))
                readonly property real my: liveY > -1e11 ? liveY
                        : (pointBounds ? pointBounds.y : (modelData.y || 0))
                readonly property real mw: liveW >= 0 ? liveW
                        : (pointBounds ? pointBounds.w : (modelData.w || 0))
                readonly property real mh: liveH >= 0 ? liveH
                        : (pointBounds ? pointBounds.h : (modelData.h || 0))

                // Drag anchors, captured on press.
                property real dragStartX: 0
                property real dragStartY: 0
                property real dragStartW: 0
                property real dragStartH: 0

                visible: !maskBox.isBars
                x: (mx - mw / 2) * clipFrame.width
                y: (my - mh / 2) * clipFrame.height
                width: Math.max(1, mw * clipFrame.width)
                height: Math.max(1, mh * clipFrame.height)
                transformOrigin: Item.Center
                // A freeform's box is derived from its (already absolute) points, so rotating
                // it here would double-apply the rotation the rasterizer does.
                rotation: maskBox.isFreeform
                          ? 0 : (liveRotation < 1e8 ? liveRotation : (modelData.rotation || 0))
                // The selected entry sits on top so its grips win hit-testing over the outlines
                // of entries behind it.
                z: maskBox.isSelected ? 100 : 0

                // Normalized snap targets: the clip's edges and centre lines.
                readonly property var snapTargets: [0, 0.5, 1]
                readonly property real snapTolX: root.snapTolPx / Math.max(1, clipFrame.width)
                readonly property real snapTolY: root.snapTolPx / Math.max(1, clipFrame.height)
                // A rotated box has no axis-aligned edges to stick with.
                readonly property bool canSnap: Math.abs(maskBox.rotation) < 0.01

                // True once any of this entry's scalars has keys: the drag then has to write
                // through the keyframe API, or it would move the static value under an
                // animation that immediately overrides it again on the next frame.
                readonly property bool animated: !!maskBox.modelData.animated

                function writeLive() {
                    if (maskBox.animated) {
                        writeLiveKeyframes()
                        return
                    }
                    const mask = Object.assign({}, maskBox.modelData)
                    mask.x = maskBox.mx
                    mask.y = maskBox.my
                    mask.w = maskBox.mw
                    mask.h = maskBox.mh
                    if (maskBox.liveRotation < 1e8)
                        mask.rotation = maskBox.liveRotation
                    EditorState.previewSetTrackMaskAt(root.maskTrackIndex,
                                                      maskBox.modelData.laneIndex, mask)
                }

                // Writes go to the playhead. previewSetClipKeyframe honours autoKeyEnabled and
                // the retarget-nearest policy, so an existing key moves rather than a new one
                // appearing on every drag.
                function writeLiveKeyframes() {
                    const at = EditorState.playheadSeconds
                    const prefix = "mask." + maskBox.modelData.laneIndex + "."
                    // Mask props resolve against the track's lane, so the clip index only has to
                    // name the track — this is the clip the handles are placed against anyway.
                    const clipOnTrack = root.clipInfo ? root.clipInfo.clip : -1
                    const put = (param, value) => EditorState.previewSetClipKeyframe(
                        root.maskTrackIndex, clipOnTrack, prefix + param, at, value)
                    put("x", maskBox.mx)
                    put("y", maskBox.my)
                    put("w", maskBox.mw)
                    put("h", maskBox.mh)
                    if (maskBox.liveRotation < 1e8)
                        put("rotation", maskBox.liveRotation)
                }

                // Freeform has no centre to write, so a body drag shifts every vertex instead.
                function writePointsTranslated(dx, dy) {
                    const mask = Object.assign({}, maskBox.modelData)
                    const src = maskBox.dragStartPoints
                    const points = []
                    for (const p of src)
                        points.push(Object.assign({}, p, { "x": p.x + dx, "y": p.y + dy }))
                    mask.points = points
                    EditorState.previewSetTrackMaskAt(root.maskTrackIndex,
                                                      maskBox.modelData.laneIndex, mask)
                }

                // Vertex positions captured on press, so a body drag stays absolute rather than
                // accumulating rounding from each incremental write.
                property var dragStartPoints: []

                function clearLive() {
                    liveX = -1e12
                    liveY = -1e12
                    liveW = -1
                    liveH = -1
                    liveRotation = 1e9
                }

                // Arrow keys nudge the selected mask; Shift makes the step coarse. Focus is
                // taken explicitly on click, never bound to selection — a rebuild would
                // otherwise steal it from the properties panel.
                Keys.onPressed: function(event) {
                    if (!maskBox.isSelected || !maskBox.movable)
                        return
                    // One screen pixel, expressed in normalized units.
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
                        maskBox.dragStartPoints = (maskBox.modelData.points || []).slice()
                        maskBox.writePointsTranslated(dx, dy)
                        maskBox.dragStartPoints = []
                    } else {
                        const mask = Object.assign({}, maskBox.modelData)
                        mask.x = (maskBox.modelData.x || 0) + dx
                        mask.y = (maskBox.modelData.y || 0) + dy
                        EditorState.previewSetTrackMaskAt(root.maskTrackIndex,
                                                          maskBox.modelData.laneIndex, mask)
                    }
                    EditorState.commitPreviewDrag()
                    event.accepted = true
                }

                // The mask outline. Ellipse and the ornamental shapes are drawn as their
                // bounding box plus a hint, rather than reproducing the rasterizer's geometry
                // in QML — the point of the box is the handles, and the real shape is visible
                // in the composite behind it.
                Rectangle {
                    anchors.fill: parent
                    color: "transparent"
                    border.width: Theme.borderWidth
                    border.color: maskBox.isSelected ? Theme.primary : Theme.mutedForeground
                    opacity: maskBox.modelData.enabled ? (maskBox.isSelected ? 1.0 : 0.5) : 0.25
                    radius: maskBox.modelData.shape === "ellipse"
                            ? Math.min(width, height) / 2 : 0
                }

                // Feather reaches beyond the shape's edge, so show how far. Drawn outside so it
                // reads as "the edge fades out to here".
                Rectangle {
                    visible: maskBox.isSelected && (maskBox.modelData.feather || 0) > 0
                    anchors.centerIn: parent
                    width: parent.width + 2 * (maskBox.modelData.feather || 0) * root.sx
                    height: parent.height + 2 * (maskBox.modelData.feather || 0) * root.sy
                    color: "transparent"
                    border.width: Theme.borderWidth
                    border.color: Theme.primary
                    opacity: 0.35
                    radius: maskBox.modelData.shape === "ellipse"
                            ? Math.min(width, height) / 2 : 0
                }

                TapHandler {
                    onTapped: {
                        EditorState.selectMask(root.maskTrackIndex, maskBox.modelData.laneIndex)
                        maskBox.forceActiveFocus()
                    }
                }

                // Body move. Translation arrives in root axes, so it is rotated back into the
                // clip frame before being normalized.
                DragHandler {
                    id: bodyDrag
                    target: null
                    enabled: maskBox.movable && maskBox.isSelected

                    onActiveChanged: {
                        if (active) {
                            maskBox.dragStartX = maskBox.mx
                            maskBox.dragStartY = maskBox.my
                            maskBox.liveX = maskBox.dragStartX
                            maskBox.liveY = maskBox.dragStartY
                            maskBox.dragStartPoints = (maskBox.modelData.points || []).slice()
                            root.interacting = true
                            maskBox.forceActiveFocus()
                            EditorState.beginPreviewDrag(qsTr("Mask changed"))
                        } else {
                            maskBox.clearLive()
                            maskBox.dragStartPoints = []
                            root.endInteraction()
                        }
                    }

                    onTranslationChanged: {
                        if (!active)
                            return
                        // The handler reports translation in the rotated box's own frame; undo
                        // the box rotation to get clip-frame axes.
                        const a = maskBox.rotation * Math.PI / 180
                        const dx = translation.x * Math.cos(a) - translation.y * Math.sin(a)
                        const dy = translation.x * Math.sin(a) + translation.y * Math.cos(a)

                        let nx = maskBox.dragStartX + dx / Math.max(1, clipFrame.width)
                        let ny = maskBox.dragStartY + dy / Math.max(1, clipFrame.height)

                        // Ctrl bypasses snapping. Alt is unusable here — the window manager
                        // takes it for window drags on Linux.
                        const bypass = (bodyDrag.centroid.modifiers & Qt.ControlModifier) !== 0
                        if (maskBox.canSnap && !bypass) {
                            const halfW = maskBox.mw / 2
                            const halfH = maskBox.mh / 2
                            const snapX = root.snapAxis([nx - halfW, nx, nx + halfW],
                                                        maskBox.snapTargets, maskBox.snapTolX)
                            const snapY = root.snapAxis([ny - halfH, ny, ny + halfH],
                                                        maskBox.snapTargets, maskBox.snapTolY)
                            nx += snapX.delta
                            ny += snapY.delta
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
                Rectangle {
                    id: rotateHandle
                    visible: maskBox.editable && maskBox.isSelected
                    width: Theme.spacingLg
                    height: Theme.spacingLg
                    radius: width / 2
                    color: rotateArea.containsMouse || rotateArea.pressed
                           ? Theme.primaryForeground : Theme.primary
                    border.width: Theme.borderWidth
                    border.color: rotateArea.containsMouse || rotateArea.pressed
                                  ? Theme.primary : Theme.primaryForeground
                    x: parent.width / 2 - width / 2
                    y: -28 - height / 2

                    MouseArea {
                        id: rotateArea
                        anchors.fill: parent
                        anchors.margins: -Theme.spacingMd
                        hoverEnabled: true
                        cursorShape: Qt.CrossCursor
                        preventStealing: true
                        onWheel: (wheel) => { wheel.accepted = false }

                        onPressed: {
                            root.interacting = true
                            maskBox.forceActiveFocus()
                            EditorState.beginPreviewDrag(qsTr("Mask changed"))
                        }

                        onPositionChanged: (mouse) => {
                            if (!pressed)
                                return
                            // Angle is measured in the clip frame, so map through it rather
                            // than through the rotating box.
                            const p = mapToItem(clipFrame, mouse.x, mouse.y)
                            const cx = maskBox.mx * clipFrame.width
                            const cy = maskBox.my * clipFrame.height
                            let deg = Math.atan2(p.y - cy, p.x - cx) * 180 / Math.PI + 90
                            if ((mouse.modifiers & Qt.ShiftModifier) !== 0)
                                deg = Math.round(deg / 15) * 15
                            maskBox.liveRotation = deg
                            maskBox.writeLive()
                        }

                        onReleased: { maskBox.clearLive(); root.endInteraction() }
                        onCanceled: { maskBox.clearLive(); root.endInteraction() }
                    }
                }

                // Polygon vertices. Drawn in the clip frame rather than the mask box, because
                // the box is only their bounding rect — dragging a vertex changes that rect,
                // which would otherwise move every other vertex under the cursor.
                Repeater {
                    model: (maskBox.isSelected && maskBox.modelData.shape === "freeform"
                            && maskBox.modelData.enabled)
                           ? (maskBox.modelData.points || []) : []

                    delegate: Rectangle {
                        id: vertex
                        required property var modelData
                        required property int index

                        readonly property real vs: Theme.spacingMd
                        width: vs
                        height: vs
                        radius: vs / 2
                        color: vertexArea.containsMouse || vertexArea.pressed
                               ? Theme.primaryForeground : Theme.primary
                        border.width: Theme.borderWidth
                        border.color: Theme.primaryForeground

                        // Live position during a drag, so the dot tracks the cursor without
                        // waiting for the model to come back.
                        property real liveVx: -1e12
                        property real liveVy: -1e12
                        readonly property real vx: liveVx > -1e11 ? liveVx : modelData.x
                        readonly property real vy: liveVy > -1e11 ? liveVy : modelData.y

                        // Positioned against the clip frame, then mapped into this box's
                        // (rotated) coordinate system.
                        parent: clipFrame
                        x: vx * clipFrame.width - vs / 2
                        y: vy * clipFrame.height - vs / 2
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

                            onPressed: (mouse) => {
                                if (mouse.button === Qt.RightButton) {
                                    EditorState.removeMaskPoint(root.maskTrackIndex,
                                                                maskBox.modelData.laneIndex,
                                                                vertex.index)
                                    return
                                }
                                vertex.liveVx = vertex.modelData.x
                                vertex.liveVy = vertex.modelData.y
                                root.interacting = true
                                maskBox.forceActiveFocus()
                                EditorState.beginPreviewDrag(qsTr("Mask point moved"))
                            }

                            onPositionChanged: (mouse) => {
                                if (!pressed || mouse.buttons !== Qt.LeftButton)
                                    return
                                const p = mapToItem(clipFrame, mouse.x, mouse.y)
                                let nx = p.x / Math.max(1, clipFrame.width)
                                let ny = p.y / Math.max(1, clipFrame.height)
                                if ((mouse.modifiers & Qt.ControlModifier) === 0) {
                                    const snapX = root.snapAxis([nx], maskBox.snapTargets,
                                                                maskBox.snapTolX)
                                    const snapY = root.snapAxis([ny], maskBox.snapTargets,
                                                                maskBox.snapTolY)
                                    nx += snapX.delta
                                    ny += snapY.delta
                                }
                                vertex.liveVx = nx
                                vertex.liveVy = ny

                                const mask = Object.assign({}, maskBox.modelData)
                                const points = (mask.points || []).slice()
                                points[vertex.index] = Object.assign({}, points[vertex.index],
                                                                     { "x": nx, "y": ny })
                                mask.points = points
                                EditorState.previewSetTrackMaskAt(root.maskTrackIndex,
                                                          maskBox.modelData.laneIndex, mask)
                            }

                            function finish() {
                                if (vertex.liveVx < -1e11)
                                    return
                                vertex.liveVx = -1e12
                                vertex.liveVy = -1e12
                                root.endInteraction()
                            }

                            onReleased: finish()
                            onCanceled: finish()
                        }
                    }
                }

                // Edge midpoints: click one to split that edge.
                Repeater {
                    model: (maskBox.isSelected && maskBox.modelData.shape === "freeform"
                            && maskBox.modelData.enabled)
                           ? (maskBox.modelData.points || []) : []

                    delegate: Rectangle {
                        id: midpoint
                        required property var modelData
                        required property int index

                        // The edge from this vertex to the next, wrapping at the end.
                        readonly property var nextPoint: {
                            const pts = maskBox.modelData.points || []
                            return pts[(midpoint.index + 1) % pts.length]
                        }
                        readonly property real mxN: nextPoint
                                ? (modelData.x + nextPoint.x) / 2 : modelData.x
                        readonly property real myN: nextPoint
                                ? (modelData.y + nextPoint.y) / 2 : modelData.y

                        parent: clipFrame
                        width: Theme.spacingSm
                        height: Theme.spacingSm
                        radius: width / 2
                        color: "transparent"
                        border.width: Theme.borderWidth
                        border.color: Theme.primary
                        opacity: midArea.containsMouse ? 1.0 : 0.5
                        x: mxN * clipFrame.width - width / 2
                        y: myN * clipFrame.height - height / 2
                        z: 199

                        MouseArea {
                            id: midArea
                            anchors.fill: parent
                            anchors.margins: -Theme.spacingSm
                            hoverEnabled: true
                            cursorShape: Qt.CrossCursor
                            preventStealing: true
                            onWheel: (wheel) => { wheel.accepted = false }
                            // Inserting before the far end of the edge splits that edge.
                            onClicked: EditorState.insertMaskPoint(
                                root.maskTrackIndex, maskBox.modelData.laneIndex,
                                midpoint.index + 1, midpoint.mxN, midpoint.myN)
                        }
                    }
                }

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

                    delegate: Rectangle {
                        id: grip
                        required property var modelData

                        readonly property real hs: Theme.spacingLg
                        readonly property bool isCorner: modelData.dx !== 0 && modelData.dy !== 0

                        width: hs
                        height: hs
                        radius: Theme.radiusXs
                        color: gripArea.containsMouse || gripArea.pressed
                               ? Theme.primaryForeground : Theme.primary
                        border.width: Theme.borderWidth
                        border.color: gripArea.containsMouse || gripArea.pressed
                                      ? Theme.primary : Theme.primaryForeground

                        x: (modelData.dx === 0 ? maskBox.width / 2
                                               : (modelData.dx < 0 ? 0 : maskBox.width)) - hs / 2
                        y: (modelData.dy === 0 ? maskBox.height / 2
                                               : (modelData.dy < 0 ? 0 : maskBox.height)) - hs / 2

                        // Press point in clip-frame coordinates. The grip rides the box as it
                        // resizes, so deltas are measured against the frame, which stands still.
                        property real startPx: 0
                        property real startPy: 0

                        // Resize about the fixed opposite edge/corner. The maths runs in the
                        // box's own axes so a rotated mask grows along the direction the grip
                        // points; the resulting centre shift is rotated back into the clip
                        // frame at the end.
                        function resizeTo(px, py, modifiers) {
                            const dxSign = grip.modelData.dx
                            const dySign = grip.modelData.dy
                            const a = maskBox.rotation * Math.PI / 180
                            const ddx = (px - grip.startPx) / Math.max(1, clipFrame.width)
                            const ddy = (py - grip.startPy) / Math.max(1, clipFrame.height)
                            // Clip axes -> box axes.
                            const lx = ddx * Math.cos(a) + ddy * Math.sin(a)
                            const ly = -ddx * Math.sin(a) + ddy * Math.cos(a)

                            let w = Math.max(0.01, maskBox.dragStartW + lx * dxSign)
                            let h = Math.max(0.01, maskBox.dragStartH + ly * dySign)

                            // Shift locks the ratio on a corner drag.
                            if (grip.isCorner && (modifiers & Qt.ShiftModifier) !== 0) {
                                const s = Math.max(w / Math.max(0.01, maskBox.dragStartW),
                                                   h / Math.max(0.01, maskBox.dragStartH))
                                w = Math.max(0.01, maskBox.dragStartW * s)
                                h = Math.max(0.01, maskBox.dragStartH * s)
                            }

                            // Keeping the anchor still means the centre moves by half the size
                            // change, toward the grip, in box axes.
                            const shiftX = (w - maskBox.dragStartW) / 2 * dxSign
                            const shiftY = (h - maskBox.dragStartH) / 2 * dySign
                            maskBox.liveX = maskBox.dragStartX
                                          + shiftX * Math.cos(a) - shiftY * Math.sin(a)
                            maskBox.liveY = maskBox.dragStartY
                                          + shiftX * Math.sin(a) + shiftY * Math.cos(a)
                            maskBox.liveW = w
                            maskBox.liveH = h
                            maskBox.writeLive()
                        }

                        MouseArea {
                            id: gripArea
                            anchors.fill: parent
                            // Generous invisible margin: the visible dot stays small enough not
                            // to hide the box edge it sits on.
                            anchors.margins: -Theme.spacingMd
                            hoverEnabled: true
                            cursorShape: grip.modelData.cursor
                            // The body DragHandler would otherwise take the grab once the drag
                            // threshold is passed, turning a resize into a move.
                            preventStealing: true

                            // Zooming with the pointer on a grip should still zoom.
                            onWheel: (wheel) => { wheel.accepted = false }

                            onPressed: (mouse) => {
                                const p = mapToItem(clipFrame, mouse.x, mouse.y)
                                grip.startPx = p.x
                                grip.startPy = p.y
                                maskBox.dragStartX = maskBox.mx
                                maskBox.dragStartY = maskBox.my
                                maskBox.dragStartW = maskBox.mw
                                maskBox.dragStartH = maskBox.mh
                                maskBox.liveX = maskBox.dragStartX
                                maskBox.liveY = maskBox.dragStartY
                                maskBox.liveW = maskBox.dragStartW
                                maskBox.liveH = maskBox.dragStartH
                                root.interacting = true
                                maskBox.forceActiveFocus()
                                EditorState.beginPreviewDrag(qsTr("Mask changed"))
                            }

                            onPositionChanged: (mouse) => {
                                if (!pressed)
                                    return
                                const p = mapToItem(clipFrame, mouse.x, mouse.y)
                                grip.resizeTo(p.x, p.y, mouse.modifiers)
                            }

                            onReleased: { maskBox.clearLive(); root.endInteraction() }
                            onCanceled: { maskBox.clearLive(); root.endInteraction() }
                        }
                    }
                }
            }
        }

        // Bars entries, drawn as the two full-width bands they actually are.
        Repeater {
            model: root.masks

            delegate: Item {
                id: barsEntry
                required property var modelData
                required property int index
                anchors.fill: parent
                visible: modelData.shape === "bars" && modelData.enabled

                // barH in the rasterizer is h * canvasHeight * 0.5, applied at both edges.
                readonly property real bandH:
                    Math.max(1, (barsEntry.modelData.h || 0) * 0.5 * clipFrame.height)

                Repeater {
                    model: 2
                    delegate: Rectangle {
                        required property int index
                        width: clipFrame.width
                        height: barsEntry.bandH
                        y: index === 0 ? 0 : clipFrame.height - barsEntry.bandH
                        color: "transparent"
                        border.width: Theme.borderWidth
                        border.color: Theme.mutedForeground
                        opacity: 0.5
                    }
                }
            }
        }
    }

    // Dropping media here adds it to the stack. Two mime types because the app has two drag
    // sources: the asset library's internal drag carries an index as text/plain (and mirrors it
    // on EditorState.draggingAssetIndex, which the timeline prefers), and the desktop sends
    // text/uri-list. Declared last so it sits above the grips only while a drag is in flight.
    DropArea {
        id: mediaDrop
        anchors.fill: parent
        z: 400
        keys: ["text/plain", "text/uri-list"]

        onDropped: (drop) => {
            const track = root.maskTrackIndex >= 0 ? root.maskTrackIndex
                                                   : EditorState.selectedTrack
            if (track < 0) {
                drop.accepted = false
                return
            }

            let added = -1
            if (drop.hasUrls && drop.urls.length > 0) {
                for (const url of drop.urls) {
                    const at = EditorState.addTrackMaskMedia(track, url.toString())
                    if (at >= 0)
                        added = at
                }
            } else if (EditorState.draggingAssetIndex >= 0) {
                added = EditorState.addTrackMaskFromAsset(track, EditorState.draggingAssetIndex)
            }

            if (added >= 0) {
                EditorState.selectMask(track, added)
                drop.accept()
            } else {
                drop.accepted = false
            }
        }
    }

    Rectangle {
        visible: mediaDrop.containsDrag
        anchors.fill: parent
        z: 401
        color: Theme.primary
        opacity: 0.15
        border.width: Theme.borderWidth * 2
        border.color: Theme.primary
    }

    // Nothing to manipulate: no clip under the playhead, or the clip has no editable entries.
    Rectangle {
        visible: !root.clipInfo || root.masks.length === 0
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: Theme.spacingLg
        width: hintText.width + Theme.spacingLg * 2
        height: hintText.height + Theme.spacingMd * 2
        radius: Theme.radiusSm
        color: Theme.panelBackground
        opacity: 0.9

        Text {
            id: hintText
            anchors.centerIn: parent
            text: root.clipInfo ? qsTr("Add a cutout in the Cutouts tab to edit it here")
                                : qsTr("Select a clip at the playhead to edit its cutouts")
            color: Theme.mutedForeground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeXs
        }
    }
}
