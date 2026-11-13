import QtQuick
import Drift

// Feather reaches beyond the shape's edge, so show how far. Drawn outside so it
// reads as "the edge fades out to here".
Rectangle {
    id: ring

    required property bool selected
    required property real feather
    required property real scaleX
    required property real scaleY
    required property string shape
    property real roundness: 0

    visible: ring.selected && ring.feather > 0
    anchors.centerIn: parent
    width: parent.width + 2 * ring.feather * ring.scaleX
    height: parent.height + 2 * ring.feather * ring.scaleY
    color: "transparent"
    border.width: Theme.c.maskOverlayFeatherRing.borderWidth
    border.color: Theme.c.maskOverlayFeatherRing.border
    opacity: 0.35
    radius: ring.shape === "ellipse" ? Math.min(width, height) / 2
            : (ring.shape === "rectangle" && ring.roundness > 0
               ? ring.roundness * Math.min(parent.width, parent.height) / 2 + ring.feather * ring.scaleX : 0)
}
