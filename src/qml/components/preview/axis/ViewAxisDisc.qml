import QtQuick
import Drift

// Faint disc behind the axis ends. It fills in while the pointer is over the widget.
Rectangle {
    anchors.fill: parent
    radius: Theme.radiusPill
    color: hover.hovered ? Theme.c.viewAxisDisc.backgroundHover : "transparent"

    HoverHandler { id: hover }
}
