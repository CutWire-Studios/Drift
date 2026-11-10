import QtQuick
import Drift

// One preset shade on a colour parameter (Face Retouch's lip colours and the like).
Rectangle {
    id: swatch

    required property string modelData
    property bool current: false
    property int effectIndex: -1
    property bool procedural: false
    property string paramKey: ""

    width: 22
    height: 22
    radius: Theme.c.effectsColorSwatchChip.radius
    color: modelData
    border.width: current ? Theme.c.effectsColorSwatchChip.borderWidthSelected : Theme.c.effectsColorSwatchChip.borderWidth
    border.color: current ? Theme.c.effectsColorSwatchChip.borderSelected
                : swatchMouse.containsMouse ? Theme.c.effectsColorSwatchChip.borderForeground
                : Theme.c.effectsColorSwatchChip.border

    MouseArea {
        id: swatchMouse
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: swatch.procedural
                   ? EditorState.setProceduralColorParam(
                         EditorState.selectedTrack, EditorState.selectedClip,
                         swatch.paramKey, swatch.modelData)
                   : EditorState.setEffectColorParam(
                         EditorState.selectedTrack, EditorState.selectedClip,
                         swatch.effectIndex, swatch.paramKey, swatch.modelData)
    }
    ThemedToolTip {
        visible: swatchMouse.containsMouse
        text: swatch.modelData
    }
}
