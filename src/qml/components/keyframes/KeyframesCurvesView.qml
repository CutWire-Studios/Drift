pragma ComponentBehavior: Bound
import QtQuick
import Drift
import "EasingCache.js" as EasingCache

Item {
    id: root

    required property var panel

    readonly property var timeView: panel.timeView
    readonly property bool busy: surface.busy
    readonly property real graphMargin: Theme.spacingLg
    readonly property real graphHeight: Math.max(1, height - 2 * graphMargin)

    property real vMin: 0
    property real vMax: 1
    property var lockedRanges: null
    property bool needsFit: true
    property string focusedTrack: ""
    property string hoverId: ""

    signal contextRequested(real x, real y, var hit)

    // Host contract of KeyframeGraphCurveCanvas / KeyframeGraphTangentHandle.
    readonly property real pxPerSecond: timeView.pxPerSecond
    readonly property real paintChunk: 256
    readonly property real paintOriginX: Math.floor(timeView.contentX / paintChunk) * paintChunk - paintChunk
    readonly property real clipStart: timeView.clipStart
    readonly property real clipDuration: timeView.clipDuration
    readonly property bool curveEditing: true
    readonly property string focusedProp: panel.focusedProp
    readonly property real laneHeight: graphHeight
    readonly property real defaultHandleSeconds: 0.12

    property bool draggingKey: false
    property var frozenSeries: []
    property string dragProp: ""
    property int dragIndex: -1
    property real dragSeconds: 0
    property real dragValue: 0
    property int dragTangentIndex: -1
    property real dragTangentInDx: 0
    property real dragTangentInDy: 0
    property real dragTangentOutDx: 0
    property real dragTangentOutDy: 0

    function xForSeconds(seconds) {
        return seconds * pxPerSecond
    }

    function secondsForX(x) {
        return x / pxPerSecond
    }

    function yForValue(v, entry) {
        const span = Math.max(1e-6, entry.valueMax - entry.valueMin)
        return graphHeight - ((v - entry.valueMin) / span) * graphHeight
    }

    function valueForY(y, entry) {
        const span = Math.max(1e-6, entry.valueMax - entry.valueMin)
        return entry.valueMin + (graphHeight - y) / graphHeight * span
    }

    function screenY(v, entry) {
        return graphMargin + yForValue(v, entry)
    }

    function easingFnSpec(key) {
        return { "kind": "function", "fn": key.fn, "amplitude": key.fnAmp, "period": key.fnPeriod }
    }

    function handleSeconds(point, outgoing) {
        const raw = outgoing ? (point.outDx || 0) : (point.inDx || 0)
        if (Math.abs(raw) > 1e-6)
            return raw
        return outgoing ? defaultHandleSeconds : -defaultHandleSeconds
    }

    function handleValue(point, outgoing) {
        return point.value + (outgoing ? (point.outDy || 0) : (point.inDy || 0))
    }

    function isFocused(index) {
        return focusedIndex < 0 || series[index].rowProp === panel.focusedProp
    }

    function valueRangeFor(id, pts, base, row) {
        if (id === "opacity")
            return { "min": 0, "max": 1 }
        if (id === "volume")
            return { "min": 0, "max": 2 }
        if (id === "rotation" || id === "mask.rotation")
            return { "min": -180, "max": 180 }
        if (id === "mask.x" || id === "mask.y" || id === "mask.w" || id === "mask.h")
            return { "min": 0, "max": 1 }
        if ((id.substring(0, 3) === "fx." || id.substring(0, 4) === "afx.") && row.hasRange)
            return { "min": row.min, "max": row.max }
        let lo = Infinity
        let hi = -Infinity
        for (let i = 0; i < pts.length; ++i) {
            const v = Number(pts[i].value)
            lo = Math.min(lo, v)
            hi = Math.max(hi, v)
            if (pts[i].fn && i + 1 < pts.length) {
                const span = Number(pts[i + 1].value) - v
                const samples = EasingCache.samples(easingFnSpec(pts[i]), 48, (s, n) => EditorState.easingSamples(s, n))
                for (let k = 0; k < samples.length; ++k) {
                    const w = v + span * samples[k].y
                    lo = Math.min(lo, w)
                    hi = Math.max(hi, w)
                }
            }
        }
        if (!isFinite(lo))
            lo = base
        if (!isFinite(hi))
            hi = base
        return {
            "min": lo - Math.max(1, Math.abs(lo) * 0.1),
            "max": hi + Math.max(1, Math.abs(hi) * 0.1)
        }
    }

    function buildEntries() {
        const out = []
        const rows = panel.rows
        for (let i = 0; i < rows.length; ++i) {
            const r = rows[i]
            if (r.type === "group" || r.kind === "color" || !panel.curveShown[r.keyProp])
                continue
            const stepped = r.kind === "bool" || r.kind === "enum" || r.kind === "int"
            for (let t = 0; t < r.tracks.length; ++t) {
                const id = r.tracks[t]
                const raw = (r.trackKeys && r.trackKeys[id]) || []
                const pts = stepped ? raw.map(k => Object.assign({}, k, { "hold": true })) : raw
                const base = pts.length > 0 ? Number(pts[0].value)
                                            : EditorState.propertyBaseValue(panel.track, panel.clip, id, 0)
                const range = valueRangeFor(id, pts, base, r)
                out.push({
                    "prop": id,
                    "rowProp": r.prop,
                    "rowIndex": i,
                    "label": r.tracks.length > 1 ? r.label + " " + id.substring(id.lastIndexOf(".") + 1) : r.label,
                    "color": Theme.keyframeCurveColor(id),
                    "points": pts,
                    "baseValue": base,
                    "enabled": r.enabled,
                    "shown": true,
                    "ownMin": range.min,
                    "ownMax": range.max,
                    "valueMin": range.min,
                    "valueMax": range.max
                })
            }
        }
        return out
    }

    readonly property var baseEntries: {
        void panel.rows
        void panel.curveShown
        return buildEntries()
    }

    readonly property var liveSeries: {
        const out = []
        for (let i = 0; i < baseEntries.length; ++i) {
            const e = baseEntries[i]
            const lock = lockedRanges ? lockedRanges[e.prop] : null
            let lo = vMin
            let hi = vMax
            if (lock) {
                lo = lock.min
                hi = lock.max
            } else if (panel.normalize) {
                lo = e.ownMin
                hi = e.ownMax
            }
            out.push(Object.assign({}, e, { "valueMin": lo, "valueMax": hi }))
        }
        return out
    }

    readonly property var series: draggingKey ? frozenSeries : liveSeries

    readonly property int focusedIndex: {
        if (panel.focusedProp === "")
            return -1
        let first = -1
        for (let i = 0; i < series.length; ++i) {
            if (series[i].rowProp !== panel.focusedProp)
                continue
            if (series[i].prop === focusedTrack)
                return i
            if (first < 0)
                first = i
        }
        return first
    }

    readonly property var keyHandles: {
        const out = []
        for (let s = 0; s < series.length; ++s) {
            const entry = series[s]
            for (let i = 0; i < entry.points.length; ++i) {
                out.push({
                    "seriesIndex": s,
                    "prop": entry.prop,
                    "color": entry.color,
                    "enabled": entry.enabled,
                    "seconds": entry.points[i].seconds,
                    "value": entry.points[i].value,
                    "hold": !!entry.points[i].hold
                })
            }
        }
        return out
    }

    readonly property var tangentHandles: {
        const out = []
        if (focusedIndex < 0 || focusedIndex >= series.length)
            return out
        const entry = series[focusedIndex]
        const pts = entry.points
        for (let i = 0; i < pts.length; ++i) {
            const leavesShaped = pts[i].hold || !!pts[i].fn
            if (i > 0 && !(pts[i - 1].hold || !!pts[i - 1].fn))
                out.push({ "pointIndex": i, "outgoing": false, "color": entry.color, "point": pts[i] })
            if (i < pts.length - 1 && !leavesShaped)
                out.push({ "pointIndex": i, "outgoing": true, "color": entry.color, "point": pts[i] })
        }
        return out
    }

    readonly property var labelRange: {
        if (!panel.normalize)
            return { "min": vMin, "max": vMax, "labelled": true, "color": Theme.c.keyframesCurves.label }
        if (focusedIndex >= 0 && focusedIndex < series.length)
            return { "min": series[focusedIndex].valueMin, "max": series[focusedIndex].valueMax,
                     "labelled": true, "color": series[focusedIndex].color }
        return { "min": 0, "max": 1, "labelled": false, "color": Theme.c.keyframesCurves.label }
    }

    readonly property var gridTicks: {
        const lo = labelRange.min
        const hi = labelRange.max
        const out = []
        if (!labelRange.labelled) {
            for (let i = 0; i <= 4; ++i)
                out.push({ "frac": i / 4, "text": "" })
            return out
        }
        const span = Math.max(1e-6, hi - lo)
        const raw = span / 5
        const mag = Math.pow(10, Math.floor(Math.log(raw) / Math.LN10))
        const norm = raw / mag
        const step = (norm < 1.5 ? 1 : norm < 3.5 ? 2 : norm < 7.5 ? 5 : 10) * mag
        const decimals = Math.max(0, Math.min(6, Math.ceil(-Math.log(step) / Math.LN10 - 1e-9)))
        for (let v = Math.ceil(lo / step) * step; v <= hi + step * 1e-6; v += step)
            out.push({ "frac": (v - lo) / span, "text": Number(v.toFixed(decimals)).toString() })
        return out
    }

    function lockRanges() {
        const map = {}
        for (let i = 0; i < liveSeries.length; ++i)
            map[liveSeries[i].prop] = { "min": liveSeries[i].valueMin, "max": liveSeries[i].valueMax }
        lockedRanges = map
    }

    function unlockRanges() {
        lockedRanges = null
    }

    function fitValues(selectedOnly) {
        let lo = Infinity
        let hi = -Infinity
        for (let i = 0; i < baseEntries.length; ++i) {
            const e = baseEntries[i]
            if (e.points.length === 0) {
                if (!selectedOnly) {
                    lo = Math.min(lo, e.baseValue)
                    hi = Math.max(hi, e.baseValue)
                }
                continue
            }
            for (let p = 0; p < e.points.length; ++p) {
                const pt = e.points[p]
                if (selectedOnly && !panel.selection.has(e.prop, pt.seconds))
                    continue
                const v = Number(pt.value)
                const reach = [v, v + (pt.outDy || 0), v + (pt.inDy || 0)]
                if (pt.fn && p + 1 < e.points.length) {
                    const span = Number(e.points[p + 1].value) - v
                    const samples = EasingCache.samples(easingFnSpec(pt), 48, (s, n) => EditorState.easingSamples(s, n))
                    for (let k = 0; k < samples.length; ++k)
                        reach.push(v + span * samples[k].y)
                }
                for (let k = 0; k < reach.length; ++k) {
                    lo = Math.min(lo, reach[k])
                    hi = Math.max(hi, reach[k])
                }
            }
        }
        if (!isFinite(lo)) {
            if (selectedOnly)
                fitValues(false)
            return
        }
        const pad = Math.max((hi - lo) * 0.1, hi - lo < 1e-9 ? Math.max(1, Math.abs(lo) * 0.1) : 0)
        vMin = lo - pad
        vMax = hi + pad
    }

    function zoomValuesAt(y, factor) {
        const t = Math.max(0, Math.min(1, (graphMargin + graphHeight - y) / graphHeight))
        const span = vMax - vMin
        const anchor = vMin + t * span
        const next = Math.max(1e-6, span * factor)
        vMin = anchor - t * next
        vMax = vMin + next
    }

    function panValuesBy(dy) {
        const dv = dy / graphHeight * (vMax - vMin)
        vMin += dv
        vMax += dv
    }

    function gripAt(x, y) {
        for (let i = 0; i < tangentHandles.length; ++i) {
            const h = tangentHandles[i]
            const entry = series[focusedIndex]
            const gx = timeView.xFor(h.point.seconds + handleSeconds(h.point, h.outgoing))
            const gy = screenY(handleValue(h.point, h.outgoing), entry)
            if (Math.hypot(gx - x, gy - y) <= Theme.c.keyframesKeyDot.size)
                return true
        }
        return false
    }

    function cancelDrag() {
        surface.cancelDrag()
    }

    clip: true
    onBaseEntriesChanged: {
        if (needsFit && baseEntries.length > 0) {
            needsFit = false
            fitValues(false)
        }
    }

    Connections {
        target: root.panel

        function onTrackChanged() { root.needsFit = true }
        function onClipChanged() { root.needsFit = true }
        function onNormalizeChanged() {
            if (!root.panel.normalize)
                root.fitValues(false)
        }
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.c.keyframesCurves.background
    }

    Rectangle {
        readonly property real x0: root.timeView.xFor(root.timeView.clipStart)
        readonly property real x1: root.timeView.xFor(root.timeView.clipStart + root.timeView.clipDuration)
        x: Math.max(0, x0)
        width: Math.max(0, Math.min(root.width, x1) - x)
        height: parent.height
        color: Qt.rgba(Theme.c.keyframesCurves.clipSpan.r, Theme.c.keyframesCurves.clipSpan.g,
                       Theme.c.keyframesCurves.clipSpan.b, Theme.c.keyframesCurves.opacityClipSpan)
    }

    Repeater {
        model: root.gridTicks
        delegate: Rectangle {
            required property var modelData

            y: root.graphMargin + root.graphHeight * (1 - modelData.frac)
            width: root.width
            height: Theme.c.panelFrame.borderWidth
            color: Qt.rgba(Theme.c.keyframesCurves.grid.r, Theme.c.keyframesCurves.grid.g,
                           Theme.c.keyframesCurves.grid.b, Theme.c.keyframesCurves.opacityGrid)
        }
    }

    Item {
        id: graph
        x: -root.timeView.contentX
        y: root.graphMargin
        width: root.width
        height: root.graphHeight

        KeyframeGraphCurveCanvas {
            host: root
            viewportWidth: root.width
        }
    }

    KeyframesCurvesInteraction {
        id: surface
        z: 3
        anchors.fill: parent
        panel: root.panel
        view: root
        onContextRequested: (x, y, hit) => root.contextRequested(x, y, hit)
    }

    Repeater {
        model: root.keyHandles
        delegate: KeyframesKeyDot {
            id: dot

            required property var modelData

            readonly property real cx: root.timeView.xFor(modelData.seconds)

            z: 4
            visible: cx > -width && cx < root.width + width
            x: cx - width / 2
            y: root.graphMargin + root.yForValue(modelData.value, root.series[modelData.seriesIndex]) - height / 2
            color: modelData.color
            hold: modelData.hold
            disabled: !modelData.enabled
            collides: {
                void EditorState.keyframes.collisions
                return EditorState.keyframes.collidesWith([{ "prop": modelData.prop, "seconds": modelData.seconds }])
            }
            selected: {
                void root.panel.selection.revision
                return root.panel.selection.has(modelData.prop, modelData.seconds)
            }
            hovered: root.hoverId === root.panel.selection.keyId(modelData.prop, modelData.seconds)
            opacity: (modelData.enabled ? 1 : Theme.c.keyframesKeyDot.opacityDisabled)
                     * (root.isFocused(modelData.seriesIndex) ? 1 : Theme.c.keyframesCurves.opacityContext)
        }
    }

    Item {
        id: handleLayer
        z: 5
        x: -root.timeView.contentX
        y: root.graphMargin
        width: root.width
        height: root.graphHeight

        Repeater {
            model: root.tangentHandles
            delegate: KeyframeGraphTangentHandle {
                host: root
            }
        }
    }

    Item {
        z: 6
        anchors.fill: parent

        Repeater {
            model: root.labelRange.labelled ? root.gridTicks : []
            delegate: Text {
                required property var modelData

                x: Theme.spacingXs
                y: root.graphMargin + root.graphHeight * (1 - modelData.frac) - height
                text: modelData.text
                color: root.labelRange.color
                font.family: Theme.monoFontFamily
                font.pixelSize: Theme.fontSizeXs
            }
        }
    }
}
