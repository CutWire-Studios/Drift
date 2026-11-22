import QtQuick
import Drift

// One resize grip on a clip box. dx/dy say which edges it moves (-1 = left/top, +1 = right/bottom,
// 0 = stays put); the opposite edge or corner is held. Corners are squares and edges dots.
HandleKnob {
    id: grip

    required property var modelData
    required property var clipBox
    readonly property var manip: clipBox?.manip ?? null
    readonly property int dx: modelData.dx
    readonly property int dy: modelData.dy
    readonly property bool isCorner: dx !== 0 && dy !== 0
    // The two-way arrow's direction in the box's own frame, 0 = horizontal.
    readonly property real direction: isCorner ? (dx === dy ? 45 : 135) : (dx !== 0 ? 0 : 90)

    shape: isCorner ? "square" : "dot"
    space: clipBox?.stage ?? null
    visualSize: manip?.touch ? 14 : 8
    cursor: "resize:" + (direction + (clipBox?.layoutRotation ?? 0))
    x: (dx === 0 ? clipBox.width / 2 : (dx < 0 ? 0 : clipBox.width)) - width / 2
    y: (dy === 0 ? clipBox.height / 2 : (dy < 0 ? 0 : clipBox.height)) - height / 2

    onPressed: (point, modifiers, device) => clipBox.resizeBegin(grip, point, device)
    onMoved: (point, modifiers) => clipBox.resizeMove(point, modifiers)
    onReleased: if (clipBox.resizing) manip.endGesture()
}
