import QtQuick
import Drift

// Tint over one track a selected transform clip moves.
Rectangle {
    property var overlay
    required property var modelData
    visible: overlay.draggingTrack !== modelData
    x: overlay.layerClip ? overlay.layerClip.start * overlay.pxPerSecond : 0
    width: overlay.layerClip ? overlay.layerClip.duration * overlay.pxPerSecond : 0
    y: overlay.rowTop ? overlay.rowTop(modelData) : 0
    height: overlay.rowHeight ? overlay.rowHeight(modelData) : 0
    color: Qt.alpha(Theme.c.transformCoverageTint.background, Theme.c.transformCoverageTint.backgroundOpacity)
    border.width: Theme.c.transformCoverageTint.borderWidth
    border.color: Qt.alpha(Theme.c.transformCoverageTint.border, Theme.c.transformCoverageTint.borderOpacity)
    radius: Theme.c.transformCoverageTint.radius
}
