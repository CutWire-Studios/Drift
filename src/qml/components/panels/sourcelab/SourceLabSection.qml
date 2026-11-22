import QtQuick
import Drift

// A collapsible block of the Process panel: title, a dot while it differs from the defaults, and
// a reset button.
Item {
    id: root

    property string title: ""
    property bool expanded: true
    property bool dirty: false
    property bool resettable: true

    signal resetRequested()

    default property alias content: body.data

    width: parent ? parent.width : 200
    implicitHeight: column.height

    Column {
        id: column
        width: parent.width

        Rectangle {
            width: parent.width
            height: Theme.c.sourceLabPanel.cardBorderWidth
            color: Theme.c.sourceLabPanel.cardBorder
        }

        Item {
            id: header
            width: parent.width
            height: Theme.controlHeightSm + Theme.spacingMd

            TapHandler { onTapped: root.expanded = !root.expanded }

            Row {
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                spacing: Theme.spacingSm

                IconGlyph {
                    anchors.verticalCenter: parent.verticalCenter
                    glyph: root.expanded ? Theme.icons.chevronDown : Theme.icons.chevronRight
                    iconSize: Theme.iconSizeSm
                }
                ThemedLabel {
                    anchors.verticalCenter: parent.verticalCenter
                    tone: "default"
                    size: "sm"
                    text: root.title
                }
                Rectangle {
                    anchors.verticalCenter: parent.verticalCenter
                    visible: root.dirty
                    width: Theme.spacingMd
                    height: width
                    radius: Theme.radiusPill
                    color: Theme.c.sourceLabPanel.dirtyDot
                }
            }

            IconButton {
                visible: root.resettable
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                glyph: Theme.icons.reset
                tooltip: qsTr("Reset %1").arg(root.title)
                enabled: root.dirty
                onClicked: root.resetRequested()
            }
        }

        Column {
            id: body
            width: parent.width
            spacing: Theme.spacingMd
            visible: root.expanded
            bottomPadding: visible ? Theme.spacingLg : 0
        }
    }
}
