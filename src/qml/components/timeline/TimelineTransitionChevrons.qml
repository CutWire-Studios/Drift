import QtQuick
import QtQuick.Shapes
import Drift

// Repeating "> > >" fill for a transition capsule. The parent insets it from the rounded ends,
// so the plain rectangular clip is enough.
Item {
    id: root

    property color color: "transparent"

    clip: true

    readonly property real chevronW: height * 0.22
    readonly property real step: chevronW * 2.4
    readonly property string pathData: {
        const count = Math.min(200, Math.floor(width / step) + 1)
        const top = height * 0.25
        const half = height * 0.25
        let d = ""
        for (let i = 0; i < count; ++i) {
            const x = i * step
            d += "M" + x + " " + top + "L" + (x + chevronW) + " " + (top + half)
                 + "L" + x + " " + (top + 2 * half)
        }
        return d
    }

    Shape {
        anchors.fill: parent
        preferredRendererType: Shape.CurveRenderer
        ShapePath {
            strokeColor: root.color
            strokeWidth: 1.25
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            PathSvg { path: root.pathData }
        }
    }
}
