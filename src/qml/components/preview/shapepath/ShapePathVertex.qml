import QtQuick
import Drift

// One path vertex: square for a corner, round for a smooth point. Drawn in the overlay's plane
// and never takes the pointer; the overlay hit-tests everything itself so a drag survives the
// delegates being rebuilt.
Rectangle {
    id: vertex

    required property var info

    readonly property real side: info.first && info.open ? Theme.spacingLg * 1.1 : Theme.spacingLg * 0.9

    x: info.px - side / 2
    y: info.py - side / 2
    width: side
    height: side
    radius: info.smooth ? side / 2 : 0
    z: info.selected ? 3 : 2
    color: info.closing ? Theme.c.shapePathOverlay.closeHint
                        : (info.selected ? Theme.c.shapePathOverlay.vertexSelected
                                         : Theme.c.shapePathOverlay.vertex)
    border.width: 1.5
    border.color: info.selected ? Theme.c.shapePathOverlay.vertexSelectedBorder
                                : Theme.c.shapePathOverlay.vertexBorder
}
