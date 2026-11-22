import QtQuick
import Drift

// The grid of a CurveGraphView: the lines the model lists, with a label on the ones that have one.
// A count model keeps the delegates alive while the points are dragged.
Item {
    id: grid

    required property CurveGraphModel graph

    Repeater {
        model: grid.graph.gridLines.length

        delegate: Item {
            id: line
            required property int index
            readonly property var g: grid.graph.gridLines[index]
            readonly property bool vertical: g ? Math.abs(g.x1 - g.x2) < 0.5 : false

            Rectangle {
                x: line.g ? Math.min(line.g.x1, line.g.x2) : 0
                y: line.g ? Math.min(line.g.y1, line.g.y2) : 0
                width: line.vertical ? Theme.c.curveGraph.gridWidth : (line.g ? Math.abs(line.g.x2 - line.g.x1) : 0)
                height: line.vertical ? (line.g ? Math.abs(line.g.y2 - line.g.y1) : 0) : Theme.c.curveGraph.gridWidth
                color: line.g && line.g.emphasis ? Theme.c.curveGraph.gridEmphasis : Theme.c.curveGraph.grid
                opacity: line.g && line.g.emphasis ? Theme.c.curveGraph.gridEmphasisOpacity : Theme.c.curveGraph.gridOpacity
            }

            Text {
                visible: !!line.g && line.g.label !== ""
                x: line.g ? line.g.x1 + 2 : 0
                y: line.g ? line.g.y1 - height - 1 : 0
                text: line.g ? line.g.label : ""
                color: Theme.c.curveGraph.label
                font.family: Theme.monoFontFamily
                font.pixelSize: Theme.fontSizeXs
            }
        }
    }
}
