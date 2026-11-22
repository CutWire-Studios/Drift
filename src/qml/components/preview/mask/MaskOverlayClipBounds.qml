import QtQuick
import Drift

// The clip's own bounds, so it is obvious what the mask coordinates are relative to.
Rectangle {
    anchors.fill: parent
    color: "transparent"
    border.width: Theme.c.maskOverlayClipBounds.borderWidth
    border.color: Theme.c.maskOverlayClipBounds.border
    opacity: Theme.c.maskOverlayClipBounds.borderOpacity
}
