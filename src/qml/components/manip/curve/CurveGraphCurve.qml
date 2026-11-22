import QtQuick
import QtQuick.Shapes
import Drift

// The curve itself, an optional fill under it and the handle arms, all from the model's sampling.
Shape {
    id: curve

    required property CurveGraphModel graph
    property bool filled: false

    preferredRendererType: Shape.CurveRenderer

    ShapePath {
        strokeColor: "transparent"
        strokeWidth: 0
        fillColor: curve.filled && curve.graph.curvePath.length > 1
                   ? Qt.rgba(Theme.c.curveGraph.fill.r, Theme.c.curveGraph.fill.g, Theme.c.curveGraph.fill.b,
                             Theme.c.curveGraph.fillOpacity)
                   : "transparent"
        PathPolyline { path: curve.filled ? curve.graph.fillPath : [] }
    }

    ShapePath {
        strokeColor: Theme.c.curveGraph.arm
        strokeWidth: Theme.c.curveGraph.armWidth
        fillColor: "transparent"
        PathMultiline { paths: curve.graph.arms }
    }

    ShapePath {
        strokeColor: Theme.c.curveGraph.curve
        strokeWidth: Theme.c.curveGraph.curveWidth
        fillColor: "transparent"
        capStyle: ShapePath.RoundCap
        joinStyle: ShapePath.RoundJoin
        PathPolyline { path: curve.graph.curvePath }
    }
}
