import QtQuick
import Drift

// Heading of one group of controls on a phone tool page. Everything dropped inside sits at the
// right edge, e.g. a reset button.
Item {
    id: root

    property string text: ""
    default property alias trailing: trailingRow.data

    width: parent ? parent.width : implicitWidth
    implicitHeight: Math.max(label.implicitHeight, trailingRow.children.length > 0 ? Theme.androidRowHeight : 0)
    height: implicitHeight

    Text {
        id: label
        anchors.left: parent.left
        anchors.right: trailingRow.left
        anchors.verticalCenter: parent.verticalCenter
        text: root.text
        elide: Text.ElideRight
        color: Theme.c.androidPageSection.foreground
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSizeSm
        font.weight: Font.Medium
    }

    Row {
        id: trailingRow
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
    }
}
