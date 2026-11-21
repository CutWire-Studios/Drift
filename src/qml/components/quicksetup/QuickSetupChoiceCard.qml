import QtQuick
import QtQuick.Window
import Drift

QuickSetupCard {
    id: root

    property string glyph: ""
    property string title: ""
    property string detail: ""
    readonly property bool mobile: Window.window ? Window.window.shellPreference === "mobile" : false

    text: title
    implicitHeight: mobile ? 96 : 88

    Row {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: Theme.spacingXl
        anchors.rightMargin: Theme.spacingLg
        anchors.verticalCenter: parent.verticalCenter
        spacing: Theme.spacingLg

        IconGlyph {
            anchors.verticalCenter: parent.verticalCenter
            glyph: root.glyph
            iconSize: 24
            iconColor: root.selected ? Theme.c.quickSetupCard.borderSelected : Theme.c.quickSetupCard.foregroundMuted
        }

        Column {
            width: parent.width - 24 - parent.spacing
            anchors.verticalCenter: parent.verticalCenter
            spacing: 2

            Text {
                width: parent.width
                text: root.title
                elide: Text.ElideRight
                color: Theme.c.quickSetupCard.foreground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeSm
                font.weight: Font.Medium
            }

            Text {
                width: parent.width
                text: root.detail
                wrapMode: Text.WordWrap
                maximumLineCount: 2
                elide: Text.ElideRight
                color: Theme.c.quickSetupCard.foregroundMuted
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
            }
        }
    }
}
