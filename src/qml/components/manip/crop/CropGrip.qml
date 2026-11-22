import QtQuick
import Drift

// One edge or corner of the crop frame. dx/dy say which edges it moves (-1 = left/top, +1 = right/
// bottom, 0 = stays); the opposite edge or corner is held. A corner is an L bracket whose corner
// sits on the frame's, an edge a short bar centred on the frame line.
HandleKnob {
    id: grip

    required property var modelData
    required property var crop
    readonly property int dx: modelData.dx
    readonly property int dy: modelData.dy
    readonly property bool isCorner: dx !== 0 && dy !== 0
    // The two-way arrow's direction in the stage's own frame, 0 = horizontal.
    readonly property real direction: isCorner ? (dx === dy ? 45 : 135) : (dx !== 0 ? 0 : 90)
    // Capped by the frame so the brackets of a small crop do not run into each other.
    readonly property real armLength: Math.max(8, Math.min(crop.touch ? 22 : 16,
                                                           crop.frameRect.width / 3, crop.frameRect.height / 3))
    readonly property real barLength: Math.max(8, Math.min(crop.touch ? 30 : 22,
                                                           (dx === 0 ? crop.frameRect.width : crop.frameRect.height) / 2))

    shape: isCorner ? "bracket" : "bar"
    space: crop.stage
    visualSize: isCorner ? armLength : barLength
    bracketAngle: isCorner ? (dx < 0 ? (dy < 0 ? 0 : 270) : (dy < 0 ? 90 : 180)) : (dx !== 0 ? 90 : 0)
    cursor: "resize:" + (direction + crop.stageRotation)
    enabled: crop.editable

    x: crop.frameRect.x + (dx === 0 ? crop.frameRect.width / 2 : (dx < 0 ? 0 : crop.frameRect.width))
       - width / 2 - (isCorner ? dx * visualSize / 2 : 0)
    y: crop.frameRect.y + (dy === 0 ? crop.frameRect.height / 2 : (dy < 0 ? 0 : crop.frameRect.height))
       - height / 2 - (isCorner ? dy * visualSize / 2 : 0)

    onPressed: (point, modifiers, device) => crop.beginResize(dx, dy, point, device)
    onMoved: (point, modifiers) => crop.moveResize(point, modifiers)
    onReleased: crop.endGesture()
}
