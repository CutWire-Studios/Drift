import QtQuick
import Drift

// Foldable group title on an effect card (the relight package's lights).
Rectangle {
    id: root

    property bool groupOpen: false
    property string groupTitle: ""
    signal toggled()

    width: parent ? parent.width : implicitWidth
    height: Theme.touchInput && Theme.compact ? Theme.androidRowHeight : groupLabel.implicitHeight + 8
    radius: Theme.c.effectsParamGroupHeader.radius
    color: groupMouse.containsMouse ? Theme.c.effectsParamGroupHeader.backgroundHover : "transparent"

    Text {
        id: groupLabel
        anchors.left: parent.left
        anchors.leftMargin: 4
        anchors.verticalCenter: parent.verticalCenter
        text: (root.groupOpen ? "▾  " : "▸  ") + root.groupTitle
        color: Theme.c.effectsParamGroupHeader.foreground
        font.family: Theme.fontFamily
        font.pixelSize: Theme.touchInput && Theme.compact ? Theme.fontSizeSm : Theme.fontSizeXs
        font.weight: Font.Medium
    }
    MouseArea {
        id: groupMouse
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: root.toggled()
    }
}
