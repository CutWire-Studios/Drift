import QtQuick
import Drift

// The kept rectangle, with the rule-of-thirds cue for judging a reframe. Dragging inside it moves
// the crop without resizing; the grips sit above it and keep their presses. `crop` is the overlay
// (crop rect, pxScale, moveBounds: the area the rect stays inside, or an empty size for none).
Rectangle {
    id: frame

    required property var crop

    color: "transparent"
    border.width: Theme.c.androidCropFrame.borderWidth
    border.color: Theme.c.androidCropFrame.border

    Repeater {
        model: 2
        Rectangle {
            width: Theme.c.androidCropFrame.guideWidth
            height: parent.height
            x: parent.width * (index + 1) / 3
            color: Theme.c.androidCropFrame.guide
        }
    }
    Repeater {
        model: 2
        Rectangle {
            height: Theme.c.androidCropFrame.guideWidth
            width: parent.width
            y: parent.height * (index + 1) / 3
            color: Theme.c.androidCropFrame.guide
        }
    }

    MouseArea {
        anchors.fill: parent
        preventStealing: true

        property point start
        property real startCropX: 0
        property real startCropY: 0

        onPressed: (mouse) => {
            start = mapToItem(frame.crop, mouse.x, mouse.y)
            startCropX = frame.crop.cropX
            startCropY = frame.crop.cropY
            Haptics.pickUp()
        }

        onPositionChanged: (mouse) => {
            if (!pressed)
                return
            const p = mapToItem(frame.crop, mouse.x, mouse.y)
            let x = startCropX + (p.x - start.x) / frame.crop.pxScale
            let y = startCropY + (p.y - start.y) / frame.crop.pxScale
            const b = frame.crop.moveBounds
            if (b.width > 0 && b.height > 0) {
                x = Math.max(0, Math.min(b.width - frame.crop.cropW, x))
                y = Math.max(0, Math.min(b.height - frame.crop.cropH, y))
            }
            frame.crop.cropX = x
            frame.crop.cropY = y
            frame.crop.didResize = true
        }

        onReleased: {
            Haptics.drop()
            frame.crop.gripReleased()
        }
        onCanceled: Haptics.drop()
    }
}
