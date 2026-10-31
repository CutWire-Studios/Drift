import QtQuick
import Drift
// A line of information or a warning from the layout.
Item {
    id: root

    required property var section
    required property var host

    readonly property bool warning: root.section.tone === "warning"

    implicitHeight: box.height

    Rectangle {
        id: box
        x: root.host.sidePadding
        width: parent.width - root.host.sidePadding * 2
        height: noticeRow.implicitHeight + Theme.spacingXl * 2
        radius: Theme.c.marketplace.noticeRadius
        color: root.warning ? Theme.c.marketplace.noticeWarningBackground
                            : Theme.c.marketplace.noticeInfoBackground

        Row {
            id: noticeRow
            anchors.fill: parent
            anchors.margins: Theme.spacingXl
            spacing: Theme.spacingLg

            IconGlyph {
                glyph: root.warning ? Theme.icons.warning : Theme.icons.info
                iconSize: Theme.iconSizeBase
                iconColor: root.warning ? Theme.c.marketplace.noticeWarningIcon
                                        : Theme.c.marketplace.noticeInfoIcon
            }
            Text {
                width: parent.width - Theme.iconSizeBase - parent.spacing
                text: root.section.text || ""
                color: root.warning ? Theme.c.marketplace.noticeWarningForeground
                                    : Theme.c.marketplace.noticeInfoForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeSm
                wrapMode: Text.WordWrap
            }
        }
    }
}
