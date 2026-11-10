import QtQuick
import Drift

Column {
    property var row
    property int effectIndex: -1
    property bool procedural: false
    visible: row.paramData.type === "enum"
    width: parent.width
    spacing: 4
    Text {
        width: parent.width
        elide: Text.ElideRight
        text: row.paramData.label
        color: Theme.c.effectsEnumParam.foregroundMuted
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSizeXs
    }
    ThemedComboBox {
        width: parent.width
        model: row.paramData.options || []
        currentIndex: Math.round(Number(row.paramData.value) || 0)
        onActivated: (index) => procedural
            ? EditorState.setProceduralParam(
                  EditorState.selectedTrack, EditorState.selectedClip, row.paramData.key, index)
            : EditorState.setEffectParam(
                  EditorState.selectedTrack, EditorState.selectedClip,
                  effectIndex, row.paramData.key, index)
    }
}
