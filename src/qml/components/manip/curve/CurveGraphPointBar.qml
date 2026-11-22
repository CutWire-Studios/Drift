import QtQuick
import Drift

// What long-press (or right-click) on a point opens: the touch chips that stand in for Shift, Alt
// and Ctrl, then Delete / Smooth / Corner / Reset for the point. The chips' state is the model's.
TouchContextBar {
    id: bar

    required property CurveGraphModel graph
    required property string backendName
    signal dismissed()

    chips: graph.hasTangents && backendName !== "cubic" ? ["snap", "breakHandle", "constrain"]
         : graph.hasTangents ? ["snap", "breakHandle"] : ["snap", "constrain"]

    Binding { target: bar.graph; property: "snapEnabled"; value: bar.snap }
    Binding { target: bar.graph; property: "chipBreak"; value: bar.breakHandle }
    Binding { target: bar.graph; property: "chipConstrain"; value: bar.constrain }

    ThemedChip {
        visible: bar.graph.canDelete
        chipHeight: Theme.androidMinTouchTarget
        text: qsTr("Delete")
        onClicked: {
            bar.graph.deleteSelected()
            bar.dismissed()
        }
    }

    ThemedChip {
        visible: bar.backendName === "speed" && bar.graph.selectionCount > 0
        chipHeight: Theme.androidMinTouchTarget
        text: qsTr("Smooth")
        selected: bar.graph.selectedSmooth
        onClicked: bar.graph.setSelectedSmooth(true)
    }

    ThemedChip {
        visible: bar.backendName === "speed" && bar.graph.selectionCount > 0
        chipHeight: Theme.androidMinTouchTarget
        text: qsTr("Corner")
        selected: !bar.graph.selectedSmooth && bar.graph.selectionCount > 0
        onClicked: bar.graph.setSelectedSmooth(false)
    }

    ThemedChip {
        chipHeight: Theme.androidMinTouchTarget
        text: qsTr("Reset")
        onClicked: {
            bar.graph.resetSelected()
            bar.dismissed()
        }
    }

    ThemedChip {
        chipHeight: Theme.androidMinTouchTarget
        text: qsTr("Done")
        variant: "outline"
        onClicked: bar.dismissed()
    }
}
