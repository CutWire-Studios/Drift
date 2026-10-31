import QtQuick
import Drift

// One edge or corner of the canvas crop. The touch target is the whole item;
// the dot inside it stays small enough not to hide the frame it sits on.
// `crop` is the AndroidCropOverlay: frame geometry, grip sizes, and the crop rect.
Item {

    id: grip

    required property var crop
    required property var modelData

    // The touch target is the whole delegate; the dot inside it stays
    // small enough not to hide the frame it sits on.
    width: crop.gripTouch
    height: crop.gripTouch

    x: crop.frameX + (modelData.dx === 0 ? crop.frameW / 2
                                         : (modelData.dx < 0 ? 0 : crop.frameW)) - width / 2
    y: crop.frameY + (modelData.dy === 0 ? crop.frameH / 2
                                         : (modelData.dy < 0 ? 0 : crop.frameH)) - height / 2

    Rectangle {
        anchors.centerIn: parent
        width: crop.gripSize
        height: crop.gripSize
        radius: Theme.c.androidCropGrip.radius
        color: gripArea.pressed ? Theme.c.androidCropGrip.backgroundPressed
                                : Theme.c.androidCropGrip.background
        border.width: Theme.c.androidCropGrip.borderWidth
        border.color: gripArea.pressed ? Theme.c.androidCropGrip.borderPressed
                                       : Theme.c.androidCropGrip.border

        Behavior on color {
            ColorAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
        }
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
