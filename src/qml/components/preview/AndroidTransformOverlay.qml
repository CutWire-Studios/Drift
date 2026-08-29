import QtQuick
import QtQuick.Controls.Basic
import Drift
import ".."

// Touch transform overlay: move/scale/rotate the clips visible at the playhead,
// plus the in-place text editor. Same maths as the desktop TransformOverlay, but
// the grips carry a fingertip-sized hit area instead of a mouse-sized one, and
// the mouse-only affordances (hover, wheel pass-through, arrow-key nudging,
// Shift/Ctrl modifiers) are gone — a phone has none of them. Corner grips
// therefore keep footage aspect unconditionally and snapping is always on.
// Geometry (x/y/width/height/z/visible) is driven by AndroidPreview.
Item {
    id: root

    property var overlayClips: []
    // True while a handle is being dragged. Rebuilding the model mid-drag would
    // destroy the delegate that owns the active grab, so refreshes are
    // suppressed until the drag ends. (Also held true while a text clip is
    // edited in place, so the inline editor delegate is not destroyed mid-edit.)
    property bool interacting: false

    // "track:clip" of the text clip currently edited in place, or "".
    property string editingKey: ""

    // "track:clip" of a freshly added placeholder text clip that should open its
    // editor as soon as its box exists.
    property string pendingEditKey: ""

    // Visible dot; the touch target around it is gripTouch wide.
    readonly property real gripSize: 14
    readonly property real gripTouch: 44

    // True when two overlay models describe the same boxes. Text/name updates
    // still reach delegates via liveStyle/liveText, so rebuilding for every
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
                    || x.canvasWidth !== y.canvasWidth
                    || x.canvasHeight !== y.canvasHeight)
                return false
        }
        return true
    }

    function refreshOverlay() {
        if (interacting)
            return
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
    readonly property real snapTolPx: 10

    // Engaged guide lines, in overlay px, or -1 when nothing is snapped.
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
            // True when this clip was just added with no text and should open
            // with an empty editor instead of the placeholder string.
            property bool openAsPlaceholder: false
            // Live clip style — tracks EditorState so property-sheet edits apply
            // to the inline editor in real time.
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

            // A just-added placeholder clip opens its own editor. Checked both on
            // creation and when the key arrives, since either can happen first.
            function claimPendingEdit() {
                if (!handle.isText || handle.editing)
                    return
                if (root.pendingEditKey !== (modelData.track + ":" + modelData.clip))
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

            // Live overrides applied during a drag so the box tracks the finger
            // without rebuilding the (stale) model.
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
            // Front-most track (lowest index) sits on top so it wins tap
            // hit-testing over boxes behind it. The clip being edited jumps above
            // everything so its editor and the tap-away catcher order correctly.
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
            // underlines, so styles that use them fall back to the composited
            // raster rather than preview something the export will not match.
            readonly property bool plainStyle: !handle.liveStyle
                    || ((!handle.liveStyle.accent || handle.liveStyle.accent.rule === "none")
                        && !(handle.liveStyle.wordHighlight
                             && handle.liveStyle.wordHighlight.enabled)
                        && !handle.liveStyle.underlineEnabled)

            // Crisp vector text while the clip is selected. The composited raster
            // is downscaled for preview and looks soft when upscaled.
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

            // In-place text editor. Shown over the box while editing; the baked
            // raster is hidden by the compositor via beginTextEdit. Plain TextArea
            // (not a Themed* control): this is canvas content that must match the
            // rendered text, not app chrome.
            TextArea {
                id: editor
                anchors.fill: parent
                visible: handle.editing
                enabled: handle.editing
                background: null
                renderType: TextEdit.NativeRendering
                padding: handle.liveStyle && handle.liveStyle.boxEnabled
                         ? Math.max(0, handle.liveStyle.boxPadding * handle.sy) : 0
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
                // Reaching a text editor by double-tap alone is a coin flip on a
                // phone: the first tap can land on a neighbouring box. Long-press
                // is the unambiguous route.
                onLongPressed: if (handle.isText) handle.enterEdit()
            }

            DragHandler {
                id: bodyDrag
                target: null
                // Off while a grip is held: a handler on the parent item can
                // otherwise take the grab from the grip once the drag threshold
                // is passed, turning a resize into a move.
                enabled: !handle.editing && !handle.resizing
                onActiveChanged: {
                    if (active) {
                        root.interacting = true
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
                onTranslationChanged: {
                    // translation is in the rotated box frame; rotate it back to canvas axes
                    const a = handle.rotation * Math.PI / 180
                    const dx = translation.x * Math.cos(a) - translation.y * Math.sin(a)
                    const dy = translation.x * Math.sin(a) + translation.y * Math.cos(a)
                    let xPx = handle.dragStartX + dx / handle.sx
                    let yPx = handle.dragStartY + dy / handle.sy
                    // Both edges and the centre stick, so a clip can be landed
                    // flush against a canvas edge or dead-centre by feel.
                    if (handle.canSnap) {
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

            // Resize grips: 4 edges then 4 corners, the same frame the canvas crop
            // tool uses. `dx`/`dy` say which edges each grip moves (-1 = left/top,
            // +1 = right/bottom, 0 = stays put). The opposite edge or corner is the
            // anchor and does not move.
            Repeater {
                model: (handle.selected && !handle.editing)
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

                delegate: Item {
                    id: grip
                    required property var modelData

                    // The touch target is the whole delegate; the dot inside it
                    // stays small enough not to hide the box edge it sits on.
                    width: root.gripTouch
                    height: root.gripTouch

                    // Corners drive both axes, edges only one — so an edge drag is
                    // the deliberate way to stretch and corners keep the ratio.
                    readonly property bool isCorner: modelData.dx !== 0 && modelData.dy !== 0
                    // Footage has a real aspect to protect. Boxes that exist to be
                    // reshaped (text, subtitles, shapes) scale freely.
                    readonly property bool lockRatio: isCorner
                            && (handle.modelData.kind === "video"
                                || handle.modelData.kind === "image")

                    x: (modelData.dx === 0 ? handle.width / 2
                                           : (modelData.dx < 0 ? 0 : handle.width)) - width / 2
                    y: (modelData.dy === 0 ? handle.height / 2
                                           : (modelData.dy < 0 ? 0 : handle.height)) - height / 2

                    Rectangle {
                        anchors.centerIn: parent
                        width: root.gripSize
                        height: root.gripSize
                        radius: Theme.radiusXs
                        color: gripArea.pressed ? Theme.primaryForeground : Theme.primary
                        border.width: Theme.borderWidth
                        border.color: gripArea.pressed ? Theme.primary : Theme.primaryForeground

                        Behavior on color {
                            ColorAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
                        }
                    }

                    // Press point in overlay coordinates. The grip rides the box as
                    // it resizes, so deltas are measured against the overlay, which
                    // stands still.
                    property real startPx: 0
                    property real startPy: 0

                    // Resize about the fixed anchor. The maths runs in the box's own
                    // axes, so a rotated clip grows along the direction the grip
                    // points; the resulting centre shift is rotated back to canvas
                    // axes at the end. At rotation 0 it reduces to a plain "opposite
                    // corner stays put".
                    function resizeTo(px, py) {
                        const dxSign = grip.modelData.dx
                        const dySign = grip.modelData.dy
                        const a = handle.rotation * Math.PI / 180
                        const ddx = (px - grip.startPx) / handle.sx
                        const ddy = (py - grip.startPy) / handle.sy
                        // Canvas axes -> box axes: the inverse of the rotation the
                        // body drag applies to its translation.
                        const lx = ddx * Math.cos(a) + ddy * Math.sin(a)
                        const ly = -ddx * Math.sin(a) + ddy * Math.cos(a)

                        const locked = grip.lockRatio

                        let w = Math.max(1, handle.dragStartW + lx * dxSign)
                        let h = Math.max(1, handle.dragStartH + ly * dySign)
                        if (locked) {
                            // Take the scale from whichever axis the finger moved
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
                        if (handle.canSnap) {
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

                        // Keeping the anchor still means the centre moves by half the
                        // size change, toward the grip, in box axes.
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
                        // The delegate's body DragHandler would otherwise take the
                        // grab once the drag threshold is passed, turning a resize
                        // into a move.
                        preventStealing: true

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
                            grip.resizeTo(p.x, p.y)
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

            // Live size while resizing, the same readout the crop tool shows.
            Rectangle {
                visible: handle.resizing
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.top: parent.bottom
                anchors.topMargin: root.gripTouch / 2 + Theme.spacingSm
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
                    text: Math.round(handle.layoutW) + "×" + Math.round(handle.layoutH)
                    color: Theme.onMedia
                    font.family: Theme.monoFontFamily
                    font.pixelSize: Theme.fontSizeXs
                }
            }

            // Rotation handle above the box.
            Item {
                id: rotateGrip
                visible: handle.selected && !handle.editing
                width: root.gripTouch
                height: root.gripTouch
                x: handle.width / 2 - width / 2
                y: -height - Theme.spacingLg

                Rectangle {
                    anchors.top: parent.verticalCenter
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: 1
                    height: rotateGrip.height / 2 + Theme.spacingLg
                    color: Theme.primary
                }

                Rectangle {
                    anchors.centerIn: parent
                    width: root.gripSize + 6
                    height: width
                    radius: width / 2
                    color: rotateDrag.active ? Theme.primaryForeground : Theme.primary
                    border.width: 1
                    border.color: rotateDrag.active ? Theme.primary : Theme.onMedia
                }

                DragHandler {
                    id: rotateDrag
                    target: null
                    onActiveChanged: {
                        if (active) {
                            root.interacting = true
                            handle.liveRotation = handle.modelData.rotation
                            EditorState.selectClip(handle.modelData.track, handle.modelData.clip)
                            handle.forceActiveFocus()
                            EditorState.beginPreviewDrag()
                        } else {
                            handle.liveRotation = 1e9
                            root.endInteraction()
                        }
                    }
                    onCentroidChanged: {
                        if (!active)
                            return
                        const p = root.mapFromItem(null, rotateDrag.centroid.scenePosition.x,
                                                   rotateDrag.centroid.scenePosition.y)
                        const ang = Math.atan2(p.y - handle.centerY, p.x - handle.centerX)
                        const deg = ang * 180 / Math.PI + 90
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

    // Tap-away catcher: while a text clip is edited in place, a press outside the
    // (raised) editor box drops focus, which commits the edit via the editor's
    // onActiveFocusChanged.
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
