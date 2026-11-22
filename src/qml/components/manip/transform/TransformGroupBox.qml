pragma ComponentBehavior: Bound
import QtQuick
import Drift

// The one frame around several selected clips. Dragging it moves, scales or turns every member
// together; each drag is solved from the boxes as they stood at the grab. Works the same with a
// finger: the grips and the stalk are HandleKnobs, and the body takes touch and mouse alike.
Item {
    id: group

    required property var manip
    required property var boxes

    readonly property Item stage: manip.stage
    readonly property real sx: stage.width / Math.max(1, boxes.length > 0 ? boxes[0].canvasWidth : 1)
    readonly property real sy: stage.height / Math.max(1, boxes.length > 0 ? boxes[0].canvasHeight : 1)

    // Bounds of every member's projected quad, in canvas px.
    readonly property rect bounds: {
        let l = Infinity, t = Infinity, r = -Infinity, b = -Infinity
        for (const box of group.boxes) {
            const q = box.quad || []
            for (const p of q) {
                l = Math.min(l, p.x); r = Math.max(r, p.x)
                t = Math.min(t, p.y); b = Math.max(b, p.y)
            }
        }
        return isFinite(l) ? Qt.rect(l, t, r - l, b - t) : Qt.rect(0, 0, 0, 0)
    }

    // Live canvas-px rect and turn while a drag runs; the model is not rebuilt meanwhile.
    property var liveRect: null
    property real liveRotation: 0
    // What the pointer is doing: {kind, device, ...values taken at the grab}, or null.
    property var g: null
    readonly property bool grabbing: g !== null && g.kind !== "move"

    readonly property rect shown: liveRect ? liveRect : bounds
    x: shown.x * sx
    y: shown.y * sy
    width: Math.max(1, shown.width * sx)
    height: Math.max(1, shown.height * sy)
    transformOrigin: Item.Center
    rotation: liveRotation

    function canvasPoint(p) {
        return Qt.point(p.x / group.sx, p.y / group.sy)
    }

    function stagePoint(scenePos) {
        return stage.mapFromItem(null, scenePos.x, scenePos.y)
    }

    function clearGesture() {
        g = null
        liveRect = null
        liveRotation = 0
    }

    function begin(kind, device, text, p) {
        manip.beginGesture(kind, device, text, null)
        manip.pointerAt(p)
        g = { "kind": kind, "device": device, "boxes": boxes, "press": canvasPoint(p), "rect": bounds }
    }

    function showHud(text, p) {
        manip.pointerAt(p)
        EditorState.drag.update(text)
    }

    function moveTo(p, mods) {
        if (!g || g.kind !== "move")
            return
        const c = canvasPoint(p)
        let dx = c.x - g.press.x
        let dy = c.y - g.press.y
        if (Manip.fromQtFlags(mods).constrain) {
            if (Math.abs(dx) >= Math.abs(dy))
                dy = 0
            else
                dx = 0
        }
        liveRect = Qt.rect(g.rect.x + dx, g.rect.y + dy, g.rect.width, g.rect.height)
        EditorState.preview.applyGroupTransform(g.boxes, 0, 0, 1, 1, 0, dx, dy)
        showHud(qsTr("Δ %1, %2").arg(Math.round(dx)).arg(Math.round(dy)), p)
    }

    function resizeTo(grip, p, mods) {
        if (!g || g.kind !== "resize")
            return
        const m = manip.modifiersFor(mods)
        const r = g.rect
        const k = m.fromCenter ? 2 : 1
        const c = canvasPoint(p)
        const ddx = k * (c.x - g.press.x)
        const ddy = k * (c.y - g.press.y)
        let fx = grip.dx === 0 ? 1 : Math.max(1, r.width + ddx * grip.dx) / Math.max(1, r.width)
        let fy = grip.dy === 0 ? 1 : Math.max(1, r.height + ddy * grip.dy) / Math.max(1, r.height)
        // A mixed selection has no ratio of its own to protect, so corners are free and Shift holds
        // the ratio; on a finger the Lock ratio chip does.
        if (grip.dx !== 0 && grip.dy !== 0) {
            const locked = g.device !== DragSession.Mouse && manip.touchBarItem
                           ? manip.touchBarItem.lockRatio : Manip.fromQtFlags(mods).constrain
            if (locked) {
                const s = Math.abs(fx - 1) >= Math.abs(fy - 1) ? fx : fy
                fx = s
                fy = s
            }
        }
        const cx = r.x + r.width / 2
        const cy = r.y + r.height / 2
        let ax = grip.dx < 0 ? r.x + r.width : (grip.dx > 0 ? r.x : cx)
        let ay = grip.dy < 0 ? r.y + r.height : (grip.dy > 0 ? r.y : cy)
        if (m.fromCenter) {
            ax = cx
            ay = cy
        }
        liveRect = Qt.rect(ax + (r.x - ax) * fx, ay + (r.y - ay) * fy, r.width * fx, r.height * fy)
        EditorState.preview.applyGroupTransform(g.boxes, ax, ay, fx, fy, 0, 0, 0)
        showHud(Math.round(r.width * fx) + "×" + Math.round(r.height * fy), p)
    }

    function pointerAngle(p) {
        const c = canvasPoint(p)
        return Math.atan2((c.y - g.pivot.y) * group.sy, (c.x - g.pivot.x) * group.sx) * 180 / Math.PI
    }

    function rotateBegin(p, device) {
        begin("rotate", device, qsTr("Rotate clips"), p)
        const b = group.bounds
        g.pivot = Qt.point(b.x + b.width / 2, b.y + b.height / 2)
        g.start = pointerAngle(p)
        g.detent = null
        liveRect = b
    }

    function rotateTo(p, mods) {
        if (!g || g.kind !== "rotate")
            return
        let deg = normalizeDeg(pointerAngle(p) - g.start)
        if (Manip.fromQtFlags(mods).constrain) {
            deg = normalizeDeg(Manip.constrainAngle(deg, 15))
        } else if (g.device !== DragSession.Mouse && !manip.modifiersFor(mods).noSnap) {
            const nearest = normalizeDeg(Manip.constrainAngle(deg, 15))
            if (Math.abs(normalizeDeg(nearest - deg)) <= 4) {
                deg = nearest
                if (g.detent !== nearest)
                    Haptics.detent()
                g.detent = nearest
            } else {
                g.detent = null
            }
        }
        liveRotation = deg
        EditorState.preview.applyGroupTransform(g.boxes, g.pivot.x, g.pivot.y, 1, 1, deg, 0, 0)
        showHud(Math.round(deg) + "°", p)
    }

    function normalizeDeg(deg) {
        let d = deg % 360
        if (d > 180)
            d -= 360
        if (d <= -180)
            d += 360
        return d
    }

    Rectangle {
        anchors.fill: parent
        color: "transparent"
        border.width: Theme.c.transformOverlayBoxBorder.borderWidthSelected
        border.color: Theme.c.transformOverlayBoxBorder.borderSelected
        radius: Theme.c.transformOverlayBoxBorder.radius
    }

    DragHandler {
        target: null
        acceptedDevices: PointerDevice.Mouse | PointerDevice.Stylus | PointerDevice.TouchPad
        enabled: !group.grabbing
        cursorShape: Qt.SizeAllCursor
        onActiveChanged: {
            if (active)
                group.begin("move", DragSession.Mouse, qsTr("Move clips"), group.stagePoint(centroid.scenePosition))
            else if (group.g && group.g.kind === "move")
                group.manip.endGesture()
        }
        onCentroidChanged: {
            if (active)
                group.moveTo(group.stagePoint(centroid.scenePosition), centroid.modifiers)
        }
    }

    DragHandler {
        target: null
        acceptedDevices: PointerDevice.TouchScreen
        maximumPointCount: 1
        enabled: !group.grabbing
        onActiveChanged: {
            if (active)
                group.begin("move", DragSession.Touch, qsTr("Move clips"), group.stagePoint(centroid.scenePosition))
            else if (group.g && group.g.kind === "move")
                group.manip.endGesture()
        }
        onCentroidChanged: {
            if (active)
                group.moveTo(group.stagePoint(centroid.scenePosition), centroid.modifiers)
        }
    }

    // 4 edges then 4 corners, as on a single clip: the opposite edge or corner is the anchor.
    Repeater {
        model: [
            { dx: -1, dy:  0 },
            { dx:  1, dy:  0 },
            { dx:  0, dy: -1 },
            { dx:  0, dy:  1 },
            { dx: -1, dy: -1 },
            { dx:  1, dy:  1 },
            { dx:  1, dy: -1 },
            { dx: -1, dy:  1 }
        ]

        delegate: HandleKnob {
            id: grip
            required property var modelData
            readonly property int dx: modelData.dx
            readonly property int dy: modelData.dy
            readonly property bool isCorner: dx !== 0 && dy !== 0

            shape: isCorner ? "square" : "dot"
            space: group.stage
            visualSize: group.manip.touch ? 14 : 8
            cursor: "resize:" + ((isCorner ? (dx === dy ? 45 : 135) : (dx !== 0 ? 0 : 90)) + group.liveRotation)
            x: (dx === 0 ? group.width / 2 : (dx < 0 ? 0 : group.width)) - width / 2
            y: (dy === 0 ? group.height / 2 : (dy < 0 ? 0 : group.height)) - height / 2

            onPressed: (point, modifiers, device) => group.begin("resize", device, qsTr("Resize clips"), point)
            onMoved: (point, modifiers) => group.resizeTo(grip, point, modifiers)
            onReleased: if (group.g && group.g.kind === "resize") group.manip.endGesture()
        }
    }

    Rectangle {
        x: group.width / 2 - width / 2
        y: -stalk
        width: 1
        height: stalk
        color: Theme.c.manipHandle.border
        readonly property real stalk: group.manip.touch ? 44 : 28
    }

    HandleKnob {
        shape: "dot"
        space: group.stage
        visualSize: group.manip.touch ? 16 : 9
        cursor: "rotate:" + (-90 + group.liveRotation)
        x: group.width / 2 - width / 2
        y: -(group.manip.touch ? 44 : 28) - height / 2

        onPressed: (point, modifiers, device) => group.rotateBegin(point, device)
        onMoved: (point, modifiers) => group.rotateTo(point, modifiers)
        onReleased: if (group.g && group.g.kind === "rotate") group.manip.endGesture()
    }
}
