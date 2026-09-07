import QtQuick
import Drift
import ".."

// Mask editor: direct manipulation of the selected clip's mask layers. Geometry is driven by the
// owning PreviewPanel, which mirrors the canvas rect here so grips on a mask that runs past a
// canvas edge stay drawn and grabbable.
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
        if (interacting)
            return
        // Playhead ticks ~60 Hz; rebuilding grips every tick during playback is wasted work.
        if (EditorState.playing)
            return
        const next = EditorState.maskEditorState()
        editorState = next
        // Set imperatively for the same reason TransformOverlay does: binding the model
        // re-enters when tracksChanged fires during delegate setup.
        maskRepeater.model = next.layers || []
    }

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
        function onSelectionChanged() { root.refreshOverlay() }
        function onSelectedClipDataChanged() { root.refreshOverlay() }
        function onPlayheadSecondsChanged() { root.refreshOverlay() }
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
        rotation: root.editorState.rotation || 0

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

                readonly property bool isSelected: !!modelData.selected
                readonly property var maskData: modelData.mask || ({})
                readonly property int layerTrack: modelData.track
                readonly property int layerClip: modelData.clip
                // Once any scalar has keys the drag has to write through the keyframe API, or it
                // would move the static value under an animation that immediately overrides it
                // again on the next frame.
                readonly property bool animated: !!modelData.animated

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
                Rectangle {
                    anchors.fill: parent
                    color: "transparent"
                    border.width: Theme.borderWidth
                    border.color: maskBox.isSelected ? Theme.primary : Theme.mutedForeground
                    opacity: maskBox.maskData.enabled === false
                             ? 0.25 : (maskBox.isSelected ? 1.0 : 0.5)
                    radius: maskBox.maskData.shape === "ellipse"
                            ? Math.min(width, height) / 2 : 0
                }

                // Feather reaches beyond the shape's edge, so show how far. Drawn outside so it
                // reads as "the edge fades out to here".
                Rectangle {
                    visible: maskBox.isSelected && (maskBox.maskData.feather || 0) > 0
                    anchors.centerIn: parent
                    width: parent.width + 2 * (maskBox.maskData.feather || 0) * root.sx
                    height: parent.height + 2 * (maskBox.maskData.feather || 0) * root.sy
                    color: "transparent"
                    border.width: Theme.borderWidth
                    border.color: Theme.primary
                    opacity: 0.35
                    radius: maskBox.maskData.shape === "ellipse"
                            ? Math.min(width, height) / 2 : 0
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
                Rectangle {
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
                            // Angle is measured in the clip frame, so map through it rather than
                            // through the rotating box.
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

                // Polygon vertices. Drawn in the clip frame rather than the mask box, because the
                // box is only their bounding rect — dragging a vertex changes that rect, which
                // would otherwise move every other vertex under the cursor.
                Repeater {
                    model: (maskBox.isSelected && maskBox.isFreeform
                            && maskBox.maskData.enabled !== false)
                           ? (maskBox.maskData.points || []) : []

                    delegate: Rectangle {
                        id: vertex
                        required property var modelData
                        required property int index

                        // Deliberately larger than the resize grips: a vertex is a point target
                        // rather than an edge to catch, and it is the only way to reshape a
                        // freeform, so it has to be easy to hit.
                        readonly property real vs: Theme.spacingLg * 1.25
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
                                    EditorState.removeMaskPoint(maskBox.layerTrack,
                                                                maskBox.layerClip, vertex.index)
                                    return
                                }
                                root.interacting = true
                                maskBox.forceActiveFocus()
                                EditorState.beginPreviewDrag(qsTr("Mask changed"))
                            }

                            onPositionChanged: (mouse) => {
                                if (!pressed || mouse.buttons !== Qt.LeftButton)
                                    return
                                const p = mapToItem(clipFrame, mouse.x, mouse.y)
                                vertex.liveVx = p.x / Math.max(1, clipFrame.width)
                                vertex.liveVy = p.y / Math.max(1, clipFrame.height)
                                maskBox.writePointAt(vertex.index, vertex.liveVx, vertex.liveVy)
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
                    model: (maskBox.isSelected && maskBox.isFreeform
                            && maskBox.maskData.enabled !== false)
                           ? (maskBox.maskData.points || []) : []

                    delegate: Rectangle {
                        id: midpoint
                        required property var modelData
                        required property int index

                        // The edge from this vertex to the next, wrapping at the end.
                        readonly property var nextPoint: {
                            const pts = maskBox.maskData.points || []
                            return pts[(midpoint.index + 1) % pts.length]
                        }
                        readonly property real mxN: nextPoint
                                ? (modelData.x + nextPoint.x) / 2 : modelData.x
                        readonly property real myN: nextPoint
                                ? (modelData.y + nextPoint.y) / 2 : modelData.y

                        parent: clipFrame
                        width: Theme.spacingMd
                        height: Theme.spacingMd
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
                                maskBox.layerTrack, maskBox.layerClip,
                                midpoint.index + 1, midpoint.mxN, midpoint.myN)
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

                        // Resize about the fixed opposite edge/corner. The maths runs in the box's
                        // own axes so a rotated mask grows along the direction the grip points;
                        // the resulting centre shift is rotated back into the clip frame at the end.
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
                            // Generous invisible margin: the visible dot stays small enough not to
                            // hide the box edge it sits on.
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

        // Bars, drawn as the two full-width bands it actually is. It has no bounding box to
        // grip: the rasterizer ignores x, y, w and rotation outright and derives both bands from
        // `h` alone, so a centred box with corner grips would be a lie. The one thing that does
        // mean something is the band edge, so that is what is draggable.
        Repeater {
            model: root.layers

            delegate: Item {
                id: barsEntry
                required property var modelData
                readonly property var maskData: modelData.mask || ({})
                readonly property bool isSelected: !!modelData.selected
                readonly property bool animated: !!modelData.animated
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

                        Rectangle {
                            anchors.fill: parent
                            color: "transparent"
                            border.width: Theme.borderWidth
                            border.color: barsEntry.isSelected ? Theme.primary
                                                               : Theme.mutedForeground
                            opacity: barsEntry.isSelected ? 1.0 : 0.5
                        }

                        // The inner edge — the one `h` actually moves. Drawn as a bar rather than
                        // a dot because it spans the whole width, like the thing it resizes.
                        Rectangle {
                            visible: barsEntry.isSelected
                            width: Theme.spacingLg * 2
                            height: Theme.spacingSm
                            radius: Theme.radiusXs
                            x: (parent.width - width) / 2
                            y: (band.atTop ? parent.height : 0) - height / 2
                            color: edgeArea.containsMouse || edgeArea.pressed
                                   ? Theme.primaryForeground : Theme.primary
                            border.width: Theme.borderWidth
                            border.color: edgeArea.containsMouse || edgeArea.pressed
                                          ? Theme.primary : Theme.primaryForeground

                            MouseArea {
                                id: edgeArea
                                anchors.fill: parent
                                anchors.margins: -Theme.spacingMd
                                hoverEnabled: true
                                cursorShape: Qt.SizeVerCursor
                                preventStealing: true
                                onWheel: (wheel) => { wheel.accepted = false }

                                onPressed: {
                                    root.interacting = true
                                    EditorState.beginPreviewDrag(qsTr("Mask changed"))
                                }

                                onPositionChanged: (mouse) => {
                                    if (!pressed)
                                        return
                                    const p = mapToItem(clipFrame, mouse.x, mouse.y)
                                    // Both bands come from one number, so the bottom edge is
                                    // measured from the bottom of the frame.
                                    const depth = band.atTop ? p.y : clipFrame.height - p.y
                                    barsEntry.writeHeight(
                                        2.0 * depth / Math.max(1, clipFrame.height))
                                }

                                function finish() {
                                    barsEntry.liveH = -1
                                    root.endInteraction()
                                }

                                onReleased: finish()
                                onCanceled: finish()
                            }
                        }
                    }
                }
            }
        }
    }

    // Nothing to manipulate: no clip under the playhead, or the clip has no mask layers.
    Rectangle {
        visible: !root.hasFrame || root.layers.length === 0
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
            text: root.hasFrame ? qsTr("Add a mask in the Cutouts tab to edit it here")
                                : qsTr("Select a clip at the playhead to edit its masks")
            color: Theme.mutedForeground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeXs
        }
    }
}
