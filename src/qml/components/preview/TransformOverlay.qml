import QtQuick
import QtQuick.Controls.Basic
import Drift
import ".."

// Transform overlay: resize/rotate grips and the in-place text editor for the
// clips visible at the playhead. Sits outside the (clipped) canvas rect,
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

    // "track:clip" of the text clip currently edited in place,
    // or "" when no inline edit is active.
    property string editingKey: ""

    // "track:clip" of a freshly added placeholder text clip that
    // should open its editor as soon as its box exists.
    property string pendingEditKey: ""

    // True when two overlay models describe the same boxes. Text/name
    // updates still reach delegates via liveStyle/liveText, so rebuilding
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
                    || x.canvasWidth !== y.canvasWidth
                    || x.canvasHeight !== y.canvasHeight)
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
        const next = EditorState.previewClipsAtPlayhead()
        if (clipsOverlayEqual(overlayClips, next))
            return
        overlayClips = next
        // Set model imperatively. Binding `model: overlayClips` re-enters when
        // tracksChanged fires during delegate setup (binding loop on model).
        clipRepeater.model = next
    }

    function endInteraction() {
        EditorState.commitPreviewDrag()
        interacting = false
        snapGuideX = -1
        snapGuideY = -1
        Qt.callLater(refreshOverlay)
    }

    // Stickiness: while a clip is moved or resized its edges and centre pull to
    // the canvas edges and centre lines. The tolerance is a screen distance, so
    // the pull feels the same whatever the project resolution or preview zoom.
    readonly property real snapTolPx: 8

    // Engaged guide lines, in overlay px (the overlay mirrors the canvas rect),
    // or -1 when nothing is snapped.
    property real snapGuideX: -1
    property real snapGuideY: -1

    // Nearest target within `tol` of any candidate, returned as the delta to add
    // to the moving value. `guide` is the target that won, or -1 for no snap.
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
        function onSelectionChanged() { root.refreshOverlay() }
        function onPlayheadSecondsChanged() { root.refreshOverlay() }
        function onPlayingChanged() {
            if (!EditorState.playing)
                root.refreshOverlay()
        }
        function onInlineTextEditRequested(trackIndex, clipIndex) {
            root.pendingEditKey = trackIndex + ":" + clipIndex
            root.refreshOverlay()
        }
    }

    Repeater {
        id: clipRepeater

        delegate: Item {
            id: handle
            required property var modelData

            readonly property bool selected: EditorState.selectedTrack === modelData.track
                                                    && EditorState.selectedClip === modelData.clip
            readonly property bool isText: modelData.kind === "text"
            readonly property bool editing: root.editingKey
                                                    === (modelData.track + ":" + modelData.clip)
            // True when this clip was just added with no text and should
            // open with an empty editor instead of the placeholder string.
            property bool openAsPlaceholder: false
            // Live clip style — tracks EditorState so property-panel edits
            // apply to the inline editor in real time.
            readonly property var liveStyle: {
                void EditorState.tracks
                const info = EditorState.clipAt(modelData.track, modelData.clip)
                return info && info.textStyle ? info.textStyle : null
            }
            readonly property var liveClip: {
                void EditorState.tracks
                return EditorState.clipAt(modelData.track, modelData.clip)
            }
            readonly property string liveText: liveClip ? (liveClip.textContent || "") : ""

            function enterEdit() {
                root.editingKey = modelData.track + ":" + modelData.clip
                root.interacting = true
                EditorState.selectClip(modelData.track, modelData.clip)
                EditorState.beginTextEdit(modelData.track, modelData.clip)
                const info = EditorState.clipAt(modelData.track, modelData.clip)
                if (handle.openAsPlaceholder) {
                    editor.text = ""
                    EditorState.previewSetClipTextContent(modelData.track, modelData.clip, "")
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
                EditorState.commitTextEdit(modelData.track, modelData.clip, editor.text)
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
                        !== (modelData.track + ":" + modelData.clip))
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

            readonly property real canvasW: Math.max(1, modelData.canvasWidth)
            readonly property real canvasH: Math.max(1, modelData.canvasHeight)
            readonly property real sx: parent.width / canvasW
            readonly property real sy: parent.height / canvasH

            // Live overrides applied during a drag so the box tracks
            // the cursor without rebuilding the (stale) model.
            property real liveX: -1e12
            property real liveY: -1e12
            property real liveW: -1
            property real liveH: -1
            property real liveRotation: 1e9

            readonly property real layoutX: liveX > -1e11 ? liveX : modelData.x
            readonly property real layoutY: liveY > -1e11 ? liveY : modelData.y
            readonly property real layoutW: liveW >= 0 ? liveW : modelData.width
            readonly property real layoutH: liveH >= 0 ? liveH : modelData.height
            readonly property real centerX: (layoutX + layoutW * 0.5) * sx
            readonly property real centerY: (layoutY + layoutH * 0.5) * sy

            x: layoutX * sx
            y: layoutY * sy
            width: Math.max(24, layoutW * sx)
            height: Math.max(24, layoutH * sy)
            // Front-most track (lowest index) sits on top so it
            // wins click hit-testing over boxes behind it. The clip
            // being edited jumps above everything so its editor and
            // the click-away catcher order correctly.
            z: handle.editing ? 1000 : -modelData.track
            transformOrigin: Item.Center
            rotation: liveRotation < 1e8 ? liveRotation : modelData.rotation

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

            // Canvas edges and centre lines, in layout px.
            readonly property var snapTargetsX: [0, handle.canvasW / 2, handle.canvasW]
            readonly property var snapTargetsY: [0, handle.canvasH / 2, handle.canvasH]
            readonly property real snapTolX: root.snapTolPx / handle.sx
            readonly property real snapTolY: root.snapTolPx / handle.sy
            // A rotated box has no axis-aligned edges to stick with, so it does
            // not snap — pulling its bounding box would move it sideways.
            readonly property bool canSnap: Math.abs(handle.rotation) < 0.01

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
                if (!handle.selected || handle.editing)
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
                    handle.modelData.track,
                    handle.modelData.clip,
                    handle.modelData.x + dx,
                    handle.modelData.y + dy)
                EditorState.commitPreviewDrag()
                event.accepted = true
            }

            Connections {
                target: EditorState
                function onSelectedClipDataChanged() {
                    if (!handle.editing || editor.activeFocus)
                        return
                    const info = EditorState.clipAt(modelData.track, modelData.clip)
                    if (!info)
                        return
                    const next = info.textContent || ""
                    if (editor.text !== next)
                        editor.text = next
                }
            }

            Rectangle {
                anchors.fill: parent
                visible: handle.isText && (handle.selected || handle.editing)
                         && handle.liveStyle && handle.liveStyle.boxEnabled
                color: handle.liveStyle ? handle.liveStyle.boxColor : "transparent"
                radius: handle.liveStyle ? handle.liveStyle.boxRadius * handle.sy : 0
            }

            // A plain Text item cannot show per-word accents, highlight pills or
            // underlines, so styles that use them fall back to the composited raster
            // rather than preview something the export will not match.
            readonly property bool plainStyle: !handle.liveStyle
                    || ((!handle.liveStyle.accent || handle.liveStyle.accent.rule === "none")
                        && !(handle.liveStyle.wordHighlight
                             && handle.liveStyle.wordHighlight.enabled)
                        && !handle.liveStyle.underlineEnabled)

            // Crisp vector text while the clip is selected. The composited
            // raster is downscaled for preview and looks soft when upscaled.
            Text {
                anchors.fill: parent
                visible: handle.isText && handle.selected && !handle.editing
                         && handle.plainStyle
                text: handle.liveText
                renderType: Text.NativeRendering
                color: handle.liveStyle ? handle.liveStyle.color : "white"
                font.family: handle.liveStyle ? handle.liveStyle.fontFamily : Theme.fontFamily
                font.pixelSize: handle.liveStyle
                                ? Math.max(1, Math.round(handle.liveStyle.pixelSize * handle.sy))
                                : 16
                font.weight: handle.liveStyle ? handle.liveStyle.fontWeight : Font.Normal
                font.italic: handle.liveStyle ? handle.liveStyle.italic : false
                font.letterSpacing: handle.liveStyle ? handle.liveStyle.letterSpacing * handle.sy : 0
                wrapMode: (handle.liveStyle && handle.liveStyle.wordWrap === false)
                          ? Text.NoWrap : Text.WordWrap
                horizontalAlignment: !handle.liveStyle ? Text.AlignHCenter
                                     : handle.liveStyle.align === "left" ? Text.AlignLeft
                                     : handle.liveStyle.align === "right" ? Text.AlignRight
                                     : Text.AlignHCenter
                verticalAlignment: !handle.liveStyle ? Text.AlignVCenter
                                   : handle.liveStyle.valign === "top" ? Text.AlignTop
                                   : handle.liveStyle.valign === "bottom" ? Text.AlignBottom
                                   : Text.AlignVCenter
                leftPadding: handle.liveStyle && handle.liveStyle.boxEnabled
                             ? Math.max(0, handle.liveStyle.boxPadding * handle.sy) : 0
                rightPadding: leftPadding
                topPadding: leftPadding
                bottomPadding: leftPadding
            }

            Rectangle {
                anchors.fill: parent
                color: "transparent"
                border.width: (handle.selected || handle.editing)
                              ? Theme.borderWidthFocus : Theme.borderWidth
                border.color: handle.selected ? Theme.primary : Theme.guideStrong
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
                        handle.modelData.track,
                        handle.modelData.clip,
                        text)
                }
                Keys.onEscapePressed: handle.cancelEdit()
                onActiveFocusChanged: if (!activeFocus && handle.editing) handle.commitEdit()
            }

            TapHandler {
                enabled: !handle.editing
                onTapped: {
                    EditorState.selectClip(handle.modelData.track, handle.modelData.clip)
                    handle.forceActiveFocus()
                }
                onDoubleTapped: if (handle.isText) handle.enterEdit()
            }

            DragHandler {
                id: bodyDrag
                target: null
                // Off while a grip is held: a handler on the parent item can
                // otherwise take the grab from the grip once the drag threshold
                // is passed, turning a resize into a move.
                enabled: !handle.editing && !handle.resizing
                cursorShape: Qt.SizeAllCursor

                // Press point in overlay coordinates. The box rides the cursor as
                // it moves, so deltas are measured against the overlay, which
                // stands still and keeps canvas axes whatever the box rotation is.
                property real pressPx: 0
                property real pressPy: 0

                onActiveChanged: {
                    if (active) {
                        root.interacting = true
                        // Taken at activation rather than at the press, so the drag
                        // threshold is not replayed as a jump.
                        const p = root.mapFromItem(null, bodyDrag.centroid.scenePosition.x,
                                                         bodyDrag.centroid.scenePosition.y)
                        bodyDrag.pressPx = p.x
                        bodyDrag.pressPy = p.y
                        handle.dragStartX = handle.modelData.x
                        handle.dragStartY = handle.modelData.y
                        handle.liveX = handle.dragStartX
                        handle.liveY = handle.dragStartY
                        EditorState.selectClip(handle.modelData.track, handle.modelData.clip)
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
                    let xPx = handle.dragStartX + (p.x - bodyDrag.pressPx) / handle.sx
                    let yPx = handle.dragStartY + (p.y - bodyDrag.pressPy) / handle.sy
                    // Both edges and the centre stick, so a clip can be landed
                    // flush against a canvas edge or dead-centre by feel.
                    // Ctrl passes straight through (Alt is the window drag on
                    // most Linux desktops, so it is not usable here).
                    if (handle.canSnap && !(bodyDrag.centroid.modifiers & Qt.ControlModifier)) {
                        const w = handle.layoutW
                        const h = handle.layoutH
                        const snapX = root.snapAxis([xPx, xPx + w / 2, xPx + w],
                                                    handle.snapTargetsX, handle.snapTolX)
                        const snapY = root.snapAxis([yPx, yPx + h / 2, yPx + h],
                                                    handle.snapTargetsY, handle.snapTolY)
                        xPx += snapX.delta
                        yPx += snapY.delta
                        handle.publishGuides(snapX.guide, snapY.guide)
                    } else {
                        handle.publishGuides(-1, -1)
                    }
                    handle.liveX = xPx
                    handle.liveY = yPx
                    EditorState.previewSetClipPosition(
                        handle.modelData.track,
                        handle.modelData.clip,
                        xPx,
                        yPx)
                }
            }

            // Resize grips: 4 edges then 4 corners, the same frame the canvas
            // crop tool uses. `dx`/`dy` say which edges each grip moves
            // (-1 = left/top, +1 = right/bottom, 0 = stays put). The opposite
            // edge or corner is the anchor and does not move.
            Repeater {
                model: (handle.selected && !handle.editing)
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
                    readonly property bool lockByDefault: handle.modelData.kind === "video"
                                                          || handle.modelData.kind === "image"

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

                    // Resize about the fixed anchor. The maths runs in the box's
                    // own axes, so a rotated clip grows along the direction the
                    // grip points; the resulting centre shift is rotated back to
                    // canvas axes at the end. At rotation 0 it reduces to a plain
                    // "opposite corner stays put".
                    function resizeTo(px, py, modifiers) {
                        const dxSign = grip.modelData.dx
                        const dySign = grip.modelData.dy
                        const a = handle.rotation * Math.PI / 180
                        const ddx = (px - grip.startPx) / handle.sx
                        const ddy = (py - grip.startPy) / handle.sy
                        // Canvas axes -> box axes: the inverse of the rotation the
                        // body drag applies to its translation.
                        const lx = ddx * Math.cos(a) + ddy * Math.sin(a)
                        const ly = -ddx * Math.sin(a) + ddy * Math.cos(a)

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
                            const anchorX = dxSign < 0 ? handle.dragStartX + handle.dragStartW
                                                       : handle.dragStartX
                            const anchorY = dySign < 0 ? handle.dragStartY + handle.dragStartH
                                                       : handle.dragStartY
                            const snapX = dxSign === 0
                                    ? { delta: 0, guide: -1 }
                                    : root.snapAxis([anchorX + dxSign * w],
                                                    handle.snapTargetsX, handle.snapTolX)
                            const snapY = dySign === 0
                                    ? { delta: 0, guide: -1 }
                                    : root.snapAxis([anchorY + dySign * h],
                                                    handle.snapTargetsY, handle.snapTolY)
                            if (locked) {
                                // The axes are tied, so only the closer of the two
                                // snaps wins and it sets the scale for both.
                                const rx = snapX.guide >= 0 ? Math.abs(snapX.delta) : Infinity
                                const ry = snapY.guide >= 0 ? Math.abs(snapY.delta) : Infinity
                                let s = -1
                                if (rx <= ry && snapX.guide >= 0) {
                                    s = Math.abs(snapX.guide - anchorX) / handle.dragStartW
                                    guideX = snapX.guide
                                } else if (ry < Infinity) {
                                    s = Math.abs(snapY.guide - anchorY) / handle.dragStartH
                                    guideY = snapY.guide
                                }
                                if (s > 0) {
                                    w = Math.max(1, handle.dragStartW * s)
                                    h = Math.max(1, handle.dragStartH * s)
                                }
                            } else {
                                if (snapX.guide >= 0) {
                                    w = Math.max(1, w + snapX.delta * dxSign)
                                    guideX = snapX.guide
                                }
                                if (snapY.guide >= 0) {
                                    h = Math.max(1, h + snapY.delta * dySign)
                                    guideY = snapY.guide
                                }
                            }
                        }
                        handle.publishGuides(guideX, guideY)

                        // Keeping the anchor still means the centre moves by half
                        // the size change, toward the grip, in box axes.
                        const shiftX = (w - handle.dragStartW) / 2 * dxSign
                        const shiftY = (h - handle.dragStartH) / 2 * dySign
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
                                handle.modelData.track,
                                handle.modelData.clip,
                                x, y, w, h, size)
                        } else {
                            EditorState.previewSetClipRect(
                                handle.modelData.track,
                                handle.modelData.clip,
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
                            const p = mapToItem(root, mouse.x, mouse.y)
                            grip.startPx = p.x
                            grip.startPy = p.y
                            handle.dragStartX = handle.layoutX
                            handle.dragStartY = handle.layoutY
                            handle.dragStartW = handle.layoutW
                            handle.dragStartH = handle.layoutH
                            handle.dragStartPixelSize = handle.modelData.pixelSize || 64
                            handle.liveX = handle.dragStartX
                            handle.liveY = handle.dragStartY
                            handle.liveW = handle.dragStartW
                            handle.liveH = handle.dragStartH
                            handle.resizing = true
                            root.interacting = true
                            EditorState.selectClip(handle.modelData.track, handle.modelData.clip)
                            handle.forceActiveFocus()
                            EditorState.beginPreviewDrag()
                        }

                        onPositionChanged: (mouse) => {
                            if (!pressed)
                                return
                            const p = mapToItem(root, mouse.x, mouse.y)
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
                          ? Math.round(handle.rotation) + "°"
                          : Math.round(handle.layoutW) + "×" + Math.round(handle.layoutH)
                    color: Theme.onMedia
                    font.family: Theme.monoFontFamily
                    font.pixelSize: Theme.fontSizeXs
                }
            }

            // Rotation handle above the box
            Item {
                visible: handle.selected && !handle.editing
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

                    // Angle from the box centre to the pointer, in the same frame
                    // as the clip rotation (the grip sits above the box, hence +90).
                    function pointerAngle() {
                        const p = root.mapFromItem(null, rotateDrag.centroid.scenePosition.x,
                                                                 rotateDrag.centroid.scenePosition.y)
                        const ang = Math.atan2(p.y - handle.centerY, p.x - handle.centerX)
                        return ang * 180 / Math.PI + 90
                    }

                    onActiveChanged: {
                        if (active) {
                            root.interacting = true
                            handle.rotating = true
                            handle.liveRotation = handle.modelData.rotation
                            // Turning starts from where the grip was taken, so
                            // grabbing it does not snap the clip to the pointer.
                            handle.rotateGrabOffset = handle.modelData.rotation
                                    - rotateDrag.pointerAngle()
                            EditorState.selectClip(handle.modelData.track, handle.modelData.clip)
                            handle.forceActiveFocus()
                            EditorState.beginPreviewDrag()
                        } else {
                            handle.rotating = false
                            handle.liveRotation = 1e9
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
                            handle.modelData.track,
                            handle.modelData.clip,
                            deg)
                    }
                }
            }
        }
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
