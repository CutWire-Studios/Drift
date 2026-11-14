import QtQuick
import QtQuick.Shapes
import Drift

// 45 degree stripes over whatever is switched off: a hidden lane, a hidden track's header. The
// clip bodies get theirs from the scene-graph renderer, which draws the same pattern. Rectangular
// clip only, so against a rounded parent a stripe can overshoot a corner by a pixel or two.
//
// Hand it the timeline's view state and it draws just the visible span (in 512 px chunks, so
// scrolling does not rebuild the path every frame); without one it covers the whole item.
Item {
    id: root

    property color color: Theme.c.timelineClipItem.disabledStripe
    property real spacing: 8
    property real stripeWidth: 3
    property var viewState: null

    readonly property real from: viewState
                                 ? Math.max(0, Math.floor((viewState.viewX - 256) / 512) * 512) : 0
    readonly property real to: viewState
                               ? Math.min(width, (Math.floor((viewState.viewX + viewState.viewW + 256) / 512) + 1) * 512)
                               : width
    readonly property string pathData: {
        const step = spacing * Math.SQRT2
        const h = height
        let d = ""
        for (let x = Math.ceil((from - h) / step) * step; x < to; x += step)
            d += "M" + (x - from).toFixed(2) + "," + h + " L" + (x - from + h).toFixed(2) + ",0 "
        return d
    }

    clip: true
    enabled: false

    Shape {
        x: root.from
        width: Math.max(0, root.to - root.from)
        height: root.height
        preferredRendererType: Shape.GeometryRenderer

        ShapePath {
            strokeColor: root.color
            strokeWidth: root.stripeWidth
            fillColor: "transparent"
            capStyle: ShapePath.FlatCap

            PathSvg { path: root.pathData }
        }
    }
}
