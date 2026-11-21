import QtQuick
import Drift
IconButton {
    id: button

    property string shortcutTip: ""
    property string shortcutActionId: ""

    anchors.verticalCenter: parent.verticalCenter
    glyph: Theme.icons.stepForward
    variant: "text"
    tooltip: button.shortcutTip
    tooltipActionId: button.shortcutActionId
    onClicked: EditorState.stepFrames(1)
}
