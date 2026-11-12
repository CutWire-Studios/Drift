import QtQuick
import QtQuick.Controls.Basic
import Drift

ItemDelegate {
    required property var dialog
    id: row
    required property var modelData
    required property int index
    width: ListView.view.width
    height: 44
    highlighted: modelData.id === dialog.templateId
    hoverEnabled: true

    background: Rectangle {
        color: {
            if (row.highlighted)
                return Theme.c.layoutChooserTemplateRow.backgroundSelected
            if (row.hovered)
                return Theme.c.layoutChooserTemplateRow.backgroundHover
            return "transparent"
        }
    }

    contentItem: Item {
        anchors.fill: parent
        anchors.leftMargin: 10
        anchors.rightMargin: 10

        Row {
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.spacingLg

            IconGlyph {
                id: templateIcon
                anchors.verticalCenter: parent.verticalCenter
                glyph: row.modelData.icon
                iconSize: 16
                iconColor: row.highlighted
                           ? Theme.c.layoutChooserTemplateRow.iconSelected
                           : Theme.c.layoutChooserTemplateRow.icon
            }

            Column {
                anchors.verticalCenter: parent.verticalCenter
                spacing: 1

                Text {
                    text: row.modelData.label
                    color: row.highlighted
                           ? Theme.c.layoutChooserTemplateRow.foregroundSelected
                           : Theme.c.layoutChooserTemplateRow.foreground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeSm
                    font.weight: Font.Medium
                }

                Text {
                    text: row.modelData.detail
                    color: Theme.c.layoutChooserTemplateRow.foregroundMuted
                    font.family: Theme.monoFontFamily
                    font.pixelSize: Theme.fontSizeXs
                }
            }
        }

        // Mini aspect-ratio box on the right of each row.
        Item {
            id: aspectBox
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            width: 28
            height: 28

            readonly property real aw: {
                switch (row.modelData.aspect) {
                case "9:16": return 12
                case "4:5": return 16
                case "1:1": return 20
                default: return 26
                }
            }
            readonly property real ah: {
                switch (row.modelData.aspect) {
                case "9:16": return 22
                case "4:5": return 20
                case "1:1": return 20
                default: return 15
                }
            }

            Rectangle {
                anchors.centerIn: parent
                width: parent.aw
                height: parent.ah
                radius: Theme.c.layoutChooserTemplateRow.previewRadius
                color: "transparent"
                border.width: Theme.c.layoutChooserTemplateRow.previewBorderWidth
                border.color: row.highlighted ? Theme.c.layoutChooserTemplateRow.previewBorderSelected
                                              : Theme.c.layoutChooserTemplateRow.previewBorder
            }
        }

        AssetFavoriteButton {
            id: favoriteButton
            anchors.right: aspectBox.left
            anchors.rightMargin: Theme.spacingSm
            anchors.verticalCenter: parent.verticalCenter
            visible: favoriteButton.favorited || row.hovered || Theme.touchUi
            tabId: "layouts"
            itemId: row.modelData.id
        }
    }

    onClicked: dialog.templateId = modelData.id
}
