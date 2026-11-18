pragma ComponentBehavior: Bound
import QtQuick
import Drift

// Pannable, zoomable surface holding the graph. Everything inside `world` is in graph
// coordinates; the grid, rubber band and overlays are in screen coordinates.
//
// Mouse: left-drag on empty space rubber-bands, middle-drag or Space+drag pans, the wheel zooms
// about the cursor (trackpad scroll pans, Ctrl+wheel zooms), right-click opens the menu, double
// click or Space opens quick-add, and dragging a wire onto empty space opens quick-add filtered
// by the wire's type. Touch: one finger on empty space pans, pinch zooms, long-press opens
// quick-add (on a node, its menu), and tapping a port then another port connects them. Each
// gesture is bound to a device by its handler, so a touchscreen laptop gets both sets.
FocusScope {
    id: root

    focus: true
    clip: true

    property var controller: null

    property real zoom: 1.0
    property real panX: 0
    property real panY: 0
    readonly property real minZoom: 0.2
    readonly property real maxZoom: 3.0
    property bool userMoved: false

    property alias world: worldItem
    property alias edgeLayer: edges
    readonly property int nodeCount: nodeRepeater.count
    property rect contentRect: Qt.rect(0, 0, 0, 0)
    readonly property rect viewRect: Qt.rect(-panX / zoom, -panY / zoom, width / zoom, height / zoom)

    property string selectedEdge: ""
    property string hoverEdge: ""
    property int nodesRevision: 0

    // The wire being made: {node, port, isOutput, type}, or null.
    property var pending: null
    property int pendingRevision: 0
    property point pendingStart: Qt.point(0, 0)
    property point cursorWorld: Qt.point(0, 0)
    property string hoverPortKey: ""
    property point hoverPos: Qt.point(NaN, NaN)
    property bool spaceDown: false
    property bool spacePanned: false

    signal nodeDoubleClicked(string id)
    signal nodeMenuRequested(string id, point screenPos)
    signal contextMenuRequested(point screenPos, point worldPos, string edgeId)
    // pendingWire is the wire dropped on empty space, or null for a plain add.
    signal quickAddRequested(point screenPos, point worldPos, var pendingWire)

    function clampZoom(z) {
        return Math.max(minZoom, Math.min(maxZoom, z))
    }

    function toWorld(sx, sy) {
        return Qt.point((sx - panX) / zoom, (sy - panY) / zoom)
    }

    function toScreen(wx, wy) {
        return Qt.point(wx * zoom + panX, wy * zoom + panY)
    }

    function zoomAt(sx, sy, target) {
        const z = clampZoom(target)
        if (z === zoom)
            return
        userMoved = true
        const wx = (sx - panX) / zoom
        const wy = (sy - panY) / zoom
        zoom = z
        panX = sx - wx * z
        panY = sy - wy * z
    }

    function zoomCentered(factor) {
        zoomAt(width / 2, height / 2, zoom * factor)
    }

    function resetZoom() {
        zoomAt(width / 2, height / 2, 1.0)
    }

    function frameRect(r) {
        if (width <= 0 || height <= 0 || r.width <= 0 || r.height <= 0)
            return
        const margin = 48
        const z = clampZoom(Math.min((width - 2 * margin) / r.width, (height - 2 * margin) / r.height, 1.0))
        zoom = z
        panX = width / 2 - (r.x + r.width / 2) * z
        panY = height / 2 - (r.y + r.height / 2) * z
    }

    function recomputeBounds() {
        let x0 = Infinity, y0 = Infinity, x1 = -Infinity, y1 = -Infinity
        for (let i = 0; i < nodeRepeater.count; ++i) {
            const it = nodeRepeater.itemAt(i)
            if (!it)
                continue
            x0 = Math.min(x0, it.x)
            y0 = Math.min(y0, it.y)
            x1 = Math.max(x1, it.x + it.width)
            y1 = Math.max(y1, it.y + it.height)
        }
        contentRect = x0 === Infinity ? Qt.rect(0, 0, 0, 0) : Qt.rect(x0, y0, x1 - x0, y1 - y0)
    }

    function noteNodesChanged() {
        Qt.callLater(root._bumpNodes)
    }

    function _bumpNodes() {
        ++nodesRevision
        recomputeBounds()
    }

    function fitAll() {
        recomputeBounds()
        frameRect(contentRect)
    }

    function frameSelection() {
        const sel = controller ? controller.selection : []
        if (sel.length === 0) {
            fitAll()
            return
        }
        let x0 = Infinity, y0 = Infinity, x1 = -Infinity, y1 = -Infinity
        for (const id of sel) {
            const it = nodeItem(id)
            if (!it)
                continue
            x0 = Math.min(x0, it.x)
            y0 = Math.min(y0, it.y)
            x1 = Math.max(x1, it.x + it.width)
            y1 = Math.max(y1, it.y + it.height)
        }
        if (x0 !== Infinity)
            frameRect(Qt.rect(x0, y0, x1 - x0, y1 - y0))
    }

    function centerOn(wx, wy) {
        userMoved = true
        panX = width / 2 - wx * zoom
        panY = height / 2 - wy * zoom
    }

    onNodeCountChanged: {
        if (nodeCount <= 1)
            userMoved = false
        if (!userMoved && nodeCount > 1)
            Qt.callLater(root.fitAll)
    }
    onWidthChanged: if (!userMoved && nodeCount > 1) Qt.callLater(root.fitAll)
    onHeightChanged: if (!userMoved && nodeCount > 1) Qt.callLater(root.fitAll)

    function nodeItem(id) {
        if (!controller)
            return null
        const row = controller.nodes.rowOfNode(id)
        return row < 0 ? null : nodeRepeater.itemAt(row)
    }

    // The node under a screen point, with a margin so a port hanging off a node's edge still
    // counts as the node. Keeps the background gestures off nodes and ports.
    function nodeNear(sx, sy) {
        const w = toWorld(sx, sy)
        const slack = (Theme.touchInput ? 18 : 10) / zoom
        for (let i = 0; i < nodeRepeater.count; ++i) {
            const it = nodeRepeater.itemAt(i)
            if (it && w.x >= it.x - slack && w.x <= it.x + it.width + slack && w.y >= it.y && w.y <= it.y + it.height)
                return it
        }
        return null
    }

    function deleteSelection() {
        if (!controller)
            return
        if (selectedEdge !== "") {
            const e = controller.edges.get(controller.edges.rowOfEdge(selectedEdge))
            selectedEdge = ""
            if (e.toNode !== undefined)
                controller.disconnectInput(e.toNode, e.toPort)
            return
        }
        controller.deleteSelection()
    }

    function clearEdgeSelection() {
        selectedEdge = ""
    }

    // ── Connections ────────────────────────────────────────────────────────

    function portCenter(nodeId, isOutput, portId) {
        const it = nodeItem(nodeId)
        if (!it)
            return Qt.point(0, 0)
        return Qt.point(it.x + (isOutput ? it.width : 0),
                        it.y + (it.portY[(isOutput ? "o:" : "i:") + portId] ?? it.height / 2))
    }

    function portAt(wp) {
        const reach = (Theme.touchInput ? 18 : 10) / Math.max(zoom, 0.5)
        for (let i = 0; i < nodeRepeater.count; ++i) {
            const it = nodeRepeater.itemAt(i)
            if (!it || wp.x < it.x - reach || wp.x > it.x + it.width + reach
                    || wp.y < it.y - reach || wp.y > it.y + it.height + reach)
                continue
            for (let o = 0; o < 2; ++o) {
                const isOutput = o === 1
                for (const p of isOutput ? it.outputs : it.inputs) {
                    if (p.hidePort)
                        continue
                    const c = portCenter(it.nodeId, isOutput, p.id)
                    if (Math.hypot(wp.x - c.x, wp.y - c.y) <= reach)
                        return { node: it.nodeId, isOutput: isOutput, port: p.id, type: p.type }
                }
            }
        }
        return null
    }

    function _keyOf(hit) {
        return hit ? hit.node + (hit.isOutput ? ":o:" : ":i:") + hit.port : ""
    }

    function _connectArgs(a, b) {
        return a.isOutput ? [a.node, a.port, b.node, b.port] : [b.node, b.port, a.node, a.port]
    }

    function canFinishAt(nodeId, isOutput, portId) {
        if (!pending || pending.isOutput === isOutput || pending.node === nodeId)
            return false
        const args = _connectArgs(pending, { node: nodeId, isOutput: isOutput, port: portId })
        return controller.canConnect(args[0], args[1], args[2], args[3])
    }

    function beginPending(nodeId, isOutput, portId, type, wp) {
        pending = { node: nodeId, isOutput: isOutput, port: portId, type: type }
        pendingStart = portCenter(nodeId, isOutput, portId)
        cursorWorld = wp
        ++pendingRevision
    }

    function cancelPending() {
        if (pending === null)
            return
        pending = null
        hoverPortKey = ""
        ++pendingRevision
    }

    function _finishAt(hit) {
        if (!canFinishAt(hit.node, hit.isOutput, hit.port))
            return false
        const args = _connectArgs(pending, hit)
        controller.connectPorts(args[0], args[1], args[2], args[3])
        cancelPending()
        return true
    }

    function portDragBegan(nodeId, isOutput, portId, type, wp) {
        cancelPending()
        beginPending(nodeId, isOutput, portId, type, wp)
    }

    function portDragMoved(wp) {
        if (!pending)
            return
        cursorWorld = wp
        const hit = portAt(wp)
        hoverPortKey = hit && canFinishAt(hit.node, hit.isOutput, hit.port) ? _keyOf(hit) : ""
    }

    function portDragEnded(wp) {
        if (!pending)
            return
        const hit = portAt(wp)
        if (hit && _finishAt(hit))
            return
        if (hit) {
            cancelPending()
            return
        }
        hoverPortKey = ""
        const s = toScreen(wp.x, wp.y)
        quickAddRequested(s, wp, pending)
    }

    function portTapped(nodeId, isOutput, portId, type) {
        if (pending) {
            const same = pending.node === nodeId && pending.isOutput === isOutput && pending.port === portId
            if (!same && _finishAt({ node: nodeId, isOutput: isOutput, port: portId }))
                return
            cancelPending()
            if (same)
                return
        }
        const c = portCenter(nodeId, isOutput, portId)
        beginPending(nodeId, isOutput, portId, type, Qt.point(c.x + (isOutput ? 24 : -24), c.y))
    }

    Connections {
        target: root.controller ? root.controller.nodes : null
        function onRowsRemoved() {
            if (root.pending && !root.nodeItem(root.pending.node))
                root.cancelPending()
        }
    }

    // ── Keyboard ───────────────────────────────────────────────────────────

    function openQuickAtHover() {
        const at = isNaN(hoverPos.x) ? Qt.point(width / 2, height / 2) : hoverPos
        quickAddRequested(at, toWorld(at.x, at.y), null)
    }

    // The app-wide Ctrl+Z / Delete / Space shortcuts fire before key events reach an item; claiming
    // them here is what keeps a graph's undo and delete off the project while the canvas has focus.
    Keys.onShortcutOverride: (event) => {
        if (!controller)
            return
        const ctrl = (event.modifiers & Qt.ControlModifier) !== 0
        const ctrlKeys = [Qt.Key_Z, Qt.Key_Y, Qt.Key_A, Qt.Key_C, Qt.Key_V, Qt.Key_D, Qt.Key_0, Qt.Key_Plus,
                          Qt.Key_Equal, Qt.Key_Minus]
        const plainKeys = [Qt.Key_Delete, Qt.Key_Backspace, Qt.Key_Space, Qt.Key_F, Qt.Key_Escape]
        if (ctrl ? ctrlKeys.includes(event.key) : plainKeys.includes(event.key))
            event.accepted = true
    }

    Keys.onPressed: (event) => {
        if (!controller)
            return
        const ctrl = (event.modifiers & Qt.ControlModifier) !== 0
        const shift = (event.modifiers & Qt.ShiftModifier) !== 0
        if (event.key === Qt.Key_Space && !event.isAutoRepeat) {
            spaceDown = true
            spacePanned = false
            event.accepted = true
        } else if (event.key === Qt.Key_Delete || event.key === Qt.Key_Backspace) {
            deleteSelection()
            event.accepted = true
        } else if (ctrl && event.key === Qt.Key_Z) {
            if (shift)
                controller.redo()
            else
                controller.undo()
            event.accepted = true
        } else if (ctrl && event.key === Qt.Key_Y) {
            controller.redo()
            event.accepted = true
        } else if (ctrl && event.key === Qt.Key_A) {
            controller.selectAll()
            event.accepted = true
        } else if (event.key === Qt.Key_Escape) {
            cancelPending()
            selectedEdge = ""
            event.accepted = true
        } else if (ctrl && event.key === Qt.Key_C) {
            controller.copySelection()
            event.accepted = true
        } else if (ctrl && event.key === Qt.Key_V) {
            const at = isNaN(hoverPos.x) ? Qt.point(width / 2, height / 2) : hoverPos
            controller.paste(toWorld(at.x, at.y))
            event.accepted = true
        } else if (ctrl && event.key === Qt.Key_D) {
            controller.duplicateSelection()
            event.accepted = true
        } else if (!ctrl && event.key === Qt.Key_F) {
            frameSelection()
            event.accepted = true
        } else if (ctrl && event.key === Qt.Key_0) {
            fitAll()
            event.accepted = true
        } else if (ctrl && (event.key === Qt.Key_Plus || event.key === Qt.Key_Equal)) {
            zoomCentered(1.12)
            event.accepted = true
        } else if (ctrl && event.key === Qt.Key_Minus) {
            zoomCentered(1 / 1.12)
            event.accepted = true
        }
    }

    Keys.onReleased: (event) => {
        if (event.key === Qt.Key_Space && !event.isAutoRepeat) {
            const tapped = spaceDown && !spacePanned
            spaceDown = false
            event.accepted = true
            if (tapped && !pending)
                openQuickAtHover()
        }
    }

    onActiveFocusChanged: if (!activeFocus) spaceDown = false

    // ── Pointer: view ──────────────────────────────────────────────────────

    WheelHandler {
        id: wheelHandler
        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
        onWheel: (event) => {
            const p = wheelHandler.point.position
            const ctrl = (event.modifiers & Qt.ControlModifier) !== 0
            if (!ctrl && (event.pixelDelta.x !== 0 || event.pixelDelta.y !== 0)) {
                root.userMoved = true
                root.panX += event.pixelDelta.x
                root.panY += event.pixelDelta.y
                return
            }
            const dy = event.angleDelta.y !== 0 ? event.angleDelta.y : event.pixelDelta.y
            root.zoomAt(p.x, p.y, root.zoom * Math.pow(1.12, dy / 120))
        }
    }

    PinchHandler {
        id: pinch
        target: null
        acceptedDevices: PointerDevice.TouchScreen | PointerDevice.TouchPad
        property real startZoom: 1
        property point lastCentroid: Qt.point(0, 0)
        onActiveChanged: if (active) {
            startZoom = root.zoom
            lastCentroid = centroid.position
        }
        onCentroidChanged: if (active) {
            root.userMoved = true
            root.panX += centroid.position.x - lastCentroid.x
            root.panY += centroid.position.y - lastCentroid.y
            lastCentroid = centroid.position
        }
        onActiveScaleChanged: if (active)
            root.zoomAt(centroid.position.x, centroid.position.y, startZoom * activeScale)
    }

    HoverHandler {
        onHoveredChanged: if (!hovered) root.hoverPos = Qt.point(NaN, NaN)
        onPointChanged: {
            root.hoverPos = point.position
            if (root.pending) {
                root.cursorWorld = root.toWorld(point.position.x, point.position.y)
                const hit = root.portAt(root.cursorWorld)
                root.hoverPortKey = hit && root.canFinishAt(hit.node, hit.isOutput, hit.port) ? root._keyOf(hit) : ""
            }
        }
    }

    // ── Scene ──────────────────────────────────────────────────────────────

    Rectangle {
        anchors.fill: parent
        color: Theme.c.nodeCanvas.background
    }

    NodeGridItem {
        anchors.fill: parent
        zoom: root.zoom
        panX: root.panX
        panY: root.panY
        minorColor: Theme.c.nodeCanvas.gridMinor
        majorColor: Theme.c.nodeCanvas.gridMajor
    }

    // Everything that happens on empty canvas. The mask drops nodes and their port margins, so
    // these handlers never compete with a node's own.
    Item {
        id: background
        anchors.fill: parent
        enabled: root.controller !== null
        containmentMask: QtObject {
            function contains(p: point): bool {
                return root.nodeNear(p.x, p.y) === null
            }
        }

        function edgeUnder(p, tol) {
            const w = root.toWorld(p.x, p.y)
            return root.edgeLayer.edgeAt(w.x, w.y, tol / root.zoom)
        }

        property var baseSelection: []

        TapHandler {
            id: bgTap
            acceptedButtons: Qt.LeftButton
            onPressedChanged: if (pressed) root.forceActiveFocus()
            onTapped: (point, button) => {
                const touch = point.device.type === PointerDevice.TouchScreen
                const additive = (bgTap.point.modifiers & (Qt.ShiftModifier | Qt.ControlModifier)) !== 0
                if (root.pending) {
                    root.cancelPending()
                    return
                }
                const e = background.edgeUnder(point.position, touch ? 14 : 7)
                root.selectedEdge = e
                if (e !== "" || !additive)
                    root.controller.clearSelection()
            }
            onDoubleTapped: (point, button) => {
                if (point.device.type === PointerDevice.TouchScreen || root.pending)
                    return
                const p = point.position
                if (background.edgeUnder(p, 7) === "")
                    root.quickAddRequested(p, root.toWorld(p.x, p.y), null)
            }
            onLongPressed: {
                if (bgTap.point.device.type !== PointerDevice.TouchScreen || root.pending)
                    return
                const p = bgTap.point.position
                root.quickAddRequested(p, root.toWorld(p.x, p.y), null)
            }
        }

        TapHandler {
            acceptedButtons: Qt.RightButton
            acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
            onPressedChanged: if (pressed) root.forceActiveFocus()
            onTapped: (point) => {
                if (root.pending) {
                    root.cancelPending()
                    return
                }
                const e = background.edgeUnder(point.position, 7)
                root.selectedEdge = e
                root.contextMenuRequested(point.position, root.toWorld(point.position.x, point.position.y), e)
            }
        }

        DragHandler {
            id: band
            target: null
            acceptedButtons: Qt.LeftButton
            acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
            onActiveChanged: {
                if (active) {
                    root.forceActiveFocus()
                    const additive = (centroid.modifiers & (Qt.ShiftModifier | Qt.ControlModifier)) !== 0
                    background.baseSelection = additive ? root.controller.selection : []
                    if (!additive)
                        root.controller.clearSelection()
                    root.selectedEdge = ""
                }
            }
            onCentroidChanged: {
                if (!active)
                    return
                const a = root.toWorld(Math.min(centroid.pressPosition.x, centroid.position.x),
                                       Math.min(centroid.pressPosition.y, centroid.position.y))
                const b = root.toWorld(Math.max(centroid.pressPosition.x, centroid.position.x),
                                       Math.max(centroid.pressPosition.y, centroid.position.y))
                const hits = root.controller.nodesInRect(Qt.rect(a.x, a.y, b.x - a.x, b.y - a.y))
                const ids = background.baseSelection.slice()
                for (const id of hits)
                    if (ids.indexOf(id) < 0)
                        ids.push(id)
                root.controller.setSelection(ids)
            }
        }

        DragHandler {
            id: touchPan
            target: null
            acceptedDevices: PointerDevice.TouchScreen
            maximumPointCount: 1
            property real startX: 0
            property real startY: 0
            onActiveChanged: if (active) {
                startX = root.panX
                startY = root.panY
                root.userMoved = true
                root.forceActiveFocus()
            }
            onTranslationChanged: if (active) {
                root.panX = startX + translation.x
                root.panY = startY + translation.y
            }
        }

        DragHandler {
            id: middlePan
            target: null
            acceptedButtons: Qt.MiddleButton
            acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
            property real startX: 0
            property real startY: 0
            onActiveChanged: if (active) {
                startX = root.panX
                startY = root.panY
                root.userMoved = true
            }
            onTranslationChanged: if (active) {
                root.panX = startX + translation.x
                root.panY = startY + translation.y
            }
        }
    }

    Item {
        id: worldItem
        x: root.panX
        y: root.panY
        scale: root.zoom
        transformOrigin: Item.TopLeft

        EdgeLayer {
            id: edges
            canvas: root
        }

        Repeater {
            id: nodeRepeater
            model: root.controller ? root.controller.nodes : null
            delegate: NodeDelegate {
                canvas: root
            }
        }
    }

    Rectangle {
        id: bandRect
        visible: band.active
        x: Math.min(band.centroid.pressPosition.x, band.centroid.position.x)
        y: Math.min(band.centroid.pressPosition.y, band.centroid.position.y)
        width: Math.abs(band.centroid.position.x - band.centroid.pressPosition.x)
        height: Math.abs(band.centroid.position.y - band.centroid.pressPosition.y)
        color: Qt.alpha(Theme.c.nodeCanvas.bandBorder, 0.12)
        border.width: 1
        border.color: Theme.c.nodeCanvas.bandBorder
    }

    MouseArea {
        id: spacePan
        anchors.fill: parent
        enabled: root.spaceDown
        visible: root.spaceDown
        z: 100
        cursorShape: pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor
        property real startX: 0
        property real startY: 0
        property point pressPoint: Qt.point(0, 0)
        onPressed: (m) => {
            startX = root.panX
            startY = root.panY
            pressPoint = Qt.point(m.x, m.y)
            root.spacePanned = true
            root.userMoved = true
        }
        onPositionChanged: (m) => {
            if (!pressed)
                return
            root.panX = startX + m.x - pressPoint.x
            root.panY = startY + m.y - pressPoint.y
        }
    }
}
