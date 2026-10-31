import QtQuick
import Drift

// The one frame around several selected clips. Dragging it moves, scales or turns every member
// together; each drag is solved from the boxes as they stood at the grab.
Item {
    id: group

    // The TransformOverlay that owns snap and interaction, and the selected clips' boxes.
    required property var overlay
    required property var boxes

    readonly property real sx: overlay.width / Math.max(1, boxes.length > 0 ? boxes[0].canvasWidth : 1)
    readonly property real sy: overlay.height / Math.max(1, boxes.length > 0 ? boxes[0].canvasHeight : 1)

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
    property bool rotating: false
    property bool resizing: false
    property var startBoxes: []

    readonly property rect shown: liveRect ? liveRect : bounds
    x: shown.x * sx
    y: shown.y * sy
    width: Math.max(1, shown.width * sx)
    height: Math.max(1, shown.height * sy)
    transformOrigin: Item.Center
    rotation: liveRotation

    Keys.onPressed: function(event) {
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
        EditorState.preview.applyGroupTransform(group.boxes, 0, 0, 1, 1, 0, dx, dy)
        EditorState.commitPreviewDrag()
        event.accepted = true
    }

    function canvasPoint(scenePos) {
        const p = overlay.mapFromItem(null, scenePos.x, scenePos.y)
        return Qt.point(p.x / group.sx, p.y / group.sy)
    }

    function begin() {
        group.startBoxes = group.boxes
        overlay.interacting = true
        group.forceActiveFocus()
        EditorState.beginPreviewDrag()
    }

    function finish() {
        group.liveRect = null
        group.liveRotation = 0
        group.rotating = false
        group.resizing = false
        overlay.endInteraction()
    }

    Rectangle {
        anchors.fill: parent
        color: "transparent"
        border.width: Theme.c.transformOverlayBoxBorder.borderWidthSelected
        border.color: Theme.c.transformOverlayBoxBorder.borderSelected
        radius: Theme.c.transformOverlayBoxBorder.radius
    }

    DragHandler {
        id: bodyDrag
        target: null
        enabled: !group.resizing && !group.rotating
        cursorShape: Qt.SizeAllCursor

        property point press: Qt.point(0, 0)
        property rect startRect: Qt.rect(0, 0, 0, 0)

        onActiveChanged: {
            if (active) {
                group.begin()
                bodyDrag.press = group.canvasPoint(bodyDrag.centroid.scenePosition)
                bodyDrag.startRect = group.bounds
            } else {
                group.finish()
            }
        }
        onCentroidChanged: {
            if (!active)
                return
            const p = group.canvasPoint(bodyDrag.centroid.scenePosition)
            const dx = p.x - bodyDrag.press.x
            const dy = p.y - bodyDrag.press.y
            group.liveRect = Qt.rect(bodyDrag.startRect.x + dx, bodyDrag.startRect.y + dy,
                                     bodyDrag.startRect.width, bodyDrag.startRect.height)
            EditorState.preview.applyGroupTransform(group.startBoxes, 0, 0, 1, 1, 0, dx, dy)
        }
    }

    // 4 edges then 4 corners, as on a single clip: the opposite edge or corner is the anchor.
    Repeater {
        model: [
            { dx: -1, dy:  0, cursor: Qt.SizeHorCursor },
            { dx:  1, dy:  0, cursor: Qt.SizeHorCursor },
            { dx:  0, dy: -1, cursor: Qt.SizeVerCursor },
            { dx:  0, dy:  1, cursor: Qt.SizeVerCursor },
            { dx: -1, dy: -1, cursor: Qt.SizeFDiagCursor },
            { dx:  1, dy:  1, cursor: Qt.SizeFDiagCursor },
            { dx:  1, dy: -1, cursor: Qt.SizeBDiagCursor },
            { dx: -1, dy:  1, cursor: Qt.SizeBDiagCursor }
        ]

        delegate: Item {
            id: grip
            required property var modelData

            readonly property real hs: Theme.spacingLg
            readonly property bool isCorner: modelData.dx !== 0 && modelData.dy !== 0
            property point press: Qt.point(0, 0)
            property rect startRect: Qt.rect(0, 0, 0, 0)

            width: hs
            height: hs
            x: (modelData.dx === 0 ? group.width / 2 : (modelData.dx < 0 ? 0 : group.width)) - hs / 2
            y: (modelData.dy === 0 ? group.height / 2 : (modelData.dy < 0 ? 0 : group.height)) - hs / 2

            Rectangle {
                anchors.fill: parent
                radius: Theme.c.transformOverlayResizeGrip.radius
                color: gripArea.containsMouse || gripArea.pressed
                       ? Theme.c.transformOverlayResizeGrip.backgroundHover
                       : Theme.c.transformOverlayResizeGrip.background
                border.width: Theme.c.transformOverlayResizeGrip.borderWidth
                border.color: gripArea.containsMouse || gripArea.pressed
                              ? Theme.c.transformOverlayResizeGrip.borderHover
                              : Theme.c.transformOverlayResizeGrip.border
            }

            MouseArea {
                id: gripArea
                anchors.fill: parent
                anchors.margins: -Theme.spacingMd
                hoverEnabled: true
                cursorShape: grip.modelData.cursor
                preventStealing: true

                onWheel: (wheel) => { wheel.accepted = false }

                onPressed: (mouse) => {
                    const p = mapToItem(group.overlay, mouse.x, mouse.y)
                    grip.press = Qt.point(p.x / group.sx, p.y / group.sy)
                    grip.startRect = group.bounds
                    group.resizing = true
                    group.begin()
                }

                onPositionChanged: (mouse) => {
                    if (!pressed)
                        return
                    const p = mapToItem(group.overlay, mouse.x, mouse.y)
                    const r = grip.startRect
                    const ddx = p.x / group.sx - grip.press.x
                    const ddy = p.y / group.sy - grip.press.y
                    const dxSign = grip.modelData.dx
                    const dySign = grip.modelData.dy
                    let fx = dxSign === 0 ? 1 : Math.max(1, r.width + ddx * dxSign) / Math.max(1, r.width)
                    let fy = dySign === 0 ? 1 : Math.max(1, r.height + ddy * dySign) / Math.max(1, r.height)
                    // Shift holds the ratio on a corner, as it does for footage on a single clip.
                    if (grip.isCorner && (mouse.modifiers & Qt.ShiftModifier)) {
                        const s = Math.abs(fx - 1) >= Math.abs(fy - 1) ? fx : fy
                        fx = s
                        fy = s
                    }
                    const ax = dxSign < 0 ? r.x + r.width : (dxSign > 0 ? r.x : r.x + r.width / 2)
                    const ay = dySign < 0 ? r.y + r.height : (dySign > 0 ? r.y : r.y + r.height / 2)
                    group.liveRect = Qt.rect(ax + (r.x - ax) * fx, ay + (r.y - ay) * fy,
                                             r.width * fx, r.height * fy)
                    EditorState.preview.applyGroupTransform(group.startBoxes, ax, ay, fx, fy, 0, 0, 0)
                }

                onReleased: group.finish()
                onCanceled: group.finish()
            }
        }
    }

    Item {
        id: rotateHandle
        width: 14
        height: 14
        x: group.width / 2 - width / 2
        y: -28

        Rectangle {
            anchors.top: parent.bottom
            anchors.horizontalCenter: parent.horizontalCenter
            width: 1
            height: 16
            color: Theme.c.transformOverlayRotateHandle.background
        }

        Rectangle {
            anchors.fill: parent
            radius: width / 2
            color: Theme.c.transformOverlayRotateHandle.background
            border.width: Theme.c.transformOverlayRotateHandle.borderWidth
            border.color: Theme.c.transformOverlayRotateHandle.border
        }

        DragHandler {
            id: rotateDrag
            target: null
            cursorShape: Qt.CrossCursor

            property point pivot: Qt.point(0, 0)
            property real startAngle: 0

            function pointerAngle() {
                const p = group.canvasPoint(rotateDrag.centroid.scenePosition)
                return Math.atan2((p.y - rotateDrag.pivot.y) * group.sy,
                                  (p.x - rotateDrag.pivot.x) * group.sx) * 180 / Math.PI
            }

            onActiveChanged: {
                if (active) {
                    group.rotating = true
                    group.begin()
                    const b = group.bounds
                    rotateDrag.pivot = Qt.point(b.x + b.width / 2, b.y + b.height / 2)
                    rotateDrag.startAngle = rotateDrag.pointerAngle()
                    group.liveRect = b
                } else {
                    group.finish()
                }
            }
            onCentroidChanged: {
                if (!active)
                    return
                let deg = group.overlay.normalizeDeg(rotateDrag.pointerAngle() - rotateDrag.startAngle)
                if (!(rotateDrag.centroid.modifiers & Qt.ControlModifier))
                    deg = group.overlay.snapAngle(deg)
                group.liveRotation = deg
                EditorState.preview.applyGroupTransform(group.startBoxes, rotateDrag.pivot.x,
                                                        rotateDrag.pivot.y, 1, 1, deg, 0, 0)
            }
        }
    }
}
