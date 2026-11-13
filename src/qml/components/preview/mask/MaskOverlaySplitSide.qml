import QtQuick
import QtQuick.Shapes
import Drift

// The seam of a Split mask across the clip frame, and a shaded wash over the side it reveals.
// Rotation 0 is a vertical seam revealing the left; the angle turns clockwise, as the rasterizer does.
Item {
    id: side

    required property bool selected
    required property real seamX
    required property real seamY
    required property real angle

    readonly property real frameW: width
    readonly property real frameH: height
    readonly property var clipped: {
        const r = side.angle * Math.PI / 180
        const nx = -Math.cos(r)
        const ny = -Math.sin(r)
        const corners = [Qt.point(0, 0), Qt.point(side.frameW, 0),
                         Qt.point(side.frameW, side.frameH), Qt.point(0, side.frameH)]
        const polygon = []
        const cuts = []
        for (let i = 0; i < 4; ++i) {
            const a = corners[i]
            const b = corners[(i + 1) % 4]
            const da = (a.x - side.seamX) * nx + (a.y - side.seamY) * ny
            const db = (b.x - side.seamX) * nx + (b.y - side.seamY) * ny
            if (da >= 0)
                polygon.push(a)
            if ((da >= 0) !== (db >= 0)) {
                const t = da / (da - db)
                const cut = Qt.point(a.x + t * (b.x - a.x), a.y + t * (b.y - a.y))
                polygon.push(cut)
                cuts.push(cut)
            }
        }
        return { polygon: polygon, cuts: cuts }
    }

    Shape {
        anchors.fill: parent

        ShapePath {
            strokeColor: "transparent"
            fillColor: Qt.alpha(Theme.c.maskDrawOverlay.fill, side.selected ? 0.18 : 0.08)
            PathPolyline { path: side.clipped.polygon }
        }

        ShapePath {
            strokeColor: side.selected ? Theme.c.maskOverlayBarBand.borderSelected
                                       : Theme.c.maskOverlayBarBand.border
            strokeWidth: Theme.c.maskOverlayBarBand.borderWidth
            fillColor: "transparent"
            PathPolyline { path: side.clipped.cuts }
        }
    }
}
