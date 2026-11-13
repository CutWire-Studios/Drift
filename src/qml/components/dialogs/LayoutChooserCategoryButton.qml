import QtQuick
import QtQuick.Controls.Basic
import Drift

AbstractButton {
    required property var dialog
    id: catBtn
    required property var modelData

    // Match ThemedChip / quality pills: Control padding owns the
    // inset so icon+label stay optically centered in the pill.
    height: Theme.controlHeight
    horizontalPadding: Theme.spacingXl
    verticalPadding: 0
    spacing: Theme.spacingMd
    implicitWidth: leftPadding + catContent.implicitWidth + rightPadding
    checkable: false
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus

    readonly property bool selected: modelData.id === dialog.activeCategory && !dialog.favoritesOnly

    background: Rectangle {
        radius: Theme.c.layoutChooserCategoryButton.radius
        color: {
            if (catBtn.selected)
                return Theme.c.layoutChooserCategoryButton.backgroundSelected
            if (catBtn.down)
                return Theme.c.layoutChooserCategoryButton.backgroundPressed
            if (catBtn.hovered)
                return Theme.c.layoutChooserCategoryButton.backgroundHover
            return Theme.c.layoutChooserCategoryButton.background
        }
        border.width: Theme.c.layoutChooserCategoryButton.borderWidth
        border.color: catBtn.selected
                      ? Theme.c.layoutChooserCategoryButton.borderSelected
                      : Theme.c.layoutChooserCategoryButton.border
    }

    contentItem: Item {
        id: catContent
        implicitWidth: catRow.implicitWidth
        implicitHeight: Math.max(catIcon.implicitHeight, catLabel.implicitHeight)

        Row {
            id: catRow
            anchors.centerIn: parent
            spacing: catBtn.spacing

            IconGlyph {
                id: catIcon
                anchors.verticalCenter: parent.verticalCenter
                glyph: catBtn.modelData.icon
                iconSize: Theme.fontSizeSm
                iconColor: catBtn.selected
                           ? Theme.c.layoutChooserCategoryButton.iconSelected
                           : Theme.c.layoutChooserCategoryButton.icon
            }

            Text {
                id: catLabel
                anchors.verticalCenter: parent.verticalCenter
                // Match icon box height so verticalCenter lines up
                // with the glyph, not the taller font metrics box.
                height: catIcon.iconSize
                verticalAlignment: Text.AlignVCenter
                text: catBtn.modelData.label
                color: catBtn.selected
                       ? Theme.c.layoutChooserCategoryButton.foregroundSelected
                       : Theme.c.layoutChooserCategoryButton.foreground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
                font.weight: catBtn.selected ? Font.Medium : Font.Normal
            }
        }
    }

    onClicked: dialog.selectCategory(modelData.id)

    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.NoButton
        cursorShape: Qt.PointingHandCursor
    }
}
