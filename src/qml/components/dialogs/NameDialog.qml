import QtQuick
import QtQuick.Controls.Basic
import Drift

// Name prompt shared by every "save this as a preset" and "rename a preset" flow — text styles
// and effect stacks alike. All of them are a single short field, so they are one dialog rather
// than a family of near-identical ones.
ThemedDialog {
    id: root

    // Emitted with the trimmed name; never fires empty unless allowEmpty (a clearable field).
    signal submitted(string name)

    property alias placeholder: nameField.placeholderText
    property string fieldLabel: qsTr("Name")
    property bool allowEmpty: false

    acceptText: qsTr("Save")
    preferredWidth: Theme.dialogWidthSm

    function openWith(dialogTitle, initialName) {
        root.title = dialogTitle
        nameField.text = initialName
        open()
        nameField.forceActiveFocus()
        nameField.selectAll()
    }

    onAccepted: {
        const name = nameField.text.trim()
        if (name.length > 0 || root.allowEmpty)
            root.submitted(name)
    }

    contentItem: Column {
        spacing: Theme.spacingLg
        width: parent ? parent.width : 320

        ThemedLabel {
            text: root.fieldLabel
        }

        ThemedTextField {
            id: nameField
            width: parent.width
            placeholderText: qsTr("My style")
        }
    }
}
