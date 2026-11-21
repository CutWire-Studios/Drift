import QtQuick
import QtQuick.Controls.Basic
import Drift

Rectangle {
    property Popup popup
    id: actionRow

    property alias glyph: rowIcon.glyph
    property alias text: rowLabel.text
    property string shortcutId: ""
    // Opens a menu of its own, so the popup stays up behind it.
    property bool hasSubmenu: false
    readonly property string _shortcutText: shortcutId.length > 0
        ? Theme.shortcutDisplay(EditorState.shortcutFor(shortcutId)) : ""
    signal triggered()

    width: parent ? parent.width : 0
    height: Theme.controlHeight + Theme.spacingSm
    radius: Theme.c.recentProjectsActionRow.radius
    color: actionArea.containsMouse ? Theme.c.recentProjectsActionRow.backgroundHover : "transparent"

    Row {
        anchors.left: parent.left
        anchors.leftMargin: Theme.spacingLg
        anchors.verticalCenter: parent.verticalCenter
        spacing: Theme.spacingLg

        IconGlyph {
            id: rowIcon
            iconSize: Theme.iconSizeMd
            iconColor: Theme.c.recentProjectsActionRow.icon
            anchors.verticalCenter: parent.verticalCenter
        }

        Text {
            id: rowLabel
            color: Theme.c.recentProjectsActionRow.foreground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeSm
            anchors.verticalCenter: parent.verticalCenter
        }
    }

    Text {
        anchors.right: parent.right
        anchors.rightMargin: Theme.spacingLg
        anchors.verticalCenter: parent.verticalCenter
        visible: actionRow._shortcutText.length > 0
        text: actionRow._shortcutText
        color: Theme.c.recentProjectsActionRow.shortcutForeground
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSizeXs
    }

    IconGlyph {
        anchors.right: parent.right
        anchors.rightMargin: Theme.spacingLg
        anchors.verticalCenter: parent.verticalCenter
        visible: actionRow.hasSubmenu
        glyph: Theme.icons.chevronRight
        iconSize: Theme.iconSizeMd
        iconColor: Theme.c.recentProjectsActionRow.icon
    }

    MouseArea {
        id: actionArea
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: {
            if (!actionRow.hasSubmenu)
                popup.close()
            actionRow.triggered()
        }
    }
}
