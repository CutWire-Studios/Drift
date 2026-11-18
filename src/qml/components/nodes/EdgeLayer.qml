pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Shapes
import Drift

// Every edge as a cubic bezier, plus the wire being dragged out of a port. Endpoints bind to the
// node delegates' positions and port offsets, so moving a node re-routes its wires without any
// polling. Edges are hit-tested by sampling the curves (edgeAt), which keeps this layer out of
// the way of the nodes above it.
Item {
    id: root

    required property var canvas
    readonly property var controller: canvas.controller
    readonly property int count: repeater.count
    readonly property int samples: 24

    function portColor(type) {
        return Theme.c.portItem[type] ?? Theme.c.portItem.other
    }

    function _sample(e, t) {
        const u = 1 - t
        return Qt.point(u * u * u * e.p1x + 3 * u * u * t * e.c1x + 3 * u * t * t * e.c2x + t * t * t * e.p2x,
                        u * u * u * e.p1y + 3 * u * u * t * e.p1y + 3 * u * t * t * e.p2y + t * t * t * e.p2y)
    }

    function _segDist2(px, py, ax, ay, bx, by) {
        const dx = bx - ax, dy = by - ay
        const len2 = dx * dx + dy * dy
        let t = len2 > 0 ? ((px - ax) * dx + (py - ay) * dy) / len2 : 0
        t = Math.max(0, Math.min(1, t))
        const cx = ax + t * dx - px, cy = ay + t * dy - py
        return cx * cx + cy * cy
    }

    // Id of the edge nearest (x, y) in world coordinates within tol, or "".
    // qmllint disable missing-property
    function edgeAt(x, y, tol) {
        let best = ""
        let bestD = tol * tol
        for (let i = 0; i < repeater.count; ++i) {
            const e = repeater.itemAt(i)
            if (!e || !e.valid)
                continue
            const minX = Math.min(e.p1x, e.p2x, e.c1x, e.c2x) - tol
            const maxX = Math.max(e.p1x, e.p2x, e.c1x, e.c2x) + tol
            const minY = Math.min(e.p1y, e.p2y) - tol
            const maxY = Math.max(e.p1y, e.p2y) + tol
            if (x < minX || x > maxX || y < minY || y > maxY)
                continue
            let prev = _sample(e, 0)
            for (let s = 1; s <= samples; ++s) {
                const cur = _sample(e, s / samples)
                const d = _segDist2(x, y, prev.x, prev.y, cur.x, cur.y)
                if (d < bestD) {
                    bestD = d
                    best = e.edgeId
                }
                prev = cur
            }
        }
        return best
    }
    // qmllint enable missing-property

    Repeater {
        id: repeater
        model: root.controller ? root.controller.edges : null

        delegate: Shape {
            id: edge

            required property var model

            readonly property string edgeId: model.edgeId
            readonly property var fromItem: {
                void root.canvas.nodesRevision
                return root.canvas.nodeItem(model.fromNode)
            }
            readonly property var toItem: {
                void root.canvas.nodesRevision
                return root.canvas.nodeItem(model.toNode)
            }
            readonly property bool valid: fromItem !== null && toItem !== null
            readonly property real fromY: valid ? (fromItem.portY["o:" + model.fromPort] ?? fromItem.height / 2) : 0
            readonly property real toY: valid ? (toItem.portY["i:" + model.toPort] ?? toItem.height / 2) : 0

            readonly property real p1x: valid ? fromItem.x + fromItem.width : 0
            readonly property real p1y: valid ? fromItem.y + fromY : 0
            readonly property real p2x: valid ? toItem.x : 0
            readonly property real p2y: valid ? toItem.y + toY : 0
            readonly property real bend: Math.max(Math.abs(p2x - p1x) * 0.4, 40)
            readonly property real c1x: p1x + bend
            readonly property real c2x: p2x - bend

            readonly property bool picked: root.canvas.selectedEdge === edgeId
            readonly property bool hot: root.canvas.hoverEdge === edgeId

            visible: valid
            z: picked ? 2 : 0
            preferredRendererType: Shape.CurveRenderer

            ShapePath {
                strokeColor: edge.picked ? Qt.alpha(Theme.c.edgeLayer.selected, 0.25) : "transparent"
                strokeWidth: 8
                fillColor: "transparent"
                capStyle: ShapePath.RoundCap
                startX: edge.p1x
                startY: edge.p1y
                PathCubic {
                    x: edge.p2x
                    y: edge.p2y
                    control1X: edge.c1x
                    control1Y: edge.p1y
                    control2X: edge.c2x
                    control2Y: edge.p2y
                }
            }

            ShapePath {
                strokeColor: edge.picked ? Theme.c.edgeLayer.selected : root.portColor(edge.model.type)
                strokeWidth: edge.picked ? 3.5 : (edge.hot ? 3 : 2)
                fillColor: "transparent"
                capStyle: ShapePath.RoundCap
                startX: edge.p1x
                startY: edge.p1y
                PathCubic {
                    x: edge.p2x
                    y: edge.p2y
                    control1X: edge.c1x
                    control1Y: edge.p1y
                    control2X: edge.c2x
                    control2Y: edge.p2y
                }
            }
        }
    }

    Shape {
        id: pending
        visible: root.canvas.pending !== null
        z: 10
        preferredRendererType: Shape.CurveRenderer

        readonly property real dir: root.canvas.pending && root.canvas.pending.isOutput ? 1 : -1
        readonly property real sx: root.canvas.pendingStart.x
        readonly property real sy: root.canvas.pendingStart.y
        readonly property real ex: root.canvas.cursorWorld.x
        readonly property real ey: root.canvas.cursorWorld.y
        readonly property real bend: Math.max(Math.abs(ex - sx) * 0.4, 40)

        ShapePath {
            strokeColor: root.canvas.pending ? root.portColor(root.canvas.pending.type) : "transparent"
            strokeWidth: 2
            strokeStyle: ShapePath.DashLine
            dashPattern: [4, 3]
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap
            startX: pending.sx
            startY: pending.sy
            PathCubic {
                x: pending.ex
                y: pending.ey
                control1X: pending.sx + pending.dir * pending.bend
                control1Y: pending.sy
                control2X: pending.ex - pending.dir * pending.bend
                control2Y: pending.ey
            }
        }
    }
}
