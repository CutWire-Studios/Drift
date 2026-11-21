import QtQuick
import Drift

// One binding of an action: its key text and an × that removes it.
Rectangle {
    id: root

    property string chord: ""
    property string actionLabel: ""

    signal removeRequested()

    implicitWidth: label.implicitWidth + removeButton.width + Theme.spacingMd * 2
    implicitHeight: Theme.c.shortcutChip.height
    radius: Theme.c.shortcutChip.radius
    color: Theme.c.shortcutChip.background
    border.width: Theme.c.shortcutChip.borderWidth
    border.color: Theme.c.shortcutChip.border

    Accessible.role: Accessible.StaticText
    Accessible.name: EditorState.keymap.displayText(root.chord)

    Text {
        id: label
        anchors.left: parent.left
        anchors.leftMargin: Theme.spacingMd
        anchors.verticalCenter: parent.verticalCenter
        text: EditorState.keymap.displayText(root.chord)
        color: Theme.c.shortcutChip.foreground
        font.family: Theme.monoFontFamily
        font.pixelSize: Theme.fontSizeXs
    }

    IconButton {
        id: removeButton
        anchors.right: parent.right
        anchors.rightMargin: Theme.spacingXs
        anchors.verticalCenter: parent.verticalCenter
        buttonSize: root.height - Theme.spacingXs * 2
        iconSize: Theme.iconSizeSm
        glyph: Theme.icons.x
        tooltip: qsTr("Remove this shortcut")
        Accessible.name: qsTr("Remove shortcut %1 from %2")
                             .arg(EditorState.keymap.displayText(root.chord)).arg(root.actionLabel)
        onClicked: root.removeRequested()
    }
}
