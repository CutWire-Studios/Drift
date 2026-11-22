import QtQuick
import Drift

// Transform-layer tint, stronger while a span foot is dragged over this row.
Rectangle {
    property bool spanPreviewed: false
    property bool transformLayer: false
    property real rowHeight: 0
    width: parent.width
    height: rowHeight
    color: Theme.c.trackHeaderWash.background
    opacity: spanPreviewed ? Theme.c.trackHeaderWash.backgroundOpacitySpan : (transformLayer ? Theme.c.trackHeaderWash.backgroundOpacityTransform : 0)

    Behavior on opacity {
        NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
    }
}
