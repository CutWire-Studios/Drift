import QtQuick
import Drift

// Pivot: the clip centre, which every tool turns and scales about.
Rectangle {
    id: pivot

    required property bool shown
    required property bool touch
    required property real originX
    required property real originY

    visible: pivot.shown
    width: pivot.touch ? 10 : 7
    height: width
    radius: Theme.radiusPill
    x: pivot.shown ? pivot.originX - width / 2 : 0
    y: pivot.shown ? pivot.originY - height / 2 : 0
    color: Theme.c.transformGizmoPivot.background
    border.width: Theme.c.transformGizmoPivot.borderWidth
    border.color: Theme.c.transformGizmoPivot.border
}
