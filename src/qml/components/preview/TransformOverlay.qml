import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Shapes
import Drift
import ".."

// Transform overlay: resize/rotate grips for the clips visible at the playhead.
// (The in-place text editor is still wired up but no longer reachable: the
// properties panel owns text editing until the editor matches the render.) Sits outside the (clipped) canvas rect,
// mirroring its geometry, so grips on a clip that runs past a canvas edge stay
// drawn and grabbable instead of being cut away with the frame. Geometry
// (x/y/width/height/z/visible) is driven by the owning PreviewPanel.
Item {
    id: root

    property var overlayClips: []
    // True while a handle is being dragged. Rebuilding the model
    // mid-drag would destroy the delegate that owns the active
    // grab, so refreshes are suppressed until the drag ends.
    // (Also held true while a text clip is edited in place, so
    // the inline editor delegate is not destroyed mid-edit.)
    property bool interacting: false

    // In 3D mode the scene camera, as a gizmo box at its eye (see previewCameraBox).
    property var cameraBox: null

    // The two views edit differently: the camera output takes the flat grips (move, resize,
    // rotate) on every clip, the 3D scene only the 3D gizmo, and only on 3D layers and the camera.
    readonly property bool mode3d: EditorState.previewMode === "3d"

    // The selected clip's gizmo, when it is a 3D layer, and its pose while a handle is dragged.
    readonly property var gizmoBox: {
        if (!mode3d)
            return null
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

    // A transform layer being moved: the clips under it are drawn from a model that is not
    // rebuilt mid-drag, so their outlines would hang in the old place and are hidden meanwhile.
    property int movingLayerTrack: -1
    readonly property int hiddenChildrenOf: movingLayerTrack >= 0 ? movingLayerTrack
        : (gizmoLive && gizmoBox && gizmoBox.kind === "transform" ? gizmoBox.track : -1)

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

    // "track:clip" of the text clip currently edited in place,
    // or "" when no inline edit is active.
    property string editingKey: ""

    // "track:clip" of a freshly added placeholder text clip that
    // should open its editor as soon as its box exists.
    property string pendingEditKey: ""

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
        const camera = EditorState.previewCameraBox()
        cameraBox = camera && camera.kind === "camera" ? camera : null
        const next = EditorState.previewClipsAtPlayhead()
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
        function onEditorViewChanged() { root.refreshOverlay() }
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
                ? EditorState.previewParentOverlayMatrix(box, handle.sx).inverted() : null
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
            visible: (!isTransform || selected) && !childOfMovingLayer
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
            readonly property bool editing: root.editingKey
                                                    === (box.track + ":" + box.clip)
            // True when this clip was just added with no text and should
            // open with an empty editor instead of the placeholder string.
            property bool openAsPlaceholder: false
            // Live clip style — tracks EditorState so property-panel edits
            // apply to the inline editor in real time.
            readonly property var liveStyle: {
                void EditorState.tracksRevision
                const info = EditorState.clipAt(box.track, box.clip)
                return info && info.textStyle ? info.textStyle : null
            }
            readonly property var liveClip: {
                void EditorState.tracksRevision
                return EditorState.clipAt(box.track, box.clip)
            }

            function enterEdit() {
                root.editingKey = box.track + ":" + box.clip
                root.interacting = true
                EditorState.selectClip(box.track, box.clip)
                EditorState.beginTextEdit(box.track, box.clip)
                const info = EditorState.clipAt(box.track, box.clip)
                if (handle.openAsPlaceholder) {
                    editor.text = ""
                    EditorState.previewSetClipTextContent(box.track, box.clip, "")
                    handle.openAsPlaceholder = false
                } else {
                    editor.text = info.textContent || ""
                }
                editor.forceActiveFocus()
                editor.selectAll()
            }

            function commitEdit() {
                if (!handle.editing)
                    return
                EditorState.commitTextEdit(box.track, box.clip, editor.text)
                handle.finishEdit()
            }

            function cancelEdit() {
                if (!handle.editing)
                    return
                EditorState.cancelPreviewDrag()
                handle.finishEdit()
            }

            function finishEdit() {
                root.editingKey = ""
                EditorState.endTextEdit()
                root.interacting = false
                Qt.callLater(root.refreshOverlay)
            }

            // A just-added placeholder clip opens its own editor.
            // Checked both on creation and when the key arrives,
            // since either can happen first.
            function claimPendingEdit() {
                if (!handle.isText || handle.editing)
                    return
                if (root.pendingEditKey
                        !== (box.track + ":" + box.clip))
                    return
                root.pendingEditKey = ""
                handle.openAsPlaceholder = true
                Qt.callLater(handle.enterEdit)
            }

            Component.onCompleted: handle.claimPendingEdit()

            Connections {
                target: root
                function onPendingEditKeyChanged() { handle.claimPendingEdit() }
            }

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
            // still the one the pointer reaches. The clip being edited
            // jumps above everything so its editor and the click-away
            // catcher order correctly.
            z: handle.editing ? 1000 : handle.selected ? 900 : -box.track
            transformOrigin: Item.Center
            readonly property real layoutRotation: gizmoPose ? gizmoPose.rotation
                                                   : liveRotation < 1e8 ? liveRotation : box.rotation
            rotation: is3d || hasParent || viaCamera ? 0 : layoutRotation
            transform: Matrix4x4 {
                id: poseTransform
                matrix: (void EditorState.editorViewRevision, handle.is3d || handle.hasParent || handle.viaCamera)
                        ? EditorState.previewClipPoseMatrix({
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
                                            && EditorState.previewMode !== "3d"

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
                if (!handle.selected || handle.editing || root.mode3d)
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
                EditorState.previewSetClipPosition(
                    handle.box.track,
                    handle.box.clip,
                    handle.box.x + dx,
                    handle.box.y + dy)
                EditorState.commitPreviewDrag()
                event.accepted = true
            }

            Connections {
                target: EditorState
                function onSelectedClipDataChanged() {
                    if (!handle.editing || editor.activeFocus)
                        return
                    const info = EditorState.clipAt(box.track, box.clip)
                    if (!info)
                        return
                    const next = info.textContent || ""
                    if (editor.text !== next)
                        editor.text = next
                }
            }

            Rectangle {
                anchors.fill: parent
                color: "transparent"
                border.width: (handle.selected || handle.editing)
                              ? Theme.borderWidthFocus : Theme.borderWidth
                border.color: handle.selected ? (handle.isTransform ? Theme.clipTransform : Theme.primary)
                              : handle.coveredBySelectedLayer ? Theme.clipTransform
                              : hover.hovered ? Theme.guideStrong : "transparent"
                radius: Theme.radiusXs

                Behavior on border.width {
                    NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
                }
                Behavior on border.color {
                    ColorAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
                }
            }

            // In-place text editor (Canva-style). Shown over the
            // box while editing; the baked raster is hidden by the
            // compositor via beginTextEdit. Plain TextArea (not a
            // Themed* control): this is canvas content that must
            // match the rendered text, not app chrome.
            TextArea {
                id: editor
                anchors.fill: parent
                visible: handle.editing
                enabled: handle.editing
                background: null
                renderType: TextEdit.NativeRendering
                padding: handle.liveStyle && handle.liveStyle.boxEnabled
                          ? Math.max(0, handle.liveStyle.boxPadding * handle.sy) : 0
                selectByMouse: true
                color: handle.liveStyle ? handle.liveStyle.color : "white"
                font.family: handle.liveStyle ? handle.liveStyle.fontFamily : Theme.fontFamily
                font.pixelSize: handle.liveStyle
                                ? Math.max(1, Math.round(handle.liveStyle.pixelSize * handle.sy))
                                : 16
                font.weight: handle.liveStyle ? handle.liveStyle.fontWeight : Font.Normal
                font.italic: handle.liveStyle ? handle.liveStyle.italic : false
                font.letterSpacing: handle.liveStyle ? handle.liveStyle.letterSpacing * handle.sy : 0
                wrapMode: (handle.liveStyle && handle.liveStyle.wordWrap === false)
                          ? TextEdit.NoWrap : TextEdit.WordWrap
                horizontalAlignment: !handle.liveStyle ? TextEdit.AlignHCenter
                                     : handle.liveStyle.align === "left" ? TextEdit.AlignLeft
                                     : handle.liveStyle.align === "right" ? TextEdit.AlignRight
                                     : TextEdit.AlignHCenter
                verticalAlignment: !handle.liveStyle ? TextEdit.AlignVCenter
                                   : handle.liveStyle.valign === "top" ? TextEdit.AlignTop
                                   : handle.liveStyle.valign === "bottom" ? TextEdit.AlignBottom
                                   : TextEdit.AlignVCenter
                onTextChanged: {
                    if (!handle.editing)
                        return
                    EditorState.previewSetClipTextContent(
                        handle.box.track,
                        handle.box.clip,
                        text)
                }
                Keys.onEscapePressed: handle.cancelEdit()
                onActiveFocusChanged: if (!activeFocus && handle.editing) handle.commitEdit()
            }

            HoverHandler {
                id: hover
            }

            TapHandler {
                enabled: !handle.editing
                onTapped: (eventPoint) => {
                    // Tapping inside a selected layer's frame picks the clip under the pointer,
                    // so the group's children stay one tap away.
                    if (handle.isTransform) {
                        const p = root.mapFromItem(null, eventPoint.scenePosition.x,
                                                   eventPoint.scenePosition.y)
                        const hit = EditorState.previewClipAtCanvasPoint(p.x / handle.sx, p.y / handle.sy)
                        if (hit && hit.track !== undefined) {
                            EditorState.selectClip(hit.track, hit.clip)
                            return
                        }
                    }
                    EditorState.selectClip(handle.box.track, handle.box.clip)
                    handle.forceActiveFocus()
                }
            }

            // "Name · n clips" over a selected layer's frame.
            Rectangle {
                visible: handle.isTransform && handle.selected
                x: 0
                y: -height - Theme.spacingSm
                width: layerLabel.implicitWidth + Theme.spacingLg
                height: layerLabel.implicitHeight + Theme.spacingSm
                radius: Theme.radiusSm
                color: Theme.clipTransform

                Text {
                    id: layerLabel
                    anchors.centerIn: parent
                    text: qsTr("%1 · %n clip(s)", "", handle.box.childCount || 0)
                              .arg(handle.box.name || qsTr("Transform"))
                    color: Theme.onMedia
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeXs
                }
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
                enabled: handle.selected && !handle.editing && !handle.resizing && !root.mode3d
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
                    const moved = EditorState.previewSnapMove(bodyDrag.startBox, bodyDrag.pressPx, bodyDrag.pressPy,
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
                model: (handle.selected && !handle.editing && !root.mode3d)
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
                    // Corners drive both axes, edges only one — so an edge drag
                    // is the deliberate way to stretch and corners are free to
                    // keep the ratio.
                    readonly property bool isCorner: modelData.dx !== 0 && modelData.dy !== 0
                    // Footage has a real aspect to protect, so its corners hold
                    // the ratio unless Shift asks for a stretch. Boxes that exist
                    // to be reshaped (text, subtitles, shapes) work the other way.
                    readonly property bool lockByDefault: handle.box.kind === "video"
                                                          || handle.box.kind === "image"
                                                          || handle.box.kind === "composite"

                    width: hs
                    height: hs
                    radius: Theme.radiusXs
                    color: gripArea.containsMouse || gripArea.pressed
                           ? Theme.primaryForeground : Theme.primary
                    border.width: Theme.borderWidth
                    border.color: gripArea.containsMouse || gripArea.pressed
                                  ? Theme.primary : Theme.primaryForeground

                    Behavior on color {
                        ColorAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
                    }

                    x: (modelData.dx === 0 ? handle.width / 2
                                           : (modelData.dx < 0 ? 0 : handle.width)) - hs / 2
                    y: (modelData.dy === 0 ? handle.height / 2
                                           : (modelData.dy < 0 ? 0 : handle.height)) - hs / 2

                    // Press point in overlay coordinates. The grip rides the box
                    // as it resizes, so deltas are measured against the overlay,
                    // which stands still.
                    property real startPx: 0
                    property real startPy: 0
                    // Tilted or under the camera: the grab mapped into the box's own layout px.
                    property var toLocal: null
                    property point startLocal: Qt.point(0, 0)
                    property var startBox: null

                    // Resize about the fixed anchor. The maths runs in the box's
                    // own axes, so a rotated clip grows along the direction the
                    // grip points; the resulting centre shift is rotated back to
                    // canvas axes at the end. At rotation 0 it reduces to a plain
                    // "opposite corner stays put".
                    function resizeTo(px, py, modifiers) {
                        const dxSign = grip.modelData.dx
                        const dySign = grip.modelData.dy
                        const a = handle.layoutRotation * Math.PI / 180
                        let lx = 0
                        let ly = 0
                        if (grip.toLocal) {
                            // Through the pose, so the pointer is read in the box's own plane. A
                            // tilted box resizes about its centre, which is also the tilt pivot:
                            // holding an edge still would drag it through depth. Doubling the
                            // pointer's travel in the box plane keeps the grip under the pointer.
                            const l = grip.toLocal(px, py)
                            const k = handle.is3d ? 2 : 1
                            lx = k * (l.x - grip.startLocal.x) / handle.sx
                            ly = k * (l.y - grip.startLocal.y) / handle.sy
                        } else {
                            const ddx = (px - grip.startPx) / handle.sx
                            const ddy = (py - grip.startPy) / handle.sy
                            // Canvas axes -> box axes: the inverse of the rotation the
                            // body drag applies to its translation.
                            lx = ddx * Math.cos(a) + ddy * Math.sin(a)
                            ly = -ddx * Math.sin(a) + ddy * Math.cos(a)
                        }

                        const shift = (modifiers & Qt.ShiftModifier) !== 0
                        const locked = grip.isCorner && (grip.lockByDefault !== shift)

                        let w = Math.max(1, handle.dragStartW + lx * dxSign)
                        let h = Math.max(1, handle.dragStartH + ly * dySign)
                        if (locked) {
                            // Take the scale from whichever axis the pointer moved
                            // further along, measured as the bigger departure from
                            // the original size so it reads the same growing or
                            // shrinking.
                            const rw = w / handle.dragStartW
                            const rh = h / handle.dragStartH
                            const s = Math.abs(rw - 1) >= Math.abs(rh - 1) ? rw : rh
                            w = Math.max(1, handle.dragStartW * s)
                            h = Math.max(1, handle.dragStartH * s)
                        }

                        // Only the moving edge sticks; the anchor is already fixed.
                        let guideX = -1
                        let guideY = -1
                        if (handle.canSnap && !(modifiers & Qt.ControlModifier)) {
                            // Each moving edge is snapped as it projects, through the pose,
                            // parents and camera.
                            const snapped = EditorState.previewSnapResize(grip.startBox, w, h, dxSign, dySign,
                                                                          handle.is3d,
                                                                          root.snapTolPx / handle.sx)
                            if (locked) {
                                // The axes are tied, so only the closer of the two
                                // snaps wins and it sets the scale for both.
                                const rx = snapped.distX >= 0 ? snapped.distX : Infinity
                                const ry = snapped.distY >= 0 ? snapped.distY : Infinity
                                let s = -1
                                if (rx <= ry && rx < Infinity) {
                                    s = snapped.width / handle.dragStartW
                                    guideX = snapped.guideX
                                } else if (ry < Infinity) {
                                    s = snapped.height / handle.dragStartH
                                    guideY = snapped.guideY
                                }
                                if (s > 0) {
                                    w = Math.max(1, handle.dragStartW * s)
                                    h = Math.max(1, handle.dragStartH * s)
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
                        handle.publishGuides(guideX, guideY)

                        // Keeping the anchor still means the centre moves by half
                        // the size change, toward the grip, in box axes.
                        const shiftX = handle.is3d ? 0 : (w - handle.dragStartW) / 2 * dxSign
                        const shiftY = handle.is3d ? 0 : (h - handle.dragStartH) / 2 * dySign
                        const cx = handle.dragStartX + handle.dragStartW / 2
                                   + shiftX * Math.cos(a) - shiftY * Math.sin(a)
                        const cy = handle.dragStartY + handle.dragStartH / 2
                                   + shiftX * Math.sin(a) + shiftY * Math.cos(a)
                        const x = cx - w / 2
                        const y = cy - h / 2

                        handle.liveX = x
                        handle.liveY = y
                        handle.liveW = w
                        handle.liveH = h
                        if (handle.isText && grip.isCorner) {
                            // Height drives the glyph scale; an edge drag just
                            // re-wraps, since the box is the wrap width.
                            const size = Math.round(handle.dragStartPixelSize
                                                    * h / Math.max(1, handle.dragStartH))
                            EditorState.previewSetTextRect(
                                handle.box.track,
                                handle.box.clip,
                                x, y, w, h, size)
                        } else {
                            EditorState.previewSetClipRect(
                                handle.box.track,
                                handle.box.clip,
                                x, y, w, h)
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
                            const raw = mapToItem(root, mouse.x, mouse.y)
                            const throughPose = handle.is3d || handle.viaCamera
                            const p = throughPose ? raw : handle.toParentLocal(raw)
                            grip.startPx = p.x
                            grip.startPy = p.y
                            grip.toLocal = throughPose ? handle.overlayToLocalMapper() : null
                            if (grip.toLocal)
                                grip.startLocal = grip.toLocal(p.x, p.y)
                            if (handle.isTransform)
                                root.movingLayerTrack = handle.box.track
                            handle.dragStartX = handle.layoutX
                            handle.dragStartY = handle.layoutY
                            handle.dragStartW = handle.layoutW
                            handle.dragStartH = handle.layoutH
                            handle.dragStartPixelSize = handle.box.pixelSize || 64
                            grip.startBox = handle.grabBox()
                            handle.liveX = handle.dragStartX
                            handle.liveY = handle.dragStartY
                            handle.liveW = handle.dragStartW
                            handle.liveH = handle.dragStartH
                            handle.resizing = true
                            root.interacting = true
                            EditorState.selectClip(handle.box.track, handle.box.clip)
                            handle.forceActiveFocus()
                            EditorState.beginPreviewDrag()
                        }

                        onPositionChanged: (mouse) => {
                            if (!pressed)
                                return
                            const raw = mapToItem(root, mouse.x, mouse.y)
                            const p = grip.toLocal ? raw : handle.toParentLocal(raw)
                            grip.resizeTo(p.x, p.y, mouse.modifiers)
                        }

                        onReleased: grip.finishResize()
                        onCanceled: grip.finishResize()
                    }

                    function finishResize() {
                        if (!handle.resizing)
                            return
                        handle.resizing = false
                        handle.liveX = -1e12
                        handle.liveY = -1e12
                        handle.liveW = -1
                        handle.liveH = -1
                        root.endInteraction()
                    }
                }
            }

            // Live size while resizing, the same readout the crop tool shows;
            // the live angle while rotating.
            Rectangle {
                visible: handle.resizing || handle.rotating
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.top: parent.bottom
                anchors.topMargin: Theme.spacingLg
                width: sizeLabel.width + Theme.spacingLg
                height: sizeLabel.height + Theme.spacingSm
                radius: Theme.radiusSm
                // Sits on the preview, not a panel surface, so it uses the fixed
                // scrim and on-media foreground rather than panel tokens.
                color: Theme.scrimStrong
                border.width: Theme.borderWidth
                border.color: Theme.guideWeak
                // Keeps the number upright on a rotated clip.
                rotation: -handle.rotation

                Text {
                    id: sizeLabel
                    anchors.centerIn: parent
                    text: handle.rotating
                          ? Math.round(handle.layoutRotation) + "°"
                          : Math.round(handle.layoutW) + "×" + Math.round(handle.layoutH)
                    color: Theme.onMedia
                    font.family: Theme.monoFontFamily
                    font.pixelSize: Theme.fontSizeXs
                }
            }

            // Rotation handle above the box (a 3D layer turns with the gizmo's rings instead)
            Item {
                visible: handle.selected && !handle.editing && !root.mode3d
                width: 14
                height: 14
                x: handle.width / 2 - width / 2
                y: -28

                Rectangle {
                    anchors.top: parent.bottom
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: 1
                    height: 16
                    color: Theme.primary
                }

                Rectangle {
                    anchors.fill: parent
                    radius: width / 2
                    color: Theme.primary
                    border.width: 1
                    border.color: Theme.onMedia
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
                            const raw = root.mapFromItem(null, rotateDrag.centroid.scenePosition.x,
                                                         rotateDrag.centroid.scenePosition.y)
                            const l = rotateDrag.toLocal(raw.x, raw.y)
                            return Math.atan2(l.y - handle.layoutH * handle.sy / 2,
                                              l.x - handle.layoutW * handle.sx / 2) * 180 / Math.PI + 90
                        }
                        // Measured in the box's own frame: a parent's turn is not the clip's.
                        const p = handle.toParentLocal(root.mapFromItem(null, rotateDrag.centroid.scenePosition.x,
                                                                        rotateDrag.centroid.scenePosition.y))
                        // A tilted box spins about its projected centre.
                        const c = handle.is3d
                                ? handle.toParentLocal(handle.mapToItem(root, handle.layoutW * handle.sx / 2,
                                                                        handle.layoutH * handle.sy / 2))
                                : Qt.point(handle.centerX, handle.centerY)
                        const ang = Math.atan2(p.y - c.y, p.x - c.x)
                        return ang * 180 / Math.PI + 90
                    }

                    onActiveChanged: {
                        if (active) {
                            root.interacting = true
                            if (handle.isTransform)
                                root.movingLayerTrack = handle.box.track
                            handle.rotating = true
                            handle.liveRotation = handle.box.rotation
                            rotateDrag.toLocal = handle.viaCamera || handle.is3d
                                    ? handle.overlayToLocalMapper() : null
                            // Turning starts from where the grip was taken, so
                            // grabbing it does not snap the clip to the pointer.
                            handle.rotateGrabOffset = handle.box.rotation
                                    - rotateDrag.pointerAngle()
                            EditorState.selectClip(handle.box.track, handle.box.clip)
                            handle.forceActiveFocus()
                            EditorState.beginPreviewDrag()
                        } else {
                            handle.rotating = false
                            handle.liveRotation = 1e9
                            rotateDrag.toLocal = null
                            root.endInteraction()
                        }
                    }
                    onCentroidChanged: {
                        if (!active)
                            return
                        let deg = root.normalizeDeg(rotateDrag.pointerAngle()
                                                    + handle.rotateGrabOffset)
                        if (!(rotateDrag.centroid.modifiers & Qt.ControlModifier))
                            deg = root.snapAngle(deg)
                        handle.liveRotation = deg
                        EditorState.previewSetClipRotation(
                            handle.box.track,
                            handle.box.clip,
                            deg)
                    }
                }
            }
        }
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

        Shape {
            anchors.fill: parent
            visible: parentFrame.quad.length === 4
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                strokeColor: Theme.clipTransform
                strokeWidth: Theme.borderWidthFocus
                strokeStyle: ShapePath.DashLine
                dashPattern: [4, 3]
                fillColor: "transparent"
                startX: parentFrame.corner(0).x
                startY: parentFrame.corner(0).y
                PathLine { x: parentFrame.corner(1).x; y: parentFrame.corner(1).y }
                PathLine { x: parentFrame.corner(2).x; y: parentFrame.corner(2).y }
                PathLine { x: parentFrame.corner(3).x; y: parentFrame.corner(3).y }
                PathLine { x: parentFrame.corner(0).x; y: parentFrame.corner(0).y }
            }
        }

        Rectangle {
            visible: parentFrame.quad.length === 4
            x: parentFrame.corner(0).x
            y: parentFrame.corner(0).y - height - Theme.spacingSm
            width: parentChipLabel.implicitWidth + Theme.spacingLg
            height: parentChipLabel.implicitHeight + Theme.spacingSm
            radius: height / 2
            color: parentChipMouse.containsMouse ? Qt.lighter(Theme.clipTransform, 1.15) : Theme.clipTransform

            Text {
                id: parentChipLabel
                anchors.centerIn: parent
                text: qsTr("Select %1").arg(root.parentFrameBox ? (root.parentFrameBox.name || qsTr("Transform")) : "")
                color: Theme.onMedia
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
            }

            MouseArea {
                id: parentChipMouse
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: EditorState.selectTransformParent()
            }

            ThemedToolTip {
                visible: parentChipMouse.containsMouse
                text: qsTr("Select the transform layer moving this clip (%1)")
                          .arg(EditorState.shortcutFor("selectTransformLayer"))
            }
        }
    }

    TransformGizmo {
        id: gizmo
        anchors.fill: parent
        z: 950
        sx: root.width / Math.max(1, root.gizmoBox ? root.gizmoBox.canvasWidth : 1)
        // Hidden while the box itself is dragged, which it would not follow.
        box: root.interacting && dragging === "" ? null : root.gizmoBox
        onDragStarted: {
            root.interacting = true
            EditorState.selectClip(box.track, box.clip)
        }
        onDragFinished: root.endInteraction()
    }

    // Alignment guides for whichever snap is currently engaged. Drawn once at
    // root level so a guide spans the whole canvas, not just the clip box.
    Rectangle {
        visible: root.snapGuideX >= 0
        x: root.snapGuideX
        y: 0
        width: 1
        height: root.height
        color: Theme.primary
        z: 400
    }
    Rectangle {
        visible: root.snapGuideY >= 0
        x: 0
        y: root.snapGuideY
        width: root.width
        height: 1
        color: Theme.primary
        z: 400
    }

    // Click-away catcher: while a text clip is edited in place,
    // a press outside the (raised) editor box drops focus, which
    // commits the edit via the editor's onActiveFocusChanged.
    MouseArea {
        anchors.fill: parent
        z: 500
        visible: root.editingKey !== ""
        enabled: visible
        onPressed: (mouse) => {
            root.forceActiveFocus()
            mouse.accepted = true
        }
    }
}
