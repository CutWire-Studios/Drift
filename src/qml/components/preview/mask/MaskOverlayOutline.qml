import QtQuick
import Drift

// The mask outline. Ellipse and the ornamental shapes are drawn as their bounding
// box plus a hint, rather than reproducing the rasterizer's geometry in QML — the
// point of the box is the handles, and the real shape is visible in the composite
// behind it.
Rectangle {
    id: outline

    required property bool selected
    required property bool enabledMask
    required property string shape
    property real roundness: 0

    anchors.fill: parent
    color: "transparent"
    border.width: Theme.c.maskOverlayOutline.borderWidth
    border.color: outline.selected ? Theme.c.maskOverlayOutline.borderSelected : Theme.c.maskOverlayOutline.border
    opacity: !outline.enabledMask ? Theme.c.maskOverlayOutline.opacityDisabled : (outline.selected ? 1.0 : Theme.c.maskOverlayOutline.opacity)
    radius: outline.shape === "ellipse" ? Math.min(width, height) / 2
            : (outline.shape === "rectangle" ? outline.roundness * Math.min(width, height) / 2 : 0)
}
