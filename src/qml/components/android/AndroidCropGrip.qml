import QtQuick
import Drift

// One edge or corner of a crop frame. The touch target is the whole item; what is drawn is an
// L bracket on a corner and a short bar on an edge, sitting just outside the frame line so it never
// covers the picture being judged. `crop` is the overlay: frame geometry, grip size, crop rect.
Item {
    id: grip

    required property var crop
    required property var modelData

    width: crop.gripTouch
    height: crop.gripTouch

    x: crop.frameX + (modelData.dx === 0 ? crop.frameW / 2
                                         : (modelData.dx < 0 ? 0 : crop.frameW)) - width / 2
    y: crop.frameY + (modelData.dy === 0 ? crop.frameH / 2
                                         : (modelData.dy < 0 ? 0 : crop.frameH)) - height / 2

    readonly property bool corner: modelData.dx !== 0 && modelData.dy !== 0
    readonly property real thickness: Theme.c.androidCropGrip.thickness
    // Capped by the frame so the brackets of a small crop do not run into each other.
    readonly property real armLength: Math.max(thickness, Math.min(Theme.c.androidCropGrip.length,
                                                                   crop.frameW / 3, crop.frameH / 3))
    readonly property color handleColor: gripArea.pressed ? Theme.c.androidCropGrip.handlePressed
                                                          : Theme.c.androidCropGrip.handle
    readonly property real cx: width / 2
    readonly property real cy: height / 2

    // Horizontal arm: a corner's top/bottom stroke, or the whole bar of a top/bottom edge.
    Rectangle {
        visible: grip.modelData.dy !== 0
        color: grip.handleColor
        width: grip.corner ? grip.armLength + grip.thickness : grip.armLength * 2
        height: grip.thickness
        x: grip.corner ? (grip.modelData.dx < 0 ? grip.cx - grip.thickness : grip.cx - grip.armLength)
                       : grip.cx - width / 2
        y: grip.modelData.dy < 0 ? grip.cy - grip.thickness : grip.cy
    }

    // Vertical arm: a corner's left/right stroke, or the whole bar of a left/right edge.
    Rectangle {
        visible: grip.modelData.dx !== 0
        color: grip.handleColor
        width: grip.thickness
        height: grip.corner ? grip.armLength + grip.thickness : grip.armLength * 2
        x: grip.modelData.dx < 0 ? grip.cx - grip.thickness : grip.cx
        y: grip.corner ? (grip.modelData.dy < 0 ? grip.cy - grip.thickness : grip.cy - grip.armLength)
                       : grip.cy - height / 2
    }

    property real startX: 0
    property real startY: 0
    property real startCropX: 0
    property real startCropY: 0
    property real startCropW: 0
    property real startCropH: 0

    // Re-derives the dragged-along dimension from `crop.aspect` (w / h in crop units), keeping the
    // edge or corner opposite the grip fixed; an edge-only grip keeps the other axis centred.
    function holdAspect() {
        const a = crop.aspect
        const dx = modelData.dx
        const dy = modelData.dy
        const right = startCropX + startCropW
        const bottom = startCropY + startCropH
        if (dx !== 0 && dy === 0) {
            const h = crop.cropW / a
            crop.cropY = startCropY + (startCropH - h) / 2
            crop.cropH = h
        } else if (dy !== 0 && dx === 0) {
            const w = crop.cropH * a
            crop.cropX = startCropX + (startCropW - w) / 2
            crop.cropW = w
        } else {
            const w = Math.max(crop.cropW, crop.cropH * a)
            const h = w / a
            crop.cropW = w
            crop.cropH = h
            crop.cropX = dx < 0 ? right - w : startCropX
            crop.cropY = dy < 0 ? bottom - h : startCropY
        }
    }

    MouseArea {
        id: gripArea
        anchors.fill: parent
        preventStealing: true

        onPressed: (mouse) => {
            const p = mapToItem(crop, mouse.x, mouse.y)
            grip.startX = p.x
            grip.startY = p.y
            grip.startCropX = crop.cropX
            grip.startCropY = crop.cropY
            grip.startCropW = crop.cropW
            grip.startCropH = crop.cropH
            Haptics.pickUp()
        }

        onPositionChanged: (mouse) => {
            if (!pressed)
                return
            const p = mapToItem(crop, mouse.x, mouse.y)
            const ddx = (p.x - grip.startX) / crop.pxScale
            const ddy = (p.y - grip.startY) / crop.pxScale
            crop.didResize = true

            if (grip.modelData.dx < 0) {
                // Dragging the left edge moves the origin and shrinks the
                // width by the same amount.
                const nx = Math.min(grip.startCropX + ddx,
                                    grip.startCropX + grip.startCropW - 16)
                crop.cropX = nx
                crop.cropW = grip.startCropX + grip.startCropW - nx
            } else if (grip.modelData.dx > 0) {
                crop.cropW = Math.max(16, grip.startCropW + ddx)
            }

            if (grip.modelData.dy < 0) {
                const ny = Math.min(grip.startCropY + ddy,
                                    grip.startCropY + grip.startCropH - 16)
                crop.cropY = ny
                crop.cropH = grip.startCropY + grip.startCropH - ny
            } else if (grip.modelData.dy > 0) {
                crop.cropH = Math.max(16, grip.startCropH + ddy)
            }

            if (crop.aspect > 0)
                grip.holdAspect()
        }

        onReleased: {
            Haptics.drop()
            crop.gripReleased()
        }
        onCanceled: Haptics.drop()
    }
}
