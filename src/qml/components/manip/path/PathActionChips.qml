import QtQuick
import Drift

// Pen, open/close and done, for PathEditor's bar: on a phone they follow the modifier chips inside
// the touch context bar, on a desktop they sit alone on a plate.
Row {
    id: chips

    required property var editor
    readonly property var session: editor.session

    spacing: Theme.spacingSm

    ThemedChip {
        visible: chips.session.canPen && !chips.editor.drafting
        anchors.verticalCenter: parent.verticalCenter
        chipHeight: chips.editor.touch ? Theme.androidMinTouchTarget : Theme.controlHeightSm
        text: qsTr("Pen")
        selected: chips.session.penMode
        onClicked: chips.session.penMode = !chips.session.penMode
    }

    ThemedChip {
        visible: !chips.editor.drafting && chips.session.canToggleClosed
        anchors.verticalCenter: parent.verticalCenter
        chipHeight: chips.editor.touch ? Theme.androidMinTouchTarget : Theme.controlHeightSm
        text: chips.session.selectedClosed ? qsTr("Open path") : qsTr("Close path")
        onClicked: chips.session.toggleClosedSelected()
    }

    ThemedChip {
        visible: chips.editor.backend !== "mask"
        anchors.verticalCenter: parent.verticalCenter
        chipHeight: chips.editor.touch ? Theme.androidMinTouchTarget : Theme.controlHeightSm
        text: qsTr("Done")
        variant: "outline"
        onClicked: chips.editor.finish()
    }
}
