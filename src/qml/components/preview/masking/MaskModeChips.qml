import QtQuick
import Drift

// Include / Exclude for the brush family and the AI pick. The state is EditorState.masking.brushErase,
// shared with the Masking panel's Add / Subtract buttons, so it is the same mode wherever it is set.
// Selected is told apart by the sign and the fill, not by colour alone.
Row {
    id: chips

    property bool touch: false

    spacing: Theme.spacingSm

    ThemedChip {
        anchors.verticalCenter: parent.verticalCenter
        accentColor: Theme.c.maskBrushOverlay.stroke
        text: "+ " + qsTr("Include")
        selected: !EditorState.masking.brushErase
        tooltip: qsTr("Add to the mask")
        tooltipActionId: "masking.brushToggleErase"
        onClicked: EditorState.masking.brushErase = false
    }

    ThemedChip {
        anchors.verticalCenter: parent.verticalCenter
        accentColor: Theme.c.maskBrushOverlay.eraseStroke
        text: "− " + qsTr("Exclude")
        selected: EditorState.masking.brushErase
        tooltip: qsTr("Take away from the mask. Alt or right-drag does this for one stroke")
        tooltipActionId: "masking.brushToggleErase"
        onClicked: EditorState.masking.brushErase = true
    }
}
