import QtQuick
import Drift

Row {
    property var host
    width: parent.width
    spacing: 4

    Text {
        anchors.verticalCenter: parent.verticalCenter
        text: qsTr("Keyframes")
        color: Theme.c.keyframeGraphHeaderBar.foregroundMuted
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSizeXs
        font.weight: Font.Medium
    }

    IconButton {
        anchors.verticalCenter: parent.verticalCenter
        glyph: host.curveEditing ? Theme.icons.minimize : Theme.icons.maximize
        variant: "text"
        buttonSize: 20
        iconSize: 14
        tooltip: host.curveEditing
                 ? qsTr("Collapse curve editor to compact view (88px)")
                 : qsTr("Expand curve editor to view and edit Bezier handles (180px)")
        onClicked: host.toggleExpanded()
    }

    IconButton {
        anchors.verticalCenter: parent.verticalCenter
        visible: host.curveEditing
        glyph: Theme.icons.spline
        variant: "text"
        buttonSize: 20
        iconSize: 14
        active: host.showHandles
        tooltip: host.showHandles ? qsTr("Hide Bezier handles")
                                  : qsTr("Show Bezier handles. Without them, drag a curve to bend it")
        onClicked: host.showHandles = !host.showHandles
    }

    // Beat grid / tempo.
    IconButton {
        anchors.verticalCenter: parent.verticalCenter
        glyph: Theme.icons.music
        variant: "text"
        buttonSize: 20
        iconSize: 14
        active: EditorState.beatGridVisible
        enabled: !EditorState.beatAnalysisRunning
        tooltip: EditorState.beatAnalysisRunning
                 ? qsTr("Analyzing…")
                 : (EditorState.beatGridVisible ? qsTr("Hide the beat markers")
                                                : qsTr("Find the beat and show markers"))
        onClicked: {
            if (EditorState.beatGridVisible)
                EditorState.beatGridVisible = false
            else
                host.showBeatLayer(true)
        }
    }

    // Individual transients.
    IconButton {
        anchors.verticalCenter: parent.verticalCenter
        glyph: Theme.icons.audioLines
        variant: "text"
        buttonSize: 20
        iconSize: 14
        active: EditorState.onsetsVisible
        enabled: !EditorState.beatAnalysisRunning
        tooltip: EditorState.beatAnalysisRunning
                 ? qsTr("Analyzing…")
                 : (EditorState.onsetsVisible ? qsTr("Hide hits")
                                              : qsTr("Find beats and hits in the audio under this clip"))
        onClicked: {
            if (EditorState.onsetsVisible)
                EditorState.onsetsVisible = false
            else
                host.showBeatLayer(false)
        }
    }
}
