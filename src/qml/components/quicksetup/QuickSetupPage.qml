import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Window
import Drift

Item {
    id: root

    property string title: ""
    property string subtitle: ""
    property string glyph: ""
    property bool canAdvance: true
    property string nextLabel: ""
    property real contentMaxWidth: 720
    readonly property bool mobile: Window.window ? Window.window.shellPreference === "mobile" : false

    default property alias content: contentColumn.data

    Flickable {
        id: flick

        anchors.fill: parent
        contentWidth: width
        contentHeight: Math.max(height, body.height)
        boundsBehavior: Flickable.StopAtBounds
        clip: true
        ScrollBar.vertical: AppScrollBar { }

        Column {
            id: body

            readonly property real margin: root.mobile ? Theme.spacingXl : Theme.spacing3xl

            x: (flick.width - width) / 2
            y: 0
            width: Math.min(flick.width - margin * 2, root.contentMaxWidth)
            topPadding: root.mobile ? Theme.spacing2xl : Theme.spacing3xl
            bottomPadding: Theme.spacing2xl
            spacing: Theme.spacingLg

            Rectangle {
                anchors.horizontalCenter: parent.horizontalCenter
                width: root.mobile ? 88 : 72
                height: width
                radius: width / 2
                color: Theme.c.quickSetup.iconBackground
                visible: root.glyph.length > 0

                IconGlyph {
                    anchors.centerIn: parent
                    glyph: root.glyph
                    iconSize: parent.width * 0.46
                    iconColor: Theme.c.quickSetup.iconForeground
                }
            }

            Text {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                text: root.title
                color: Theme.c.quickSetup.foreground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeBase * (root.mobile ? 1.9 : 1.7)
                font.weight: Font.DemiBold
            }

            Text {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                text: root.subtitle
                visible: text.length > 0
                color: Theme.c.quickSetup.foregroundMuted
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeBase
            }

            Item {
                width: 1
                height: Theme.spacingMd
            }

            Column {
                id: contentColumn

                width: parent.width
                spacing: Theme.spacingXl
            }
        }
    }
}
