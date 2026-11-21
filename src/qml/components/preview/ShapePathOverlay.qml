import QtQuick
import QtQuick.Shapes
import Drift

// The pen tool. In "edit" mode it reshapes the selected Path shape clip's outline; in "draw" mode
// it lays out a new path on the canvas and turns it into a clip when finished.
//
// Both modes work on the same list of contours ({closed, vertices:[{x, y, inX, inY, outX, outY,
// smooth}]}, tangents as offsets from the vertex). Edit mode holds them normalized to the clip's
// frame, as the model does; draw mode holds canvas pixels. `plane` is the item those units are
// laid out on, so a point's position is always (x * unitX, y * unitY) inside it.
//
// Pointer handling is one MouseArea with hit-testing done here rather than per-vertex areas:
// every live edit refits the clip's box and hands back fresh contours, and a delegate that owned
// the grab would be rebuilt under the drag.
Item {
    id: root

    property Item canvas
    property bool interacting: false

    readonly property string mode: EditorState.preview.shapePathMode
    readonly property bool drawing: mode === "draw"
    readonly property int trackIdx: EditorState.selectedTrack
    readonly property int clipIdx: EditorState.selectedClip

    // PreviewController::trackerFrameOf for the edited clip; empty while drawing.
    property var ed: ({})
    property var contours: []
    property int selC: -1
    property int selV: -1
    property bool extending: false
    property point hoverPos: Qt.point(0, 0)
    property bool hoverValid: false
    property bool hoverNearFirst: false

    readonly property real canvasW: Math.max(1, drawing ? EditorState.projectFile.projectWidth() : (ed.canvasWidth || 1))
    readonly property real canvasH: Math.max(1, drawing ? EditorState.projectFile.projectHeight() : (ed.canvasHeight || 1))
    readonly property real sx: (canvas ? canvas.width : width) / canvasW
    readonly property real sy: (canvas ? canvas.height : height) / canvasH
    readonly property bool hasFrame: drawing || !!ed.hasFrame
    readonly property real unitX: drawing ? sx : plane.width
    readonly property real unitY: drawing ? sy : plane.height

    readonly property var selContour: selC >= 0 && selC < contours.length ? contours[selC] : null
    readonly property bool canExtend: !!selContour && !drawing && !selContour.closed && selV >= 0
                                      && (selV === 0 || selV === selContour.vertices.length - 1)
    onCanExtendChanged: if (!canExtend) extending = false

    readonly property var vtx: buildVertices(contours, selC, selV, unitX, unitY, hoverNearFirst, drawing)
    readonly property var hdl: buildHandles(contours, selC, selV, unitX, unitY)
    readonly property string outlineD: pathD(contours, unitX, unitY)
    readonly property string handleD: handleLines(hdl)
    readonly property string rubberD: rubberPath()

    readonly property var shapeStyle: (EditorState.selectedClipData || ({})).shapeStyle || ({})
    readonly property bool pathAnimated: !drawing && !!shapeStyle.pathAnimated

    // ---- geometry helpers -------------------------------------------------

    function clone(cs) {
        return JSON.parse(JSON.stringify(cs))
    }

    function newVertex(x, y) {
        return { "x": x, "y": y, "inX": 0, "inY": 0, "outX": 0, "outY": 0, "smooth": false }
    }

    function hasCurve(a, b) {
        return !!(a.outX || a.outY || b.inX || b.inY)
    }

    function segD(a, b, kx, ky) {
        if (!hasCurve(a, b))
            return "L" + b.x * kx + " " + b.y * ky
        return "C" + (a.x + a.outX) * kx + " " + (a.y + a.outY) * ky + " "
                + (b.x + b.inX) * kx + " " + (b.y + b.inY) * ky + " " + b.x * kx + " " + b.y * ky
    }

    function pathD(cs, kx, ky) {
        let d = ""
        for (const c of cs) {
            const v = c.vertices
            if (v.length === 0)
                continue
            d += "M" + v[0].x * kx + " " + v[0].y * ky
            for (let i = 0; i + 1 < v.length; ++i)
                d += segD(v[i], v[i + 1], kx, ky)
            if (c.closed && v.length > 1)
                d += segD(v[v.length - 1], v[0], kx, ky) + "Z"
        }
        return d === "" ? "M0 0" : d
    }

    function rubberPath() {
        if (!drawing || !hoverValid || contours.length === 0)
            return "M0 0"
        const v = contours[0].vertices
        if (v.length === 0)
            return "M0 0"
        const last = v[v.length - 1]
        const h = { "x": hoverPos.x / unitX, "y": hoverPos.y / unitY, "inX": 0, "inY": 0, "outX": 0, "outY": 0 }
        return "M" + last.x * unitX + " " + last.y * unitY + segD(last, h, unitX, unitY)
    }

    function neighbours(c, i) {
        const n = c.vertices.length
        const out = [i]
        if (i > 0 || c.closed)
            out.push((i - 1 + n) % n)
        if (i < n - 1 || c.closed)
            out.push((i + 1) % n)
        return out
    }

    function buildVertices(cs, sc, sv, kx, ky, nearFirst, draw) {
        const out = []
        for (let ci = 0; ci < cs.length; ++ci) {
            const c = cs[ci]
            for (let i = 0; i < c.vertices.length; ++i) {
                const v = c.vertices[i]
                out.push({ "c": ci, "v": i, "px": v.x * kx, "py": v.y * ky, "smooth": !!v.smooth,
                           "selected": ci === sc && i === sv, "first": i === 0, "open": !c.closed,
                           "closing": draw && i === 0 && nearFirst && c.vertices.length >= 3 })
            }
        }
        return out
    }

    // Tangents of the selected vertex and the two next to it: those are the ones shaping the
    // segments the selection touches.
    function buildHandles(cs, sc, sv, kx, ky) {
        const out = []
        if (sc < 0 || sc >= cs.length || sv < 0 || sv >= cs[sc].vertices.length)
            return out
        const c = cs[sc]
        for (const i of neighbours(c, sv)) {
            const v = c.vertices[i]
            if (v.inX || v.inY)
                out.push({ "c": sc, "v": i, "k": "in", "px": (v.x + v.inX) * kx, "py": (v.y + v.inY) * ky,
                           "vx": v.x * kx, "vy": v.y * ky })
            if (v.outX || v.outY)
                out.push({ "c": sc, "v": i, "k": "out", "px": (v.x + v.outX) * kx, "py": (v.y + v.outY) * ky,
                           "vx": v.x * kx, "vy": v.y * ky })
        }
        return out
    }

    function handleLines(list) {
        let d = ""
        for (const h of list)
            d += "M" + h.vx + " " + h.vy + "L" + h.px + " " + h.py
        return d === "" ? "M0 0" : d
    }

    function snap45(ax, ay, bx, by) {
        const dx = bx - ax
        const dy = by - ay
        const len = Math.hypot(dx, dy)
        const a = Math.round(Math.atan2(dy, dx) / (Math.PI / 4)) * (Math.PI / 4)
        return Qt.point(ax + Math.cos(a) * len, ay + Math.sin(a) * len)
    }

    function lerp(a, b, t) {
        return { "x": a.x + (b.x - a.x) * t, "y": a.y + (b.y - a.y) * t }
    }

    // The cubic from vertex i to the next one, in absolute data units.
    function segmentPoints(c, i) {
        const a = c.vertices[i]
        const b = c.vertices[(i + 1) % c.vertices.length]
        return [{ "x": a.x, "y": a.y }, { "x": a.x + a.outX, "y": a.y + a.outY },
                { "x": b.x + b.inX, "y": b.y + b.inY }, { "x": b.x, "y": b.y }]
    }

    function cubicAt(p, t) {
        const q0 = lerp(p[0], p[1], t)
        const q1 = lerp(p[1], p[2], t)
        const q2 = lerp(p[2], p[3], t)
        const r0 = lerp(q0, q1, t)
        const r1 = lerp(q1, q2, t)
        return lerp(r0, r1, t)
    }

    // Splits segment `seg` of contour `ci` at t with de Casteljau, so the outline does not change.
    function insertVertex(cs, ci, seg, t) {
        const c = cs[ci]
        const n = c.vertices.length
        const p = segmentPoints(c, seg)
        const q0 = lerp(p[0], p[1], t)
        const q1 = lerp(p[1], p[2], t)
        const q2 = lerp(p[2], p[3], t)
        const r0 = lerp(q0, q1, t)
        const r1 = lerp(q1, q2, t)
        const s = lerp(r0, r1, t)
        const a = c.vertices[seg]
        const b = c.vertices[(seg + 1) % n]
        const curved = hasCurve(a, b)
        a.outX = q0.x - p[0].x
        a.outY = q0.y - p[0].y
        b.inX = q2.x - p[3].x
        b.inY = q2.y - p[3].y
        const nv = newVertex(s.x, s.y)
        if (curved) {
            nv.inX = r0.x - s.x
            nv.inY = r0.y - s.y
            nv.outX = r1.x - s.x
            nv.outY = r1.y - s.y
            nv.smooth = true
        }
        c.vertices.splice(seg + 1, 0, nv)
        return seg + 1
    }

    function reverseContour(c) {
        c.vertices.reverse()
        for (const v of c.vertices) {
            const ix = v.inX
            const iy = v.inY
            v.inX = v.outX
            v.inY = v.outY
            v.outX = ix
            v.outY = iy
        }
    }

    function makeSmooth(c, i) {
        const n = c.vertices.length
        const v = c.vertices[i]
        const hasPrev = i > 0 || c.closed
        const hasNext = i < n - 1 || c.closed
        const prev = c.vertices[(i - 1 + n) % n]
        const next = c.vertices[(i + 1) % n]
        let tx = 0
        let ty = 0
        if (hasPrev && hasNext && n > 2) {
            tx = (next.x - prev.x) / 6
            ty = (next.y - prev.y) / 6
        } else if (hasNext && n > 1) {
            tx = (next.x - v.x) / 3
            ty = (next.y - v.y) / 3
        } else if (hasPrev && n > 1) {
            tx = (v.x - prev.x) / 3
            ty = (v.y - prev.y) / 3
        }
        v.outX = tx
        v.outY = ty
        v.inX = -tx
        v.inY = -ty
        v.smooth = true
    }

    function makeCorner(v) {
        v.inX = 0
        v.inY = 0
        v.outX = 0
        v.outY = 0
        v.smooth = false
    }

    // ---- hit testing (plane pixels) --------------------------------------

    function distToSegment(px, py, ax, ay, bx, by) {
        const dx = bx - ax
        const dy = by - ay
        const l2 = dx * dx + dy * dy
        const s = l2 > 0 ? Math.max(0, Math.min(1, ((px - ax) * dx + (py - ay) * dy) / l2)) : 0
        return { "d": Math.hypot(px - (ax + dx * s), py - (ay + dy * s)), "s": s }
    }

    function hitTest(p) {
        for (const h of hdl) {
            if (Math.hypot(p.x - h.px, p.y - h.py) <= 9)
                return { "kind": "handle", "c": h.c, "v": h.v, "k": h.k }
        }
        let best = null
        let bestD = 10
        for (const v of vtx) {
            const d = Math.hypot(p.x - v.px, p.y - v.py)
            if (d <= bestD) {
                bestD = d
                best = { "kind": "vertex", "c": v.c, "v": v.v }
            }
        }
        if (best)
            return best
        const steps = 24
        bestD = 7
        for (let ci = 0; ci < contours.length; ++ci) {
            const c = contours[ci]
            const n = c.vertices.length
            const segs = c.closed ? n : n - 1
            for (let i = 0; i < segs && n > 1; ++i) {
                const pts = segmentPoints(c, i)
                let prev = pts[0]
                for (let k = 1; k <= steps; ++k) {
                    const cur = cubicAt(pts, k / steps)
                    const r = distToSegment(p.x, p.y, prev.x * unitX, prev.y * unitY, cur.x * unitX, cur.y * unitY)
                    if (r.d < bestD) {
                        bestD = r.d
                        best = { "kind": "segment", "c": ci, "seg": i, "t": (k - 1 + r.s) / steps }
                    }
                    prev = cur
                }
            }
        }
        return best || { "kind": "none" }
    }

    // ---- state ------------------------------------------------------------

    function refresh() {
        if (!visible || interacting || EditorState.playing)
            return
        if (drawing)
            return
        const t = EditorState.selectedTrack
        const c = EditorState.selectedClip
        ed = EditorState.preview.trackerFrameOf(t, c)
        contours = EditorState.shapeContoursAt(t, c)
        if (selC >= contours.length || (selC >= 0 && selV >= contours[selC].vertices.length)) {
            selC = -1
            selV = -1
        }
    }

    function adopt(result) {
        if (result && result.length > 0)
            contours = result
        ed = EditorState.preview.trackerFrameOf(trackIdx, clipIdx)
    }

    // Draw mode keeps the draft here; edit mode round-trips through the clip, which refits its
    // box (so the contours and the frame both change) and, for a live drag, folds into one undo
    // step on the final call.
    function apply(cs, live) {
        if (drawing)
            contours = cs
        else
            adopt(EditorState.setShapeContours(trackIdx, clipIdx, cs, !live))
    }

    function planePoint(rootPos) {
        return plane.mapFromItem(root, rootPos.x, rootPos.y)
    }

    function toData(planePos) {
        return Qt.point(planePos.x / unitX, planePos.y / unitY)
    }

    function select(c, v) {
        selC = c
        selV = v
    }

    function deleteSelected() {
        if (drawing) {
            if (contours.length === 0 || contours[0].vertices.length === 0)
                return false
            const cs = clone(contours)
            cs[0].vertices.pop()
            contours = cs
            return true
        }
        if (selC < 0 || selV < 0)
            return false
        const cs = clone(contours)
        const c = cs[selC]
        c.vertices.splice(selV, 1)
        if (c.vertices.length < 2)
            cs.splice(selC, 1)
        if (cs.length === 0)
            return true
        const ci = Math.min(selC, cs.length - 1)
        const vi = cs[selC] === c ? Math.min(selV, c.vertices.length - 1) : -1
        apply(cs, false)
        select(vi >= 0 ? ci : -1, vi)
        return true
    }

    function toggleSmoothAt(c, v) {
        const cs = clone(contours)
        const vert = cs[c].vertices[v]
        if (vert.smooth || vert.inX || vert.inY || vert.outX || vert.outY)
            makeCorner(vert)
        else
            makeSmooth(cs[c], v)
        apply(cs, false)
    }

    function toggleClosed() {
        if (!selContour)
            return
        const cs = clone(contours)
        cs[selC].closed = !cs[selC].closed
        apply(cs, false)
    }

    function finishDraw() {
        if (!drawing || contours.length === 0 || contours[0].vertices.length < 2)
            return
        const made = EditorState.addPathShapeClip(contours, -1, -1)
        contours = []
        if (made && made.track !== undefined)
            EditorState.preview.shapePathMode = "edit"
        else
            EditorState.preview.shapePathMode = ""
    }

    function done() {
        if (drawing && contours.length > 0 && contours[0].vertices.length >= 2)
            finishDraw()
        else
            EditorState.preview.shapePathMode = ""
    }

    readonly property string scope: "global"
    readonly property int priority: 1

    // Escape, then Delete, as routed by Main before any panel sees them.
    function handleAction(id) {
        if (!visible || (Window.activeFocusItem && Window.activeFocusItem.cursorPosition !== undefined))
            return false
        if (id === "clearSelection") {
            if (drawing) {
                if (contours.length > 0 && contours[0].vertices.length > 0)
                    contours = []
                else
                    EditorState.preview.shapePathMode = ""
            } else if (extending) {
                extending = false
            } else if (selV >= 0) {
                select(-1, -1)
            } else {
                EditorState.preview.shapePathMode = ""
            }
            return true
        }
        if (id === "delete" && (ShellBridge.previewHovered || activeFocus))
            return deleteSelected()
        return false
    }

    // ---- pointer ----------------------------------------------------------

    property real lastPressTime: 0
    property point lastPressPos: Qt.point(0, 0)
    property var gesture: null

    function beginDrag(kind, c, v, k, pressRoot, targetPlane) {
        const grab = plane.mapToItem(root, targetPlane.x, targetPlane.y)
        gesture = { "kind": kind, "c": c, "v": v, "k": k, "press": pressRoot,
                 "grabX": grab.x - pressRoot.x, "grabY": grab.y - pressRoot.y,
                 "startX": grab.x, "startY": grab.y, "moved": false, "open": false, "pen": false }
        interacting = true
    }

    function moveDrag(pos, mods) {
        const d = gesture
        if (!d)
            return
        if (!d.moved) {
            if (Math.hypot(pos.x - d.press.x, pos.y - d.press.y) < 3)
                return
            d.moved = true
        }
        let tx = pos.x + d.grabX
        let ty = pos.y + d.grabY
        if ((mods & Qt.ShiftModifier) && d.kind === "vertex") {
            const s = snap45(d.startX, d.startY, tx, ty)
            tx = s.x
            ty = s.y
        }
        const pp = plane.mapFromItem(root, tx, ty)
        const cs = clone(contours)
        const v = cs[d.c].vertices[d.v]
        if (d.kind === "vertex") {
            v.x = pp.x / unitX
            v.y = pp.y / unitY
        } else {
            let ox = pp.x / unitX - v.x
            let oy = pp.y / unitY - v.y
            if (mods & Qt.ShiftModifier) {
                const s = snap45(0, 0, ox * unitX, oy * unitY)
                ox = s.x / unitX
                oy = s.y / unitY
            }
            const alt = (mods & Qt.AltModifier) !== 0
            if (d.pen)
                v.smooth = true
            if (alt)
                v.smooth = false
            if (d.k === "out") {
                v.outX = ox
                v.outY = oy
                if (v.smooth) {
                    v.inX = -ox
                    v.inY = -oy
                }
            } else {
                v.inX = ox
                v.inY = oy
                if (v.smooth) {
                    v.outX = -ox
                    v.outY = -oy
                }
            }
        }
        apply(cs, true)
        d.open = !drawing
    }

    function endDrag() {
        const d = gesture
        gesture = null
        interacting = false
        if (d && d.open && !drawing)
            apply(contours, false)
    }

    function pressEdit(pos, mods) {
        const pp = planePoint(pos)
        const hit = hitTest(pp)
        const dbl = Date.now() - lastPressTime < 400 && Math.hypot(pos.x - lastPressPos.x, pos.y - lastPressPos.y) < 6
        lastPressTime = dbl ? 0 : Date.now()
        lastPressPos = pos
        if (hit.kind === "vertex") {
            if (dbl && selC === hit.c && selV === hit.v) {
                toggleSmoothAt(hit.c, hit.v)
                return
            }
            const c = contours[hit.c]
            const n = c.vertices.length
            if (extending && hit.c === selC && n >= 3 && !c.closed
                    && ((selV === n - 1 && hit.v === 0) || (selV === 0 && hit.v === n - 1))) {
                const cs = clone(contours)
                cs[hit.c].closed = true
                apply(cs, false)
                extending = false
                select(hit.c, hit.v)
                return
            }
            select(hit.c, hit.v)
            beginDrag("vertex", hit.c, hit.v, "", pos, Qt.point(c.vertices[hit.v].x * unitX, c.vertices[hit.v].y * unitY))
        } else if (hit.kind === "handle") {
            const v = contours[hit.c].vertices[hit.v]
            const o = hit.k === "in" ? Qt.point(v.x + v.inX, v.y + v.inY) : Qt.point(v.x + v.outX, v.y + v.outY)
            beginDrag("handle", hit.c, hit.v, hit.k, pos, Qt.point(o.x * unitX, o.y * unitY))
        } else if (hit.kind === "segment") {
            const cs = clone(contours)
            const at = insertVertex(cs, hit.c, hit.seg, hit.t)
            apply(cs, true)
            select(hit.c, at)
            const v = contours[hit.c].vertices[at]
            beginDrag("vertex", hit.c, at, "", pos, Qt.point(v.x * unitX, v.y * unitY))
            gesture.open = true
        } else if (extending && canExtend) {
            const cs = clone(contours)
            const c = cs[selC]
            if (selV === 0 && c.vertices.length > 1)
                reverseContour(c)
            const dp = toData(pp)
            c.vertices.push(newVertex(dp.x, dp.y))
            apply(cs, true)
            const last = contours[selC].vertices.length - 1
            select(selC, last)
            beginDrag("handle", selC, last, "out", pos, pp)
            gesture.pen = true
            gesture.open = true
        } else {
            select(-1, -1)
        }
    }

    function pressDraw(pos, mods) {
        const pp = planePoint(pos)
        const dbl = Date.now() - lastPressTime < 400 && Math.hypot(pos.x - lastPressPos.x, pos.y - lastPressPos.y) < 6
        lastPressTime = dbl ? 0 : Date.now()
        lastPressPos = pos
        const cs = contours.length > 0 ? clone(contours) : [{ "closed": false, "vertices": [] }]
        const c = cs[0]
        if (dbl) {
            finishDraw()
            return
        }
        const n = c.vertices.length
        if (n >= 3 && Math.hypot(pp.x - c.vertices[0].x * unitX, pp.y - c.vertices[0].y * unitY) <= 10) {
            c.closed = true
            contours = cs
            finishDraw()
            return
        }
        let target = pp
        if ((mods & Qt.ShiftModifier) && n > 0)
            target = snap45(c.vertices[n - 1].x * unitX, c.vertices[n - 1].y * unitY, pp.x, pp.y)
        c.vertices.push(newVertex(target.x / unitX, target.y / unitY))
        contours = cs
        select(0, n)
        beginDrag("handle", 0, n, "out", pos, target)
        gesture.pen = true
    }

    function hover(pos, mods) {
        let pp = planePoint(pos)
        if (drawing && contours.length > 0 && contours[0].vertices.length > 0) {
            const v = contours[0].vertices
            if ((mods & Qt.ShiftModifier))
                pp = snap45(v[v.length - 1].x * unitX, v[v.length - 1].y * unitY, pp.x, pp.y)
            hoverNearFirst = v.length >= 3 && Math.hypot(pp.x - v[0].x * unitX, pp.y - v[0].y * unitY) <= 10
        } else {
            hoverNearFirst = false
        }
        hoverPos = Qt.point(pp.x, pp.y)
        hoverValid = true
    }

    onModeChanged: {
        selC = -1
        selV = -1
        extending = false
        contours = []
        ed = ({})
        hoverValid = false
        refresh()
        forceActiveFocus()
    }
    onVisibleChanged: if (visible) refresh()
    Component.onCompleted: {
        ShellBridge.registerActionHandler(root)
        refresh()
        forceActiveFocus()
    }
    Component.onDestruction: ShellBridge.unregisterActionHandler(root)

    focus: true
    Keys.onPressed: (event) => {
        if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
            root.done()
            event.accepted = true
        } else if (event.key === Qt.Key_Delete || event.key === Qt.Key_Backspace) {
            event.accepted = root.deleteSelected()
        }
    }

    Connections {
        target: EditorState
        function onTracksChanged() { root.refresh() }
        function onSelectionChanged() { root.refresh() }
        function onSelectedClipDataChanged() { root.refresh() }
        function onClipPropertiesPreviewed(trackIndex, clipIndex, keys) { root.refresh() }
        function onPlayheadSecondsChanged() {
            if (!EditorState.scrubbing)
                root.refresh()
        }
        function onScrubbingChanged() {
            if (!EditorState.scrubbing)
                root.refresh()
        }
        function onPlayingChanged() {
            if (!EditorState.playing)
                root.refresh()
        }
    }

    // The canvas, so the clip frame below lays out exactly as MaskOverlay's does.
    Item {
        id: canvasLayer
        x: root.canvas ? root.canvas.x : 0
        y: root.canvas ? root.canvas.y : 0
        width: root.canvas ? root.canvas.width : root.width
        height: root.canvas ? root.canvas.height : root.height

        Item {
            id: plane
            visible: root.hasFrame
            x: root.drawing ? 0 : (root.ed.x || 0) * root.sx
            y: root.drawing ? 0 : (root.ed.y || 0) * root.sy
            width: root.drawing ? canvasLayer.width : Math.max(1, (root.ed.width || root.canvasW) * root.sx)
            height: root.drawing ? canvasLayer.height : Math.max(1, (root.ed.height || root.canvasH) * root.sy)
            transformOrigin: Item.Center
            readonly property bool is3d: !root.drawing && ((root.ed.rotationX || 0) !== 0
                                                           || (root.ed.rotationY || 0) !== 0
                                                           || (root.ed.z || 0) !== 0
                                                           || root.ed.anchorX !== undefined
                                                           || root.ed.anchorY !== undefined)
            readonly property bool hasParent: !root.drawing && root.ed.parentActive === true
            rotation: root.drawing || is3d || hasParent ? 0 : (root.ed.rotation || 0)
            transform: Matrix4x4 {
                matrix: plane.is3d || plane.hasParent
                        ? EditorState.preview.clipPoseMatrix(root.ed, root.ed.x || 0, root.ed.y || 0,
                                                            root.ed.width || root.canvasW,
                                                            root.ed.height || root.canvasH,
                                                            root.ed.rotation || 0, root.sx, root.sy)
                        : Qt.matrix4x4()
            }

            Shape {
                anchors.fill: parent
                preferredRendererType: Shape.CurveRenderer

                ShapePath {
                    strokeColor: Theme.c.shapePathOverlay.handleLine
                    strokeWidth: 1
                    fillColor: "transparent"
                    PathSvg { path: root.handleD }
                }
                ShapePath {
                    strokeColor: Theme.c.shapePathOverlay.outline
                    strokeWidth: Theme.c.shapePathOverlay.outlineWidth
                    fillColor: "transparent"
                    PathSvg { path: root.outlineD }
                }
                ShapePath {
                    strokeColor: Theme.c.shapePathOverlay.rubberBand
                    strokeWidth: Theme.c.shapePathOverlay.outlineWidth
                    strokeStyle: ShapePath.DashLine
                    dashPattern: [4, 3]
                    fillColor: "transparent"
                    PathSvg { path: root.rubberD }
                }
            }

            Repeater {
                model: root.vtx.length
                delegate: ShapePathVertex {
                    required property int index
                    info: root.vtx[index] || ({ "px": 0, "py": 0, "smooth": false, "selected": false,
                                                "first": false, "open": false, "closing": false })
                }
            }

            Repeater {
                model: root.hdl.length
                delegate: ShapePathHandle {
                    required property int index
                    info: root.hdl[index] || ({ "px": 0, "py": 0 })
                }
            }
        }
    }

    ToolCursor {
        target: area
        shape: root.drawing || root.extending ? (root.hoverNearFirst ? "polygonClose" : "polygon") : ""
    }

    // The whole viewport, so a path can start or extend past the canvas edge.
    MouseArea {
        id: area
        anchors.fill: parent
        enabled: root.hasFrame
        hoverEnabled: true
        preventStealing: true
        acceptedButtons: Qt.LeftButton
        onWheel: (wheel) => { wheel.accepted = false }

        onPositionChanged: (mouse) => {
            if (root.gesture)
                root.moveDrag(Qt.point(mouse.x, mouse.y), mouse.modifiers)
            else
                root.hover(Qt.point(mouse.x, mouse.y), mouse.modifiers)
        }
        onPressed: (mouse) => {
            root.forceActiveFocus()
            if (root.drawing)
                root.pressDraw(Qt.point(mouse.x, mouse.y), mouse.modifiers)
            else
                root.pressEdit(Qt.point(mouse.x, mouse.y), mouse.modifiers)
        }
        onReleased: root.endDrag()
        onCanceled: root.endDrag()
    }

    Rectangle {
        visible: !root.drawing && !root.hasFrame
        anchors.centerIn: parent
        width: outsideText.width + Theme.spacingLg * 2
        height: outsideText.height + Theme.spacingMd * 2
        radius: Theme.c.shapePathHintBar.radius
        color: Theme.c.shapePathHintBar.background

        Text {
            id: outsideText
            anchors.centerIn: parent
            text: qsTr("Move the playhead over the selected clip")
            color: Theme.c.shapePathHintBar.foregroundMuted
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeXs
        }
    }

    ShapePathHintBar {
        title: root.drawing ? qsTr("Drawing a path") : qsTr("Editing path")
        hint: root.drawing
              ? qsTr("Click for a corner point, drag for a smooth one. Shift snaps to 45 degrees. Click the first point to close, Enter to finish, Esc to cancel.")
              : qsTr("Drag points and handles. Click a segment to add a point, double-click a point to switch corner and smooth, Alt-drag a handle to break it, Delete removes a point.")
        maxHintWidth: root.width * 0.55
        note: root.pathAnimated ? qsTr("This path is keyframed: edits are stored as a key at the playhead.") : ""
        canExtend: root.canExtend
        extending: root.extending
        canToggleClosed: !root.drawing && !!root.selContour
        closed: !!root.selContour && root.selContour.closed
        onExtendToggled: root.extending = !root.extending
        onClosedToggled: root.toggleClosed()
        onDone: root.done()
    }
}
