import QtQuick
import Drift

Column {
    property var row
    property int effectIndex: -1
    property bool procedural: false
    spacing: 4
    width: parent ? parent.width : implicitWidth
    visible: row && row.paramData.type === "bool"

    // Booleans have nothing to interpolate, so they keep the
    // plain switch and stay off the keyframe strip.
    Row {
        visible: row.paramData.type === "bool"
        width: parent.width
        spacing: 8
        Text {
            width: parent.width - 48
            elide: Text.ElideRight
            text: row.paramData.label
            color: Theme.c.effectsBoolParam.foregroundMuted
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeXs
            anchors.verticalCenter: parent.verticalCenter
        }
        Text {
            width: 40
            horizontalAlignment: Text.AlignRight
            text: row.paramData.value ? qsTr("On") : qsTr("Off")
            color: Theme.c.effectsBoolParam.foreground
            font.family: Theme.monoFontFamily
            font.pixelSize: Theme.fontSizeXs
            anchors.verticalCenter: parent.verticalCenter
        }
    }

    Row {
        visible: row.paramData.type === "bool"
        spacing: 8
        ChannelKeyButton {
            anchors.verticalCenter: parent.verticalCenter
            keyframeList: (row.paramData.keyframes
                           && row.paramData.keyframes.points) || []
            label: row.paramData.label
            onAddRequested: EditorState.setClipKeyframe(
                                EditorState.selectedTrack, EditorState.selectedClip,
                                row.paramData.prop, EditorState.playheadSeconds,
                                row.paramData.value ? 1 : 0)
            onRemoveRequested: EditorState.removeClipKeyframe(
                                   EditorState.selectedTrack, EditorState.selectedClip,
                                   row.paramData.prop, EditorState.playheadSeconds)
        }
        ThemedSwitch {
            readonly property bool animated: !!(row.paramData.keyframes
                && (row.paramData.keyframes.points || []).length > 0)
            checked: animated
                     ? EditorState.propertyValueAt(
                           EditorState.selectedTrack, EditorState.selectedClip,
                           row.paramData.prop,
                           EditorState.inspectorPlayheadSeconds,
                           row.paramData.value ? 1 : 0) > 0.5
                     : !!row.paramData.value
            // Animated: the switch keys the playhead, as the slider does.
            onToggled: animated
                       ? EditorState.setClipKeyframe(
                             EditorState.selectedTrack, EditorState.selectedClip,
                             row.paramData.prop, EditorState.playheadSeconds,
                             checked ? 1 : 0)
                       : procedural
                         ? EditorState.setProceduralParam(
                               EditorState.selectedTrack, EditorState.selectedClip,
                               row.paramData.key, checked ? 1 : 0)
                         : EditorState.setEffectParam(
                               EditorState.selectedTrack, EditorState.selectedClip,
                               effectIndex, row.paramData.key, checked ? 1 : 0)
        }
    }
}
