import QtQuick
import Drift

// Axis end. Positive ends are larger and labelled; the far end is a darker dot.
Rectangle {
    id: knob

    required property bool positive
    required property real tipX
    required property real tipY
    required property color ink
    required property color negativeInk
    required property string letter
    required property string viewName

    width: knob.positive ? 18 : 12
    height: width
    radius: Theme.radiusPill
    x: knob.tipX - width / 2
    y: knob.tipY - height / 2
    color: knob.positive ? knob.ink : knob.negativeInk
    border.width: knobArea.containsMouse ? Theme.c.viewAxisKnob.borderWidthHover : 0
    border.color: Theme.c.viewAxisKnob.border

    Text {
        visible: knob.positive
        anchors.centerIn: parent
        text: knob.letter
        color: Theme.c.viewAxisKnob.foreground
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSizeXs
        font.bold: true
    }

    MouseArea {
        id: knobArea
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: EditorState.preview.setAxisView(knob.viewName)
    }
}
