import QtQuick
import QtQuick.Controls.Basic
import Drift

// Keyframe strip aligned with the timeline: left gutter matches track labels,
// graph scroll + playhead share the timeline's contentX / pxPerSecond.
//
// Every animated property of the selected clip earns a chip here — the strip is a view of the
// clip, not of the inspector's selection. A chip toggles whether that curve is drawn: it is a
// display filter and nothing more, so a hidden curve keeps animating and keeps its chip. Switching
// an animation *off* is the inspector row's job, not this strip's.
Item {
    id: root

    property real pxPerSecond: 50
    property real contentX: 0
    // The canvases below cover the viewport plus a chunk either side, parked on a chunk boundary,
    // so scrolling (and playback's auto-scroll) only repaints them when a boundary is crossed
    // rather than on every frame.
    readonly property real paintChunk: 256
    readonly property real paintOriginX: Math.floor(contentX / paintChunk) * paintChunk - paintChunk
    property real contentWidth: 800
    property real labelsWidth: Theme.trackLabelsWidth
    // Whether the open properties tab edits keyframeable properties (PropertiesPanel `keyframes`).
    property bool keyframesTab: false

    readonly property bool hasClip: EditorState.selectedTrack >= 0 && EditorState.selectedClip >= 0
    readonly property var clip: {
        void EditorState.selectedClipData
        return EditorState.selectedClipData
    }

    // Effect parameters are addressed as "fx.<effectIndex>.<paramKey>", and audio effect ones as
    // "afx.<effectIndex>.<paramKey>"; the label, range and color all come from the effect's own
    // param spec on the selected clip.
    function effectParam(id) {
        if (!clip)
            return null
        const audio = id.substring(0, 4) === "afx."
        if (!audio && id.substring(0, 3) !== "fx.")
            return null
        const stack = audio ? clip.audioEffects : clip.effects
        const start = audio ? 4 : 3
        if (!stack)
            return null
        const dot = id.indexOf(".", start)
        if (dot < 0)
            return null
        const effectIndex = parseInt(id.substring(start, dot))
        const key = id.substring(dot + 1)
        if (isNaN(effectIndex) || effectIndex < 0 || effectIndex >= stack.length)
            return null
        const effect = stack[effectIndex]
        const params = effect.params || []
        for (let i = 0; i < params.length; ++i) {
            if (params[i].key === key)
                return { effect: effect, param: params[i] }
        }
        return null
    }

    // Spelled-out names for the one-to-three character property chips.
    function propertyLabel(id) {
        switch (id) {
        case "x": return qsTr("X position")
        case "y": return qsTr("Y position")
        case "width": return qsTr("Width")
        case "height": return qsTr("Height")
        case "rotation": return qsTr("Rotation")
        case "rotationX": return qsTr("Tilt X")
        case "rotationY": return qsTr("Tilt Y")
        case "z": return qsTr("Depth")
        case "perspective": return qsTr("Perspective")
        case "opacity": return qsTr("Opacity")
        case "volume": return qsTr("Volume")
        case "mask.x": return qsTr("Mask X")
        case "mask.y": return qsTr("Mask Y")
        case "mask.w": return qsTr("Mask width")
        case "mask.h": return qsTr("Mask height")
        case "mask.rotation": return qsTr("Mask rotation")
        case "mask.feather": return qsTr("Mask feather")
        }
        // Text and shape properties are named by the engine ("Shadow · Blur"): the layer stack
        // is per clip, so no static table could spell them.
        if (id.substring(0, 5) === "text." || id.substring(0, 6) === "shape.") {
            const label = EditorState.keyframePropertyLabel(
                            EditorState.selectedTrack, EditorState.selectedClip, id)
            if (label.length > 0)
                return label
        }
        const fx = effectParam(id)
        if (fx)
            return fx.effect.label + " · " + fx.param.label
        return id
    }
    function chipLabel(id) {
        switch (id) {
        case "x": return "X"
        case "y": return "Y"
        case "width": return "W"
        case "height": return "H"
        case "rotation": return "°"
        case "rotationX": return "X°"
        case "rotationY": return "Y°"
        case "z": return "Z"
        case "perspective": return "Per"
        case "opacity": return "Op"
        case "volume": return "Vol"
        case "mask.x": return "MX"
        case "mask.y": return "MY"
        case "mask.w": return "MW"
        case "mask.h": return "MH"
        case "mask.rotation": return "M°"
        case "mask.feather": return "MFe"
        }
        const fx = effectParam(id)
        if (fx)
            return fx.param.label.substring(0, 3)
        return id
    }
    // Volume and audio effects only exist on clips that carry audio; the rest are visual.
    function supportsProperty(id) {
        if (!clip)
            return false
        if (id === "volume")
            return clip.kind === "audio" || clip.kind === "video"
        if (id.substring(0, 3) === "fx.")
            return clip.kind !== "audio" && effectParam(id) !== null
        if (id.substring(0, 4) === "afx.")
            return effectParam(id) !== null
        // A mask lives on the clip's lane, so the strip offers it for the same clips the Masks
        // tab does. There is no per-id validation to do: the id set is fixed and closed.
        if (id.substring(0, 5) === "mask.")
            return clip.kind !== "audio"
        return clip.kind !== "audio"
    }

    // Each series is normalized against its own value range, so curves in wildly
    // different units (px vs. 0–1 opacity) stay comparable when overlaid.
    function valueRangeFor(id, pts, base) {
        const fx = effectParam(id)
        return EditorState.keyframes.curveRange(id, pts, base, fx !== null, fx ? fx.param.min : 0,
                                                fx ? fx.param.max : 1)
    }

    // A preview move emits tracksChanged, which would re-evaluate `series` and
    // make the Repeater destroy the delegate owning the active DragHandler —
    // killing the grab on the first mouse move. Freeze the model while dragging
    // and track the in-flight key separately.
    property bool draggingKey: false
    property var frozenSeries: []
    property string dragProp: ""
    property int dragIndex: -1
    property real dragSeconds: 0
    property real dragValue: 0

    // The in-flight tangent, for the same reason: the canvas paints from the frozen series,
    // so without this the curve would not reshape until the handle was released.
    property int dragTangentIndex: -1
    property real dragTangentInDx: 0
    property real dragTangentInDy: 0
    property real dragTangentOutDx: 0
    property real dragTangentOutDy: 0
    // A bent segment reshapes two keys at once, so the canvas takes both from here.
    property var dragKeys: []

    // Bezier handles are opt-in; without them a curve is shaped by dragging the curve itself.
    property bool showHandles: false

    // One entry per animated property of the clip — the chip row's model, hidden curves included.
    // [{ prop, label, color, points, enabled, shown, valueMin, valueMax }, ...]
    readonly property var allSeries: {
        void EditorState.selectedClipData
        void EditorState.tracksRevision
        void EditorState.keyframeGraphHiddenProperties
        if (!hasClip)
            return []
        const out = []
        const animated = EditorState.clipAnimatedProperties(
                           EditorState.selectedTrack, EditorState.selectedClip)
        const hidden = EditorState.keyframeGraphHiddenProperties
        for (let i = 0; i < animated.length; ++i) {
            const id = animated[i]
            if (!supportsProperty(id))
                continue
            const pts = EditorState.clipKeyframes(
                          EditorState.selectedTrack, EditorState.selectedClip, id) || []
            // What the curve reads as when the property has no keys at all — the level of the
            // flat line drawn across the clip in that case.
            const base = EditorState.propertyBaseValue(
                           EditorState.selectedTrack, EditorState.selectedClip, id, 0)
            const range = root.valueRangeFor(id, pts, base)
            out.push({
                prop: id,
                label: root.chipLabel(id),
                color: Theme.keyframeCurveColor(id),
                points: pts,
                baseValue: base,
                enabled: EditorState.clipPropertyKeyframesEnabled(
                             EditorState.selectedTrack, EditorState.selectedClip, id),
                shown: hidden.indexOf(id) < 0,
                valueMin: range.min,
                valueMax: range.max
            })
        }
        return out
    }

    // The curves actually drawn: everything the chips have left visible.
    readonly property var series: {
        if (draggingKey)
            return frozenSeries
        const out = []
        for (let i = 0; i < allSeries.length; ++i) {
            if (allSeries[i].shown)
                out.push(allSeries[i])
        }
        return out
    }

    // Flattened key dots so a single Repeater can own every series' handles.
    readonly property var keyHandles: {
        const out = []
        for (let s = 0; s < series.length; ++s) {
            const entry = series[s]
            for (let i = 0; i < entry.points.length; ++i) {
                out.push({
                    seriesIndex: s,
                    pointIndex: i,
                    prop: entry.prop,
                    color: entry.color,
                    seconds: entry.points[i].seconds,
                    value: entry.points[i].value
                })
            }
        }
        return out
    }
    readonly property int keyCount: keyHandles.length

    // Tangent grips for the focused series only: two per key (in / out), minus the ones that
    // would dangle off the ends of the curve where there is no neighbouring segment.
    readonly property var tangentHandles: {
        const out = []
        if (!showHandles || !curveEditing || focusedIndex < 0 || focusedIndex >= series.length)
            return out
        const entry = series[focusedIndex]
        const pts = entry.points
        for (let i = 0; i < pts.length; ++i) {
            // A step or a Penner function ignores its handles, so neither end of its segment has one.
            const leavesShaped = pts[i].hold || !!pts[i].fn
            if (i > 0 && !(pts[i - 1].hold || !!pts[i - 1].fn))
                out.push({ pointIndex: i, outgoing: false, color: entry.color, point: pts[i] })
            if (i < pts.length - 1 && !leavesShaped)
                out.push({ pointIndex: i, outgoing: true, color: entry.color, point: pts[i] })
        }
        return out
    }

    // A handle with no length still needs to be grabbable, so an unset tangent is drawn at a
    // default reach into its segment rather than sitting exactly on the key.
    readonly property real defaultHandleSeconds: 0.12
    function handleSeconds(point, outgoing) {
        const raw = outgoing ? (point.outDx || 0) : (point.inDx || 0)
        if (Math.abs(raw) > 1e-6)
            return raw
        return outgoing ? defaultHandleSeconds : -defaultHandleSeconds
    }
    function handleValue(point, outgoing) {
        return point.value + (outgoing ? (point.outDy || 0) : (point.inDy || 0))
    }

    readonly property real clipStart: clip.start || 0
    readonly property real clipDuration: Math.max(0.1, clip.duration || 1)

    // Beat detection results for whatever range was last analyzed. Analysis is explicit
    // (the gutter buttons) because it renders the timeline mix — never on selection change.
    // The grid and the transients are separate layers over that one result.
    readonly property var beats: EditorState.beatAnalysis
    readonly property bool analyzed: !!beats && beats.rangeDuration > 0
    readonly property bool hasGrid: EditorState.beatGridVisible && !!beats && !!beats.beats
                                    && beats.beats.length > 0
    readonly property bool hasOnsets: EditorState.onsetsVisible && !!beats && !!beats.onsets
                                      && beats.onsets.length > 0
    readonly property real bpm: (beats && beats.bpm) ? beats.bpm : 0

    // Turning a layer on analyzes if this range has not been analyzed yet; analyzeBeats
    // no-ops when the result is already current.
    function showBeatLayer(gridLayer) {
        if (gridLayer)
            EditorState.beatGridVisible = true
        else
            EditorState.onsetsVisible = true
        EditorState.analyzeBeats(root.clipStart, root.clipDuration)
    }

    function lowerBound(list, value, key) {
        let lo = 0
        let hi = list.length
        while (lo < hi) {
            const mid = (lo + hi) >> 1
            if ((key ? list[mid][key] : list[mid]) < value)
                lo = mid + 1
            else
                hi = mid
        }
        return lo
    }

    // Drag the bottom edge or wheel over the header to grow the lane.
    property real laneHeight: 88
    readonly property real minLaneHeight: 60
    readonly property real maxLaneHeight: 460
    readonly property real compactHeight: 88
    readonly property real expandedHeight: 180

    Behavior on laneHeight {
        enabled: !resizeGrip.dragging
        NumberAnimation { duration: 160; easing.type: Easing.OutCubic }
    }

    function toggleExpanded() {
        if (curveEditing)
            laneHeight = compactHeight
        else
            laneHeight = expandedHeight
    }

    height: visible ? laneHeight : 0
    // Open whenever the clip has an animation to show, even if every curve is currently folded
    // away — otherwise hiding the last one would take the chips with it.
    visible: keyframesTab && hasClip && clip && allSeries.length > 0

    // Curve editing focuses one series: it gets tangent handles and owns the value axis,
    // while the others stay drawn as dimmed context. Handles from two series normalized to
    // different ranges are not comparable on screen, so only one can be live at a time.
    property string focusedProp: ""
    readonly property int focusedIndex: {
        for (let i = 0; i < series.length; ++i) {
            if (series[i].prop === focusedProp)
                return i
        }
        return series.length > 0 ? 0 : -1
    }
    // Tangent handles need room; below this the lane is an overview and shows keys only.
    readonly property bool curveEditing: laneHeight >= 140
    function isFocused(index) { return index === focusedIndex }

    // Double-click on empty graph inserts a key on the focused curve. The value comes from the
    // curve itself rather than from the cursor's height, so dropping a node in never changes
    // the animation — same contract as the speed editor's addPointAt/speedAtPos.
    function addKeyAt(px) {
        if (focusedIndex < 0 || focusedIndex >= series.length)
            return
        const entry = series[focusedIndex]
        const seconds = Math.max(
            clipStart,
            Math.min(clipStart + clipDuration, EditorState.snapTime(secondsForX(px))))
        const value = EditorState.propertyValueAt(
                        EditorState.selectedTrack, EditorState.selectedClip, entry.prop,
                        seconds, entry.baseValue)
        EditorState.showKeyframeGraphProperty(entry.prop)
        EditorState.setClipKeyframe(EditorState.selectedTrack, EditorState.selectedClip,
                                    entry.prop, seconds, value)
        focusedProp = entry.prop
    }

    function openKeyMenu(dot, x, y) {
        keyMenu.keyProp = dot.modelData.prop
        keyMenu.keySeconds = dot.modelData.seconds
        keyMenu.keyDot = dot
        keyMenu.segment = EditorState.keyframeSegmentEasing(EditorState.selectedTrack,
                                                            EditorState.selectedClip,
                                                            keyMenu.keyProp, keyMenu.keySeconds)
        keyMenu.popup(dot, x, y)
    }

    // Absolute timeline X — same mapping as clips on the track.
    function xForSeconds(seconds) {
        return seconds * pxPerSecond
    }
    function secondsForX(x) {
        return x / pxPerSecond
    }
    function yForValue(v, entry) {
        const span = Math.max(1e-6, entry.valueMax - entry.valueMin)
        return graph.height - ((v - entry.valueMin) / span) * graph.height
    }
    function valueForY(y, entry) {
        const span = Math.max(1e-6, entry.valueMax - entry.valueMin)
        const t = (graph.height - y) / Math.max(1, graph.height)
        return entry.valueMin + t * span
    }

    // The handle-shaped segment of the focused curve under a graph point, or null.
    function segmentAt(x, y) {
        const hit = curvesItem.segmentAt(x - contentX, y, Theme.touchInput ? 14 : 8)
        return hit.a !== undefined ? hit : null
    }
    function beginBend() {
        frozenSeries = series
        dragProp = series[focusedIndex].prop
        dragKeys = []
        draggingKey = true
        EditorState.beginPreviewDrag(qsTr("Edit keyframe curve"))
    }
    // Shift keeps the segment's handles where they are in time.
    function updateBend(hit, dx, dy, shift) {
        const entry = series[focusedIndex]
        const dSeconds = shift ? 0 : dx / pxPerSecond
        const dValue = valueForY(dy, entry) - valueForY(0, entry)
        const keys = curvesItem.deformSegment(hit.a, hit.b, hit.t, dSeconds, dValue)
        for (const k of keys)
            EditorState.previewSetKeyframeTangents(EditorState.selectedTrack, EditorState.selectedClip,
                                                   dragProp, k.seconds, k.inDx, k.inDy, k.outDx, k.outDy,
                                                   k.corner)
        dragKeys = keys
    }
    function endBend(commit) {
        // Same ordering as a key drag: reset before unfreezing.
        dragProp = ""
        dragKeys = []
        if (commit)
            EditorState.commitPreviewDrag()
        else
            EditorState.cancelPreviewDrag()
        draggingKey = false
    }

    KeyframeGraphFrame { anchors.fill: parent }

    Row {
        anchors.fill: parent

        // Left gutter — same width as track labels so the graph lines up.
        Item {
            id: leftGutter
            width: root.labelsWidth
            height: parent.height

            // DAW-style lane zoom: wheel over this header grows/shrinks the keyframe lane
            MouseArea {
                anchors.fill: parent
                acceptedButtons: Qt.NoButton
                z: 30
                onWheel: (wheel) => {
                    // Modified wheel belongs to the timeline's zoom/pan.
                    if (wheel.modifiers & (Qt.ControlModifier | Qt.ShiftModifier)) {
                        wheel.accepted = false
                        return
                    }
                    const dy = wheel.angleDelta.y !== 0 ? wheel.angleDelta.y
                                                        : wheel.pixelDelta.y
                    if (dy === 0)
                        return
                    const step = 20
                    const next = root.laneHeight + (dy > 0 ? step : -step)
                    root.laneHeight = Math.max(root.minLaneHeight,
                                               Math.min(root.maxLaneHeight, next))
                }
            }

            PinchHandler {
                target: null
                acceptedDevices: PointerDevice.TouchScreen
                property real startHeight: 0
                onActiveChanged: if (active) startHeight = root.laneHeight
                onActiveScaleChanged: {
                    if (active)
                        root.laneHeight = Math.max(root.minLaneHeight,
                                                   Math.min(root.maxLaneHeight, startHeight * activeScale))
                }
            }

            TapHandler {
                onDoubleTapped: root.toggleExpanded()
            }

            Column {
                anchors.fill: parent
                anchors.margins: 6
                spacing: 4

                KeyframeGraphHeaderBar {
                    width: parent.width
                    host: root
                }

                // One chip per animated property of the clip, tinted to its curve. Clicking folds
                // that curve away or brings it back; the chip itself always stays put.
                Flow {
                    width: parent.width
                    spacing: 3
                    Repeater {
                        model: root.allSeries
                        delegate: ThemedChip {
                            required property var modelData
                            text: modelData.label
                            chipHeight: 18
                            horizontalPadding: 3
                            accentColor: modelData.color
                            // A property whose keyframes are switched off in the inspector still
                            // has a curve to look at; it just is not driving anything.
                            opacity: modelData.enabled ? 1.0 : 0.5
                            tooltip: {
                                const name = root.propertyLabel(modelData.prop)
                                const state = modelData.enabled ? name
                                                                : qsTr("%1 (keyframes off)").arg(name)
                                return modelData.shown ? qsTr("%1 — click to hide this curve").arg(state)
                                                       : qsTr("%1 — click to show this curve").arg(state)
                            }
                            selected: modelData.shown
                            onClicked: {
                                EditorState.toggleKeyframeGraphPropertyVisible(modelData.prop)
                                // Showing a curve also makes it the one the tangent handles belong
                                // to, so the chip you just clicked is the curve you can shape.
                                if (!modelData.shown)
                                    root.focusedProp = modelData.prop
                                else if (root.focusedProp === modelData.prop)
                                    root.focusedProp = ""
                            }
                        }
                    }
                }

                KeyframeGraphLaneStatus { width: parent.width
                    host: root }
            }

            KeyframeGraphGutterRule { }
        }

        // Timeline-aligned viewport (scrolls with the tracks below).
        Item {
            id: viewport
            width: parent.width - root.labelsWidth
            height: parent.height
            clip: true

            Item {
                id: content
                x: -root.contentX
                width: Math.max(viewport.width + root.contentX, root.contentWidth)
                height: parent.height

                Item {
                    id: graph
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    anchors.topMargin: 6
                    anchors.bottomMargin: 6

                    // Selected clip's time span — sits under the same region as the clip.
                    KeyframeGraphClipSpan { host: root }

                    KeyframeGraphValueAxis { host: root }

                    // Playhead — same absolute X as the timeline playhead.
                    KeyframeGraphPlayhead { host: root }

                    // Detected beat grid + onset ticks, under the curves and key dots.
                    KeyframeGraphBeatCanvas {
                        host: root
                        viewportWidth: viewport.width
                    }

                    KeyframeCurvesItem {
                        id: curvesItem
                        x: root.contentX
                        width: viewport.width
                        height: parent.height
                        series: root.series
                        focusedIndex: root.focusedIndex
                        curveEditing: root.curveEditing
                        pxPerSecond: root.pxPerSecond
                        originX: root.contentX
                        clipStart: root.clipStart
                        clipDuration: root.clipDuration
                        lineWidth: Theme.c.keyframeGraphCurveCanvas.lineWidth
                        lineWidthMuted: Theme.c.keyframeGraphCurveCanvas.lineWidthMuted
                        opacityContext: Theme.c.keyframeGraphCurveCanvas.opacityContext
                        opacityDisabled: Theme.c.keyframeGraphCurveCanvas.opacityDisabled
                        positionLinked: EditorState.positionLinked
                        dragProp: root.dragProp
                        dragIndex: root.dragIndex
                        dragSeconds: root.dragSeconds
                        dragValue: root.dragValue
                        dragTangentIndex: root.dragTangentIndex
                        dragTangentInDx: root.dragTangentInDx
                        dragTangentInDy: root.dragTangentInDy
                        dragTangentOutDx: root.dragTangentOutDx
                        dragTangentOutDy: root.dragTangentOutDy
                        dragKeys: root.dragKeys
                        showHandles: root.showHandles && root.curveEditing
                        handleStyle: Theme.plainMap(Theme.c.keyframeGraphTangentHandle)
                    }

                    // Below the tangent grips and key dots, so double-clicking an existing node
                    // grabs it instead of inserting another one on top of it. Dragging the focused
                    // curve bends it; on a touchscreen the lane pans instead.
                    MouseArea {
                        id: bendArea
                        anchors.fill: parent
                        z: 1
                        acceptedButtons: Qt.LeftButton
                        hoverEnabled: root.curveEditing
                        preventStealing: bending
                        cursorShape: bending ? Qt.SizeAllCursor
                                             : (hoverBend ? Qt.PointingHandCursor : Qt.ArrowCursor)
                        property var bend: null
                        property bool bending: false
                        property bool hoverBend: false
                        property point pressPos

                        onPressed: (mouse) => {
                            pressPos = Qt.point(mouse.x, mouse.y)
                            bend = root.curveEditing && mouse.source === Qt.MouseEventNotSynthesized
                                   ? root.segmentAt(mouse.x, mouse.y) : null
                        }
                        onPositionChanged: (mouse) => {
                            if (!pressed) {
                                hoverBend = root.curveEditing && root.segmentAt(mouse.x, mouse.y) !== null
                                return
                            }
                            if (!bend)
                                return
                            const dx = mouse.x - pressPos.x
                            const dy = mouse.y - pressPos.y
                            if (!bending) {
                                if (Math.abs(dx) <= 3 && Math.abs(dy) <= 3)
                                    return
                                bending = true
                                root.beginBend()
                            }
                            root.updateBend(bend, dx, dy, mouse.modifiers & Qt.ShiftModifier)
                        }
                        onReleased: finish(true)
                        onCanceled: finish(false)
                        onExited: hoverBend = false
                        onDoubleClicked: function (mouse) { root.addKeyAt(mouse.x) }

                        function finish(commit) {
                            bend = null
                            if (!bending)
                                return
                            bending = false
                            root.endBend(commit)
                        }
                    }

                    Repeater {
                        model: root.keyHandles
                        delegate: KeyframeGraphKeyDot {
                            host: root
                        }
                    }

                    // Tangent grips for the focused curve, under the key dots (z 2) so a handle
                    // lying on a key never hides it. The canvas draws them.
                    Repeater {
                        model: root.tangentHandles

                        delegate: KeyframeGraphTangentHandle {
                            host: root
                            z: 1.5
                        }
                    }
                }
            }
        }

    }

    // Bottom edge resize grip. A sibling of the Row rather than a child, so it anchors to the
    // lane instead of being laid out horizontally beside the graph, and sits above the lane's
    // content so a drag near the boundary resizes rather than grabbing what is underneath.
    KeyframeGraphResizeGrip {
        id: resizeGrip
        host: root
    }

    // Right-click on a key: how the segment leaving it moves. Checks the segment's current choice.
    ThemedContextMenu {
        id: keyMenu

        property string keyProp: ""
        property real keySeconds: 0
        property Item keyDot: null
        property var segment: ({})
        readonly property string segmentKind: segment.spec ? segment.spec.kind : ""

        function isPreset(id) {
            return segment.presetId === id
        }
        function setSegment(spec) {
            EditorState.setKeyframeSegmentEasing(EditorState.selectedTrack, EditorState.selectedClip,
                                                 keyProp, keySeconds, spec)
        }
        function openEasing() {
            if (ShellBridge.phoneShell) {
                ShellBridge.easingSheetRequested(EditorState.selectedTrack, EditorState.selectedClip,
                                                 keyProp, root.propertyLabel(keyProp), keySeconds)
                return
            }
            easingPopupLoader.active = true
            easingPopupLoader.item.anchorItem = keyDot
            easingPopupLoader.item.propLabel = root.propertyLabel(keyProp)
            easingPopupLoader.item.openFor(EditorState.selectedTrack, EditorState.selectedClip,
                                           keyProp, keySeconds)
        }
        function saveCurrentAsPreset() {
            const n = EditorState.easingPresets().filter(p => p.user).length + 1
            EditorState.saveEasingPreset(qsTr("My easing %1").arg(n), segment.spec)
        }

        ThemedMenuItem {
            text: qsTr("Straight")
            icon.name: keyMenu.isPreset("linear") ? Theme.icons.check : ""
            onTriggered: keyMenu.setSegment({ kind: "linear" })
        }
        ThemedMenuItem {
            text: qsTr("Jump")
            icon.name: keyMenu.isPreset("hold") ? Theme.icons.check : ""
            onTriggered: keyMenu.setSegment({ kind: "hold" })
        }
        ThemedMenuItem {
            text: qsTr("Easing…")
            onTriggered: keyMenu.openEasing()
        }
        ThemedMenuItem {
            text: qsTr("Save curve as preset…")
            enabled: keyMenu.segmentKind === "bezier" || keyMenu.segmentKind === "function"
            onTriggered: keyMenu.saveCurrentAsPreset()
        }
    }

    Loader {
        id: easingPopupLoader
        active: false
        sourceComponent: EasingPickerPopup { }
    }
}
