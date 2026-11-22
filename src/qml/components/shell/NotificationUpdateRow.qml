import QtQuick
import Drift

Rectangle {
    property bool showUpdate
    property real rowHeight
    property real horizontalPadding
    signal updateRequested()
    id: updateRow
    visible: showUpdate
    height: visible ? rowHeight + Theme.spacingSm : 0
    color: updateArea.containsMouse ? Theme.c.notificationUpdateRow.backgroundHover
                                    : Theme.c.notificationUpdateRow.background

    // The whole row opens the dialog, not just its button.
    MouseArea {
        id: updateArea
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: updateRequested()
    }

    IconGlyph {
        id: updateIcon
        anchors.leftMargin: horizontalPadding
        anchors.verticalCenter: parent.verticalCenter
        glyph: Theme.icons.download
        iconSize: Theme.iconSizeBase
        iconColor: Theme.c.notificationUpdateRow.icon
    }

    Text {
        anchors.left: updateIcon.right
        anchors.leftMargin: Theme.spacingMd
        anchors.right: updateButton.left
        anchors.rightMargin: Theme.spacingMd
        anchors.verticalCenter: parent.verticalCenter
        text: Updates.readyToInstall
              ? qsTr("Drift %1 is ready to install").arg(Updates.latestVersion)
              : qsTr("Drift %1 is available").arg(Updates.latestVersion)
        color: Theme.c.notificationUpdateRow.foreground
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSizeSm
        font.weight: Font.Medium
        elide: Text.ElideRight
    }

    ThemedButton {
        id: updateButton
        anchors.rightMargin: horizontalPadding
        anchors.verticalCenter: parent.verticalCenter
        variant: "primary"
        text: qsTr("Update")
        onClicked: updateRequested()
    }
}
