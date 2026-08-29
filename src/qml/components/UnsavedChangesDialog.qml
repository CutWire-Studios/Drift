import QtQuick
import QtQuick.Controls.Basic
import Drift

// Save / Don't Save / Cancel gate before New, Open, Recent, or Quit replaces
// dirty work. Three actions — ThemedDialog's two-button footer is not enough,
// so the footer is custom. Enter commits Save; Escape cancels (Popup default).
ThemedDialog {
    id: root

    title: qsTr("Unsaved changes")
    preferredWidth: 440
    showFooter: false
    acceptOnReturn: false
    closePolicy: Popup.CloseOnEscape

    signal saveChosen()
    signal discardChosen()

    function openDialog() {
        open()
    }

    Shortcut {
        sequences: ["Return", "Enter"]
        enabled: root.visible
        onActivated: root.saveChosen()
    }

    contentItem: Column {
        spacing: Theme.spacing2xl
        width: parent ? parent.width : 400

        ThemedLabel {
            width: parent.width
            size: "sm"
            wrapMode: Text.WordWrap
            text: qsTr("“%1” has unsaved changes. Save before continuing?")
                  .arg(EditorState.projectName.length > 0
                       ? EditorState.projectName
                       : qsTr("Untitled project"))
        }

        Item {
            width: parent.width
            height: saveButton.height

            ThemedButton {
                anchors.left: parent.left
                text: qsTr("Don't Save")
                variant: "destructive"
                onClicked: root.discardChosen()
            }

            Row {
                anchors.right: parent.right
                spacing: Theme.spacingLg

                ThemedButton {
                    text: qsTr("Cancel")
                    variant: "secondary"
                    onClicked: root.reject()
                }

                ThemedButton {
                    id: saveButton
                    text: qsTr("Save")
                    variant: "primary"
                    onClicked: root.saveChosen()
                }
            }
        }
    }

    onOpened: saveButton.forceActiveFocus()
}
