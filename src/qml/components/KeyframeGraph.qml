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
    property real contentWidth: 800
    property real labelsWidth: Theme.trackLabelsWidth
    property string propertiesTab: ""

    readonly property bool hasClip: EditorState.selectedTrack >= 0 && EditorState.selectedClip >= 0
    readonly property var clip: {
        void EditorState.selectedClipData
        return EditorState.selectedClipData
    }

    // Effect parameters are addressed as "fx.<effectIndex>.<paramKey>"; the label, range and
    // color all come from the effect's own param spec on the selected clip.
    function effectParam(id) {
        if (!clip || !clip.effects || id.substring(0, 3) !== "fx.")
            return null
        const dot = id.indexOf(".", 3)
        if (dot < 0)
            return null
        const effectIndex = parseInt(id.substring(3, dot))
        const key = id.substring(dot + 1)
        if (isNaN(effectIndex) || effectIndex < 0 || effectIndex >= clip.effects.length)
            return null
        const effect = clip.effects[effectIndex]
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
        case "opacity": return qsTr("Opacity")
        case "volume": return qsTr("Volume")
        }
        const fx = effectParam(id)
        if (fx)
            return fx.effect.label + " · " + fx.param.label
        const mask = maskParam(id)
        if (mask)
            return qsTr("Cutout %1 · %2").arg(mask.index + 1).arg(mask.label)
        return id
    }
    function chipLabel(id) {
        switch (id) {
        case "x": return "X"
        case "y": return "Y"
        case "width": return "W"
        case "height": return "H"
        case "rotation": return "°"
        case "opacity": return "Op"
        case "volume": return "Vol"
        }
        const fx = effectParam(id)
        if (fx)
            return fx.param.label.substring(0, 3)
        const mask = maskParam(id)
        if (mask)
            return "M" + (mask.index + 1) + mask.chip
        return id
    }

    // "mask.<index>.<param>" -> { index, param, label, chip, min, max }, or null.
    function maskParam(id) {
        if (id.substring(0, 5) !== "mask.")
            return null
        const dot = id.indexOf(".", 5)
        if (dot < 0)
            return null
        const index = parseInt(id.substring(5, dot))
        const param = id.substring(dot + 1)
        const defs = {
            "x":        { label: qsTr("Center X"), chip: "X",  min: 0,    max: 1 },
            "y":        { label: qsTr("Center Y"), chip: "Y",  min: 0,    max: 1 },
            "w":        { label: qsTr("Width"),    chip: "W",  min: 0.05, max: 1 },
            "h":        { label: qsTr("Height"),   chip: "H",  min: 0.05, max: 1 },
            "rotation": { label: qsTr("Rotation"), chip: "°",  min: -180, max: 180 },
            "feather":  { label: qsTr("Feather"),  chip: "Fe", min: 0,    max: 64 }
        }
        const def = defs[param]
        if (isNaN(index) || !def)
            return null
        return { index: index, param: param, label: def.label, chip: def.chip,
                 min: def.min, max: def.max }
    }
    // Volume only exists on clips that carry audio; the rest are visual.
    function supportsProperty(id) {
        if (!clip)
            return false
        if (id === "volume")
            return clip.kind === "audio" || clip.kind === "video"
        if (id.substring(0, 3) === "fx.")
            return clip.kind !== "audio" && effectParam(id) !== null
        if (id.substring(0, 5) === "mask.") {
            const mask = maskParam(id)
            return clip.kind !== "audio" && mask !== null
                   && !!clip.masks && mask.index < clip.masks.length
        }
        return clip.kind !== "audio"
    }

    // Each series is normalized against its own value range, so curves in wildly
    // different units (px vs. 0–1 opacity) stay comparable when overlaid.
    function valueRangeFor(id, pts, base) {
        if (id === "opacity")
            return { min: 0, max: 1 }
        if (id === "volume")
            return { min: 0, max: 2 }
        if (id === "rotation")
            return { min: -180, max: 180 }
        const fx = effectParam(id)
        if (fx)
            return { min: fx.param.min, max: fx.param.max }
        const mask = maskParam(id)
        if (mask)
            return { min: mask.min, max: mask.max }
        let lo = Infinity
        let hi = -Infinity
        for (let i = 0; i < pts.length; ++i) {
            const v = Number(pts[i].value)
            lo = Math.min(lo, v)
            hi = Math.max(hi, v)
        }
        // With no keys there is nothing to fit, so centre the axis on the value the property
        // actually holds — otherwise a 1920px width would be drawn far off the top of a 0..100 lane.
        if (!isFinite(lo))
            lo = base
        if (!isFinite(hi))
            hi = base
        return {
            min: lo - Math.max(1, Math.abs(lo) * 0.1),
            max: hi + Math.max(1, Math.abs(hi) * 0.1)
        }
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

    // One entry per animated property of the clip — the chip row's model, hidden curves included.
    // [{ prop, label, color, points, enabled, shown, valueMin, valueMax }, ...]
    readonly property var allSeries: {
        void EditorState.selectedClipData
        void EditorState.tracks
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
        if (!curveEditing || focusedIndex < 0 || focusedIndex >= series.length)
            return out
        const entry = series[focusedIndex]
        const pts = entry.points
        for (let i = 0; i < pts.length; ++i) {
            if (pts[i].hold)
                continue // a step has no tangents to shape
            if (i > 0)
                out.push({ pointIndex: i, outgoing: false, color: entry.color, point: pts[i] })
            if (i < pts.length - 1)
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

    // An onset this close to a grid line is already represented by it — drawing both
    // would just thicken the same line.
    function nearAnyBeat(seconds) {
        if (!beats || !beats.beats)
            return false
        for (let i = 0; i < beats.beats.length; ++i) {
            if (Math.abs(beats.beats[i] - seconds) < 0.06)
                return true
        }
        return false
    }

    // Drag the bottom edge to grow the lane. Session-only by design: it is a working
    // preference, not something worth persisting into settings or the project file.
    property real laneHeight: 88
    readonly property real minLaneHeight: 60
    readonly property real maxLaneHeight: 460

    height: visible ? laneHeight : 0
    // Open whenever the clip has an animation to show, even if every curve is currently folded
    // away — otherwise hiding the last one would take the chips with it.
    visible: (propertiesTab === "transform" || propertiesTab === "effects")
             && hasClip && clip && allSeries.length > 0

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

    Rectangle {
        anchors.fill: parent
        color: Theme.panelBackground
        border.width: 1
        border.color: Theme.panelBorder
    }

    Row {
        anchors.fill: parent

        // Left gutter — same width as track labels so the graph lines up.
        Item {
            width: root.labelsWidth
            height: parent.height

            Column {
                anchors.fill: parent
                anchors.margins: 6
                spacing: 4

                Row {
                    width: parent.width
                    spacing: 4

                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: qsTr("Keyframes")
                        color: Theme.mutedForeground
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSizeXs
                        font.weight: Font.Medium
                    }

                    // Beat grid / tempo.
                    IconButton {
                        anchors.verticalCenter: parent.verticalCenter
                        glyph: Theme.icons.music
                        variant: "text"
                        buttonSize: 20
                        iconSize: 14
                        active: EditorState.beatGridVisible
                        enabled: !EditorState.beatAnalysisRunning
                        tooltip: EditorState.beatAnalysisRunning
                                 ? qsTr("Analyzing…")
                                 : (EditorState.beatGridVisible ? qsTr("Hide the beat markers")
                                                                : qsTr("Find the beat and show markers"))
                        onClicked: {
                            if (EditorState.beatGridVisible)
                                EditorState.beatGridVisible = false
                            else
                                root.showBeatLayer(true)
                        }
                    }

                    // Individual transients.
                    IconButton {
                        anchors.verticalCenter: parent.verticalCenter
                        glyph: Theme.icons.audioLines
                        variant: "text"
                        buttonSize: 20
                        iconSize: 14
                        active: EditorState.onsetsVisible
                        enabled: !EditorState.beatAnalysisRunning
                        tooltip: EditorState.beatAnalysisRunning
                                 ? qsTr("Analyzing…")
                                 : (EditorState.onsetsVisible ? qsTr("Hide hits")
                                                              : qsTr("Find beats and hits in the audio under this clip"))
                        onClicked: {
                            if (EditorState.onsetsVisible)
                                EditorState.onsetsVisible = false
                            else
                                root.showBeatLayer(false)
                        }
                    }
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

                Text {
                    width: parent.width
                    wrapMode: Text.WordWrap
                    text: {
                        const keys = root.keyCount === 0 ? qsTr("No keyframes")
                                                         : qsTr("%n keyframes", "", root.keyCount)
                        if (!EditorState.beatGridVisible || !root.analyzed)
                            return keys
                        // The detector publishes no bpm when the tempo estimate was not
                        // confident — say so, rather than leaving the grid button lit over
                        // an empty lane. Onsets may still be perfectly usable.
                        return root.bpm > 0 ? keys + " · " + Math.round(root.bpm) + qsTr(" BPM")
                                            : keys + qsTr(" · no beat found")
                    }
                    color: Theme.mutedForeground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeXs
                }
            }

            Rectangle {
                anchors.right: parent.right
                width: 1
                height: parent.height
                color: Theme.panelBorder
            }
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
                    Rectangle {
                        x: root.xForSeconds(root.clipStart)
                        width: Math.max(2, root.clipDuration * root.pxPerSecond)
                        height: parent.height
                        color: Theme.panelAccent
                        opacity: 0.55
                        radius: 2
                    }

                    Rectangle {
                        anchors.verticalCenter: parent.verticalCenter
                        x: root.xForSeconds(root.clipStart)
                        width: Math.max(2, root.clipDuration * root.pxPerSecond)
                        height: 1
                        color: Theme.panelBorder
                    }

                    // Playhead — same absolute X as the timeline playhead.
                    Rectangle {
                        x: root.xForSeconds(EditorState.playheadSeconds) - Theme.playheadLineWidth / 2
                        width: Theme.playheadLineWidth
                        height: parent.height
                        color: Theme.primary
                        z: 3
                    }

                    // Detected beat grid + onset ticks, under the curves and key dots.
                    Canvas {
                        id: beatCanvas
                        // Viewport-sized, not content-sized — see curveCanvas.
                        x: root.contentX
                        width: viewport.width
                        anchors.top: parent.top
                        anchors.bottom: parent.bottom
                        z: 0
                        visible: root.hasGrid || root.hasOnsets

                        onPaint: {
                            const ctx = getContext("2d")
                            ctx.clearRect(0, 0, width, height)
                            const a = root.beats
                            if (!a)
                                return
                            // Canvas-local x for an absolute timeline position. Rounding happens
                            // after the shift, so a fractional scroll offset cannot push a 1px
                            // tick onto a half-pixel and smear it across two columns.
                            const localX = t => Math.round(root.xForSeconds(t) - root.contentX)

                            if (root.hasGrid) {
                                const perBar = a.beatsPerBar || 4
                                const first = a.firstDownbeat || 0
                                for (let i = 0; i < a.beats.length; ++i) {
                                    const isBar = ((i - first) % perBar + perBar) % perBar === 0
                                    const px = localX(a.beats[i])
                                    if (px < -2 || px > width + 2)
                                        continue
                                    ctx.fillStyle = String(isBar ? Theme.beatBarColor
                                                                 : Theme.beatGridColor)
                                    ctx.fillRect(px, 0, isBar ? 2 : 1, height)
                                }
                            }

                            if (!root.hasOnsets)
                                return

                            // With the grid up, only transients that miss it — the rest would
                            // just double-draw lines already there. Height tracks strength.
                            ctx.fillStyle = String(Theme.beatOnsetColor)
                            const onsets = a.onsets || []
                            for (let j = 0; j < onsets.length; ++j) {
                                if (root.hasGrid && root.nearAnyBeat(onsets[j].seconds))
                                    continue
                                const ox = localX(onsets[j].seconds)
                                if (ox < -2 || ox > width + 2)
                                    continue
                                // Centered rather than sitting on the floor: a 1px tick down
                                // at the bottom edge read as chart furniture next to the
                                // full-height beat lines.
                                const h = Math.max(6, onsets[j].strength * height * 0.5)
                                ctx.fillRect(ox - 1, (height - h) / 2, 2, h)
                            }
                        }

                        // The canvas is hidden until the first analysis lands, and a
                        // requestPaint issued while hidden is dropped.
                        onVisibleChanged: if (visible) requestPaint()

                        Connections {
                            target: root
                            function onBeatsChanged() { beatCanvas.requestPaint() }
                            // Toggling a layer leaves beatAnalysis itself untouched, so
                            // onBeatsChanged does not fire for it.
                            function onHasGridChanged() { beatCanvas.requestPaint() }
                            function onHasOnsetsChanged() { beatCanvas.requestPaint() }
                            function onPxPerSecondChanged() { beatCanvas.requestPaint() }
                            function onContentXChanged() { beatCanvas.requestPaint() }
                        }
                    }

                    Canvas {
                        id: curveCanvas
                        // Only as wide as the visible strip, parked at the viewport's left edge
                        // (content is offset by -contentX, so x: contentX cancels the scroll).
                        // Filling the scrollable content instead would size the backing texture
                        // to the whole timeline, and QSGPlainTexture silently rescales anything
                        // past GL_MAX_TEXTURE_SIZE to fit — 16384px, which at 40x zoom is barely
                        // 8 seconds of timeline. Past that the curves are drawn correctly and
                        // then downsampled onto the GPU, which is what smeared them.
                        x: root.contentX
                        width: viewport.width
                        anchors.top: parent.top
                        anchors.bottom: parent.bottom
                        onPaint: {
                            const ctx = getContext("2d")
                            ctx.clearRect(0, 0, width, height)
                            // Everything below is in absolute timeline coordinates.
                            ctx.save()
                            ctx.translate(-root.contentX, 0)
                            const spanX0 = root.xForSeconds(root.clipStart)
                            const spanX1 = root.xForSeconds(root.clipStart + root.clipDuration)

                            for (let s = 0; s < root.series.length; ++s) {
                                const entry = root.series[s]
                                const live = entry.points.map(function (p, i) {
                                    if (entry.prop !== root.dragProp)
                                        return p
                                    if (i === root.dragTangentIndex) {
                                        // Handle being dragged: same key, reshaped.
                                        return {
                                            seconds: p.seconds, value: p.value, hold: p.hold,
                                            inDx: root.dragTangentInDx, inDy: root.dragTangentInDy,
                                            outDx: root.dragTangentOutDx, outDy: root.dragTangentOutDy
                                        }
                                    }
                                    if (i !== root.dragIndex)
                                        return p
                                    // Key being moved: carry its tangents through, so the
                                    // curve shape does not snap back to linear mid-drag.
                                    return {
                                        seconds: root.dragSeconds, value: root.dragValue,
                                        inDx: p.inDx, inDy: p.inDy,
                                        outDx: p.outDx, outDy: p.outDy, hold: p.hold
                                    }
                                })
                                const sorted = live.sort((a, b) => a.seconds - b.seconds)

                                // The focused series is the one being edited; the rest are
                                // context and are drawn thinner and faded. A series whose
                                // keyframes are switched off is faded further still — its curve
                                // is a record of an animation that is not currently playing.
                                const focused = root.isFocused(s)
                                // Whole pixels. A 1.8px stroke lands ~40/100/40% across three
                                // rows, which reads as a glow wherever the curve runs shallow —
                                // exactly the flat tops and troughs between keys.
                                ctx.lineWidth = focused ? 2 : 1
                                ctx.globalAlpha = (root.curveEditing && !focused) ? 0.35 : 1.0
                                if (entry.enabled === false)
                                    ctx.globalAlpha *= 0.45
                                ctx.strokeStyle = String(entry.color)
                                ctx.beginPath()

                                // An unkeyed property is a flat line at the value it actually
                                // holds, so every selected property reads as an editable curve
                                // spanning the clip rather than as nothing at all.
                                if (sorted.length === 0) {
                                    const by = root.yForValue(entry.baseValue, entry)
                                    ctx.moveTo(spanX0, by)
                                    ctx.lineTo(spanX1, by)
                                    ctx.stroke()
                                    continue
                                }

                                // Outside the keyed range the value is held flat, which is what
                                // evaluateAt does — so the drawn line matches playback.
                                const leadY = root.yForValue(sorted[0].value, entry)
                                ctx.moveTo(spanX0, leadY)
                                ctx.lineTo(root.xForSeconds(sorted[0].seconds), leadY)
                                for (let i = 1; i < sorted.length; ++i) {
                                    const a = sorted[i - 1]
                                    const b = sorted[i]
                                    const ax = root.xForSeconds(a.seconds)
                                    const ay = root.yForValue(a.value, entry)
                                    const bx = root.xForSeconds(b.seconds)
                                    const by = root.yForValue(b.value, entry)

                                    if (a.hold) {
                                        // Step: hold the outgoing value, then jump.
                                        ctx.lineTo(bx, ay)
                                        ctx.lineTo(bx, by)
                                        continue
                                    }
                                    // Handles are clamped into the segment exactly as the
                                    // evaluator does, so the drawn curve matches playback.
                                    const c1x = Math.min(bx, Math.max(ax, ax + root.xForSeconds(a.outDx || 0)))
                                    const c2x = Math.min(bx, Math.max(ax, bx + root.xForSeconds(b.inDx || 0)))
                                    const c1y = root.yForValue(a.value + (a.outDy || 0), entry)
                                    const c2y = root.yForValue(b.value + (b.inDy || 0), entry)
                                    ctx.bezierCurveTo(c1x, c1y, c2x, c2y, bx, by)
                                }
                                const tail = sorted[sorted.length - 1]
                                ctx.lineTo(spanX1, root.yForValue(tail.value, entry))
                                ctx.stroke()
                            }
                            ctx.globalAlpha = 1.0
                            ctx.restore()
                        }
                        Connections {
                            target: root
                            function onSeriesChanged() { curveCanvas.requestPaint() }
                            function onPxPerSecondChanged() { curveCanvas.requestPaint() }
                            function onContentXChanged() { curveCanvas.requestPaint() }
                            function onDragSecondsChanged() { curveCanvas.requestPaint() }
                            function onDragValueChanged() { curveCanvas.requestPaint() }
                            function onDragTangentOutDyChanged() { curveCanvas.requestPaint() }
                            function onDragTangentOutDxChanged() { curveCanvas.requestPaint() }
                            function onDragTangentInDyChanged() { curveCanvas.requestPaint() }
                            function onDragTangentInDxChanged() { curveCanvas.requestPaint() }
                            function onFocusedPropChanged() { curveCanvas.requestPaint() }
                            function onLaneHeightChanged() { curveCanvas.requestPaint() }
                        }
                    }

                    // Below the key dots (z 2) and tangent grips (z 4), so double-clicking an
                    // existing node grabs it instead of inserting another one on top of it.
                    MouseArea {
                        anchors.fill: parent
                        z: 1
                        acceptedButtons: Qt.LeftButton
                        onDoubleClicked: function (mouse) { root.addKeyAt(mouse.x) }
                    }

                    Repeater {
                        model: root.keyHandles
                        delegate: Rectangle {
                            id: keyDot
                            required property var modelData
                            readonly property var entry: root.series[modelData.seriesIndex]
                            width: 10
                            height: 10
                            rotation: 45
                            radius: 1
                            color: modelData.color
                            border.width: 1
                            border.color: "#ffffff"
                            opacity: (root.curveEditing && !root.isFocused(modelData.seriesIndex))
                                     ? 0.4 : 1.0
                            x: root.xForSeconds(modelData.seconds) - width / 2 + dragDx
                            y: root.yForValue(modelData.value, entry) - height / 2 + dragDy
                            z: 2

                            // Offsets rather than direct x/y writes, which would
                            // clobber the bindings above for good.
                            property real dragDx: 0
                            property real dragDy: 0
                            property real editSeconds: modelData.seconds

                            // Touching a key moves focus to its series, which is what arms
                            // that curve's tangent handles.
                            TapHandler {
                                onTapped: root.focusedProp = keyDot.modelData.prop
                                onDoubleTapped: {
                                    EditorState.removeClipKeyframe(
                                        EditorState.selectedTrack,
                                        EditorState.selectedClip,
                                        keyDot.modelData.prop,
                                        keyDot.modelData.seconds
                                    )
                                }
                            }

                            DragHandler {
                                target: null
                                onActiveChanged: {
                                    if (active) {
                                        root.focusedProp = keyDot.modelData.prop
                                        keyDot.editSeconds = keyDot.modelData.seconds
                                        root.frozenSeries = root.series
                                        root.dragProp = keyDot.modelData.prop
                                        root.dragIndex = keyDot.modelData.pointIndex
                                        root.dragSeconds = keyDot.modelData.seconds
                                        root.dragValue = keyDot.modelData.value
                                        root.draggingKey = true
                                        EditorState.beginPreviewDrag(qsTr("Move keyframe"))
                                    } else {
                                        EditorState.commitPreviewDrag()
                                        root.draggingKey = false
                                        root.dragProp = ""
                                        root.dragIndex = -1
                                        keyDot.dragDx = 0
                                        keyDot.dragDy = 0
                                    }
                                }
                                onTranslationChanged: {
                                    if (!active)
                                        return
                                    const entry = keyDot.entry
                                    const baseSec = keyDot.modelData.seconds
                                    const baseVal = keyDot.modelData.value
                                    // snapTime pulls in beats, clip edges and the playhead,
                                    // and is a no-op when the toolbar's snap toggle is off.
                                    const rawSec = baseSec + translation.x / root.pxPerSecond
                                    const newSec = Math.max(
                                        root.clipStart,
                                        Math.min(root.clipStart + root.clipDuration,
                                                 EditorState.snapTime(rawSec)))
                                    const newVal = Math.max(
                                        entry.valueMin,
                                        Math.min(entry.valueMax,
                                                 root.valueForY(
                                                     root.yForValue(baseVal, entry) + translation.y,
                                                     entry)))
                                    EditorState.previewMoveClipKeyframe(
                                        EditorState.selectedTrack, EditorState.selectedClip,
                                        keyDot.modelData.prop, keyDot.editSeconds, newSec, newVal)
                                    keyDot.editSeconds = newSec
                                    keyDot.dragDx = root.xForSeconds(newSec) - root.xForSeconds(baseSec)
                                    keyDot.dragDy = root.yForValue(newVal, entry)
                                                  - root.yForValue(baseVal, entry)
                                    root.dragSeconds = newSec
                                    root.dragValue = newVal
                                }
                            }
                        }
                    }

                    // Tangent grips for the focused curve, above the keys so they stay
                    // grabbable where a handle folds back over its own key.
                    Repeater {
                        model: root.tangentHandles

                        delegate: Item {
                            id: tangent
                            required property var modelData

                            readonly property var entry: root.series[root.focusedIndex]
                            readonly property real keyX: root.xForSeconds(modelData.point.seconds)
                            readonly property real keyY: root.yForValue(modelData.point.value, entry)
                            readonly property real tipX:
                                root.xForSeconds(modelData.point.seconds
                                                 + root.handleSeconds(modelData.point, modelData.outgoing))
                            readonly property real tipY:
                                root.yForValue(root.handleValue(modelData.point, modelData.outgoing), entry)

                            anchors.fill: parent
                            z: 4

                            // Leader line from the key out to the grip.
                            Rectangle {
                                x: tangent.keyX
                                y: tangent.keyY
                                width: Math.hypot(tangent.tipX - tangent.keyX + tangent.dragDx,
                                                  tangent.tipY - tangent.keyY + tangent.dragDy)
                                height: 1
                                color: tangent.modelData.color
                                opacity: 0.55
                                transformOrigin: Item.TopLeft
                                rotation: Math.atan2(tangent.tipY - tangent.keyY + tangent.dragDy,
                                                     tangent.tipX - tangent.keyX + tangent.dragDx)
                                          * 180 / Math.PI
                            }

                            property real dragDx: 0
                            property real dragDy: 0

                            Rectangle {
                                id: grip
                                width: 8
                                height: 8
                                radius: 4
                                color: Theme.panelBackground
                                border.width: 1.5
                                border.color: tangent.modelData.color
                                x: tangent.tipX - width / 2 + tangent.dragDx
                                y: tangent.tipY - height / 2 + tangent.dragDy

                                HoverHandler { cursorShape: Qt.PointingHandCursor }

                                DragHandler {
                                    target: null
                                    onActiveChanged: {
                                        if (active) {
                                            // Freeze the model exactly as a key drag does, so the
                                            // Repeater cannot destroy this delegate mid-grab and
                                            // so the value axis stops auto-fitting under the cursor.
                                            const p = tangent.modelData.point
                                            root.frozenSeries = root.series
                                            root.dragProp = root.series[root.focusedIndex].prop
                                            root.dragTangentIndex = tangent.modelData.pointIndex
                                            root.dragTangentInDx = p.inDx || 0
                                            root.dragTangentInDy = p.inDy || 0
                                            root.dragTangentOutDx = p.outDx || 0
                                            root.dragTangentOutDy = p.outDy || 0
                                            root.draggingKey = true
                                            EditorState.beginPreviewDrag(qsTr("Edit keyframe curve"))
                                        } else {
                                            EditorState.commitPreviewDrag()
                                            root.draggingKey = false
                                            root.dragProp = ""
                                            root.dragTangentIndex = -1
                                            tangent.dragDx = 0
                                            tangent.dragDy = 0
                                        }
                                    }
                                    onTranslationChanged: {
                                        if (!active)
                                            return
                                        const p = tangent.modelData.point
                                        const outgoing = tangent.modelData.outgoing

                                        const newX = tangent.tipX + translation.x
                                        const newY = tangent.tipY + translation.y
                                        // dx keeps its sign: an out-handle may not reach back
                                        // past its key, nor an in-handle forward past its own.
                                        let dx = root.secondsForX(newX - tangent.keyX)
                                        dx = outgoing ? Math.max(0, dx) : Math.min(0, dx)
                                        const dy = root.valueForY(newY, tangent.entry) - p.value

                                        const inDx = outgoing ? (p.inDx || 0) : dx
                                        const outDx = outgoing ? dx : (p.outDx || 0)
                                        let inDy = outgoing ? (p.inDy || 0) : dy
                                        let outDy = outgoing ? dy : (p.outDy || 0)

                                        // Unless the key is a corner, the far tangent takes the
                                        // same slope so the curve stays smooth through the key.
                                        // Its dx has the opposite sign, so its dy flips with it.
                                        if (!p.corner && Math.abs(dx) > 1e-6) {
                                            const slope = dy / dx
                                            if (outgoing)
                                                inDy = slope * inDx
                                            else
                                                outDy = slope * outDx
                                        }

                                        EditorState.previewSetKeyframeTangents(
                                            EditorState.selectedTrack, EditorState.selectedClip,
                                            root.series[root.focusedIndex].prop,
                                            p.seconds, inDx, inDy, outDx, outDy, p.corner)

                                        root.dragTangentInDx = inDx
                                        root.dragTangentInDy = inDy
                                        root.dragTangentOutDx = outDx
                                        root.dragTangentOutDy = outDy
                                        tangent.dragDx = newX - tangent.tipX
                                        tangent.dragDy = newY - tangent.tipY
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

    }

    // Bottom edge resize grip. A sibling of the Row rather than a child, so it anchors to the
    // lane instead of being laid out horizontally beside the graph, and sits above the lane's
    // content so a drag near the boundary resizes rather than grabbing what is underneath.
    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 5
        z: 100
        color: resizeHover.hovered || resizeDrag.active ? Theme.primary : "transparent"
        opacity: 0.6

        HoverHandler {
            id: resizeHover
            cursorShape: Qt.SizeVerCursor
        }
        DragHandler {
            id: resizeDrag
            target: null
            yAxis.enabled: true
            xAxis.enabled: false
            property real startHeight: 0
            onActiveChanged: {
                if (active)
                    startHeight = root.laneHeight
            }
            onTranslationChanged: {
                if (!active)
                    return
                root.laneHeight = Math.max(root.minLaneHeight,
                                           Math.min(root.maxLaneHeight,
                                                    startHeight + translation.y))
            }
        }
    }
}
