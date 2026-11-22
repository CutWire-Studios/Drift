import QtQuick
import Drift

Column {
    id: root
    property var inspector
    readonly property bool phone: Theme.touchInput && Theme.compact
    width: parent.width
    spacing: 10
    visible: inspector.selectedEffects.length > 0

    Text {
        visible: !root.phone
        width: parent.width
        wrapMode: Text.WordWrap
        text: Theme.touchInput ? qsTr("Move to a time, set a value, then tap the diamond to add a keyframe. With Auto keyframes on, dragging a slider also creates them.")
                                   : qsTr("Move to a time, set a value, then click the diamond to add a keyframe. With Auto keyframes on, dragging a slider also creates them.")
        color: Theme.c.effectsAutoKeyHint.foregroundMuted
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSizeXs
    }

    Row {
        width: parent.width
        spacing: Theme.spacingMd

        ThemedChip {
            id: autoKeyChip
            anchors.verticalCenter: parent.verticalCenter
            text: qsTr("Auto keyframes")
            selected: EditorState.autoKeyEnabled
            onClicked: EditorState.autoKeyEnabled = !EditorState.autoKeyEnabled
        }

        Text {
            visible: root.phone
            anchors.verticalCenter: parent.verticalCenter
            width: parent.width - autoKeyChip.width - parent.spacing
            wrapMode: Text.WordWrap
            text: qsTr("Tap a diamond to keyframe a value, or let Auto add one as you drag.")
            color: Theme.c.effectsAutoKeyHint.foregroundMuted
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeSm
        }
    }
}
