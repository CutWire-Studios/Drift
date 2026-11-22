import QtQuick
import Drift

// Drags the guides of the set being edited. Lines and margin edges move; a line
// dragged off the canvas is removed; dragging from the top or left strip adds a
// horizontal or vertical line. Aspect frames are always centred, so they are
// edited in the popover only. Covers the whole viewport so a drag can leave the
// canvas; only the left button is taken, so middle-drag pan and wheel zoom still
// reach the viewport underneath. Esc puts a dragged guide back, Ctrl suspends snapping to the
// canvas centre and thirds, and every line has a grip at its middle for a finger.
ManipOverlay {
    id: root

    refreshPolicy: "none"
    focus: true
    hudEditable: drag !== null && !dragByTouch
    hudUnit: "%"

    property var previewCanvas

    readonly property string setId: EditorState.preview.guideEditSetId
    readonly property var editedSet: {
        const sets = EditorState.guideSets
        for (let i = 0; i < sets.length; ++i) {
            if (sets[i].id === setId)
                return sets[i]
        }
        return null
    }
    readonly property var items: editedSet ? editedSet.items : []

    readonly property real cx: previewCanvas ? previewCanvas.x : 0
    readonly property real cy: previewCanvas ? previewCanvas.y : 0
    readonly property real cw: previewCanvas ? Math.max(1, previewCanvas.width) : 1
    readonly property real ch: previewCanvas ? Math.max(1, previewCanvas.height) : 1

    readonly property real hitTolPx: 6
    readonly property real stripPx: 14
    // How far past the canvas edge a line has to go before releasing removes it.
    readonly property real removeMarginPx: 8

    // {id, key, axis, at, from, to}: the edge under the pointer or being dragged.
    property var hover: null
    property var drag: null
    property bool dragIsNew: false
    property bool dragRemoves: false
    property real dragValue: 0
    property bool dragByTouch: false
    // What the dragged property held at the press, for Esc.
    property real dragOrigin: 0
    // The fractions of the canvas a guide snaps to: its centre and thirds.
    readonly property var snapTargets: [1 / 3, 0.5, 2 / 3]

    function edgesOf(item) {
        if (item.kind === "v")
            return [{ id: item.id, key: "pos", axis: "x", at: cx + item.pos * cw, from: cy, to: cy + ch }]
        if (item.kind === "h")
            return [{ id: item.id, key: "pos", axis: "y", at: cy + item.pos * ch, from: cx, to: cx + cw }]
        if (item.kind !== "rect")
            return []
        const x0 = cx + item.left * cw
        const x1 = cx + (1 - item.right) * cw
        const y0 = cy + item.top * ch
        const y1 = cy + (1 - item.bottom) * ch
        return [
            { id: item.id, key: "left", axis: "x", at: x0, from: y0, to: y1 },
            { id: item.id, key: "right", axis: "x", at: x1, from: y0, to: y1 },
            { id: item.id, key: "top", axis: "y", at: y0, from: x0, to: x1 },
            { id: item.id, key: "bottom", axis: "y", at: y1, from: x0, to: x1 }
        ]
    }

    function hitTest(px, py) {
        let best = null
        let bestDist = hitTolPx
        for (const item of items) {
            if (item.locked)
                continue
            for (const t of edgesOf(item)) {
                const p = t.axis === "x" ? px : py
                const d = Math.abs(p - t.at)
                const along = t.axis === "x" ? py : px
                if (d < bestDist && along >= t.from - hitTolPx && along <= t.to + hitTolPx) {
                    bestDist = d
                    best = t
                }
            }
        }
        return best
    }

    function liveEdge(id, key) {
        for (const item of items) {
            if (item.id !== id)
                continue
            for (const t of edgesOf(item)) {
                if (t.key === key)
                    return t
            }
        }
        return null
    }

    function propertyValue(id, key) {
        for (const item of items) {
            if (item.id === id)
                return item[key]
        }
        return 0
    }

    // One grip per edge. The list only changes when guides come or go, so a drag that moves
    // an edge never rebuilds the grip being held.
    readonly property string gripSignature: {
        let out = ""
        for (const item of items) {
            if (!item.locked && (item.kind === "v" || item.kind === "h" || item.kind === "rect"))
                out += item.id + ":" + item.kind + ","
        }
        return out
    }
    property var gripModel: []
    onGripSignatureChanged: {
        const list = []
        for (const item of items) {
            if (item.locked)
                continue
            for (const t of edgesOf(item))
                list.push({ id: t.id, key: t.key })
        }
        gripModel = list
    }

    function inTopStrip(px, py) {
        return px >= cx && px <= cx + cw && py >= cy && py <= cy + stripPx
    }

    function inLeftStrip(px, py) {
        return px >= cx && px <= cx + stripPx && py >= cy && py <= cy + ch
    }

    function clampFor(key, v) {
        if (key === "pos")
            return Math.max(0, Math.min(1, v))
        return Math.max(0, Math.min(0.5, v))
    }

    // Pointer position to the dragged property's value, clamped as the controller clamps it, and
    // the snap target (a fraction of the canvas, or -1) it was pulled onto.
    function resolve(px, py, mods) {
        const size = drag.axis === "x" ? cw : ch
        let f = drag.axis === "x" ? (px - cx) / cw : (py - cy) / ch
        let target = -1
        const hit = Manip.snapValue(f, snapTargets, size, mods.noSnap)
        if (hit.snapped) {
            f = hit.value
            target = hit.target
        } else if (mods.constrain) {
            f = Math.round(f * 100) / 100
        }
        const edgeFraction = (drag.key === "right" || drag.key === "bottom") ? 1 - f : f
        return { value: clampFor(drag.key, edgeFraction), target: target }
    }

    function hudLabel() {
        return dragRemoves ? qsTr("Release to remove") : (dragValue * 100).toFixed(1) + "%"
    }

    function beginDrag(target, isNew, device) {
        drag = target
        dragIsNew = isNew
        dragRemoves = false
        dragByTouch = device !== DragSession.Mouse
        dragOrigin = isNew ? 0 : propertyValue(target.id, target.key)
        hover = null
        EditorState.drag.begin("guide", device, qsTr("Move guide"))
        root.forceActiveFocus()
    }

    function moveDrag(px, py, mods) {
        const r = resolve(px, py, mods)
        dragValue = r.value
        const at = drag.axis === "x" ? px : py
        const lo = drag.axis === "x" ? cx : cy
        const hi = drag.axis === "x" ? cx + cw : cy + ch
        dragRemoves = drag.key === "pos" && (at < lo - removeMarginPx || at > hi + removeMarginPx)
        if (r.target < 0 || dragRemoves) {
            snapLinesX = []
            snapLinesY = []
        } else if (drag.axis === "x") {
            snapLinesX = [cx + r.target * cw]
            snapLinesY = []
        } else {
            snapLinesX = []
            snapLinesY = [cy + r.target * ch]
        }
        pointer = Qt.point(px, py)
        EditorState.drag.update(hudLabel())
        EditorState.setGuideItemProperty(setId, drag.id, drag.key, dragValue)
    }

    function resetDrag() {
        drag = null
        dragIsNew = false
        dragRemoves = false
        snapLinesX = []
        snapLinesY = []
    }

    function endDrag(px, py) {
        // A click on a strip adds nothing: the new line only stays once dragged out of it.
        const clickedStrip = dragIsNew && (drag.axis === "y" ? inTopStrip(px, py) : inLeftStrip(px, py))
        if (dragRemoves || clickedStrip)
            EditorState.removeGuideItem(setId, drag.id)
        resetDrag()
        EditorState.drag.commit()
    }

    // Guides are not part of the project, so the session's own revert has nothing to restore.
    function revertDrag() {
        if (!drag)
            return
        if (dragIsNew)
            EditorState.removeGuideItem(setId, drag.id)
        else
            EditorState.setGuideItemProperty(setId, drag.id, drag.key, dragOrigin)
        resetDrag()
    }

    onHudApplied: (text, value) => {
        if (!drag)
            return
        dragValue = clampFor(drag.key, value / 100)
        dragRemoves = false
        EditorState.setGuideItemProperty(setId, drag.id, drag.key, dragValue)
        resetDrag()
        EditorState.drag.commit()
    }

    Connections {
        target: EditorState.drag
        function onCancelled() { root.revertDrag() }
    }

    // Where the highlighted edge sits: the live value while dragging, the hit position on hover.
    function edgeAt(t) {
        if (t !== drag)
            return t.at
        const size = t.axis === "x" ? cw : ch
        const origin = t.axis === "x" ? cx : cy
        const f = (t.key === "right" || t.key === "bottom") ? 1 - dragValue : dragValue
        return origin + f * size
    }

    readonly property var shown: drag || hover

    GuideEditAddStrip {
        x: root.cx
        y: root.cy
        width: root.cw
        height: root.stripPx
    }

    GuideEditAddStrip {
        x: root.cx
        y: root.cy + root.stripPx
        width: root.stripPx
        height: root.ch - root.stripPx
    }

    GuideEditHighlight {
        edge: root.shown
        at: root.shown ? root.edgeAt(root.shown) : 0
        removing: root.dragRemoves
    }

    Repeater {
        model: root.gripModel

        HandleKnob {
            id: grip
            required property var modelData
            readonly property var edge: root.liveEdge(modelData.id, modelData.key)
            shape: "square"
            z: 10
            visible: edge !== null
            cursor: edge && edge.axis === "x" ? Qt.SplitHCursor : Qt.SplitVCursor
            x: (edge ? (edge.axis === "x" ? edge.at : (edge.from + edge.to) / 2) : 0) - width / 2
            y: (edge ? (edge.axis === "y" ? edge.at : (edge.from + edge.to) / 2) : 0) - height / 2
            onPressed: (point, modifiers, device) => {
                if (edge) {
                    root.beginDrag(edge, false, device)
                    root.dragValue = root.resolve(point.x, point.y, root.modifiersFor(modifiers)).value
                }
            }
            onMoved: (point, modifiers) => {
                if (root.drag)
                    root.moveDrag(point.x, point.y, root.modifiersFor(modifiers))
            }
            onReleased: (point) => {
                if (root.drag)
                    root.endDrag(point.x, point.y)
            }
        }
    }

    MouseArea {
        id: mouseArea
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton
        hoverEnabled: true
        cursorShape: {
            const t = root.drag || root.hover
            if (t)
                return t.axis === "x" ? Qt.SplitHCursor : Qt.SplitVCursor
            if (root.inTopStrip(mouseX, mouseY))
                return Qt.SplitVCursor
            if (root.inLeftStrip(mouseX, mouseY))
                return Qt.SplitHCursor
            return Qt.ArrowCursor
        }

        onPressed: (mouse) => {
            const hit = root.hitTest(mouse.x, mouse.y)
            const device = mouse.source !== Qt.MouseEventNotSynthesized ? DragSession.Touch : DragSession.Mouse
            if (hit) {
                root.beginDrag(hit, false, device)
                root.dragValue = root.resolve(mouse.x, mouse.y, root.modifiersFor(mouse.modifiers)).value
                return
            }
            const top = root.inTopStrip(mouse.x, mouse.y)
            if (!top && !root.inLeftStrip(mouse.x, mouse.y))
                return
            const id = EditorState.addGuideItem(root.setId, top ? "h" : "v")
            root.beginDrag({ id: id, key: "pos", axis: top ? "y" : "x",
                             at: 0, from: top ? root.cx : root.cy,
                             to: top ? root.cx + root.cw : root.cy + root.ch }, true, device)
            root.moveDrag(mouse.x, mouse.y, root.modifiersFor(mouse.modifiers))
        }
        onPositionChanged: (mouse) => {
            if (root.drag)
                root.moveDrag(mouse.x, mouse.y, root.modifiersFor(mouse.modifiers))
            else
                root.hover = root.hitTest(mouse.x, mouse.y)
        }
        onReleased: (mouse) => {
            if (root.drag)
                root.endDrag(mouse.x, mouse.y)
        }
        onExited: if (!root.drag) root.hover = null
    }

    GuideEditBar {
        setName: root.editedSet ? root.editedSet.name : ""
        maxHintWidth: root.width * 0.6
        onDone: EditorState.preview.guideEditSetId = ""
    }
}
