import QtQuick
import Drift

Column {
    id: root

    property var gaps: []

    spacing: Theme.spacingSm
    visible: root.gaps.length > 0

    ThemedLabel {
        width: parent.width
        tone: "default"
        size: "sm"
        text: qsTr("Known limitations")
    }

    Repeater {
        model: root.gaps

        Row {
            id: gap

            required property string modelData

            width: root.width
            spacing: Theme.spacingMd

            Text {
                text: "•"
                color: Theme.c.interchange.foregroundMuted
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeBase
            }

            Text {
                width: parent.width - Theme.spacingMd - Theme.fontSizeBase
                text: gap.modelData
                color: Theme.c.interchange.foreground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeBase
                wrapMode: Text.WordWrap
            }
        }
    }
}
