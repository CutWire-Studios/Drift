import QtQuick
import Drift

// Start or end of the caption being typed: the readout, a frame nudge, and
// "set this edge to the playhead".
Column {
    id: group

    required property var modelData
    property string readout: ""
    property bool canSetPlayhead: false

    signal nudge(string edge, int frames)
    signal setToPlayhead(string edge)

    spacing: 0

    Text {
        text: group.modelData.label + "  " + group.readout
        color: Theme.c.androidComposerTimingEdge.foregroundMuted
        font.family: Theme.monoFontFamily
        font.pixelSize: Theme.fontSizeXs
    }

    Row {
        IconButton {
            buttonSize: Theme.androidMinTouchTarget
            glyph: Theme.icons.chevronLeft
            tooltip: qsTr("Back one frame")
            onClicked: group.nudge(group.modelData.edge, -1)
        }
        IconButton {
            buttonSize: Theme.androidMinTouchTarget
            glyph: Theme.icons.chevronRight
            tooltip: qsTr("Forward one frame")
            onClicked: group.nudge(group.modelData.edge, 1)
        }
        IconButton {
            buttonSize: Theme.androidMinTouchTarget
            glyph: group.modelData.glyph
            tooltip: group.modelData.tip
            enabled: group.canSetPlayhead
            onClicked: group.setToPlayhead(group.modelData.edge)
        }
    }
}
