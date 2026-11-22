import QtQuick
import Drift

// The points and handles the model lists, drawn as HandleKnobs: a corner is a square, a smooth
// point a circle, a handle a diamond, and the selected ones are filled. The knobs only draw: the
// view's own area does the hit-testing, from the model, at the reach of each press's device.
Item {
    id: knobs

    required property CurveGraphModel graph

    Repeater {
        model: knobs.graph.knobs.length

        delegate: HandleKnob {
            id: knob
            required property int index
            readonly property var k: knobs.graph.knobs[index]

            enabled: false
            visible: !!k
            x: (k ? k.x : 0) - width / 2
            y: (k ? k.y : 0) - height / 2
            shape: !k ? "dot" : k.kind === "handle" ? "diamond" : k.kind === "anchor" ? "square" : (k.smooth ? "dot" : "square")
            visualSize: k && k.kind === "anchor" ? 6 : 8
            selected: !!k && k.selected
            highlighted: !!k && k.hot
        }
    }
}
