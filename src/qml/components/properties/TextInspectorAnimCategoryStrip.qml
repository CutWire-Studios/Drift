import QtQuick
import Drift

ScrollStrip {
    id: root
    property var inspector
    width: parent.width
    height: Theme.controlHeightSm
    visible: inspector.animCategories.length > 1

    Row {
        id: categoryRow
        height: parent.height
        spacing: Theme.spacingSm

        IconButton {
            anchors.verticalCenter: parent.verticalCenter
            glyph: Theme.icons.star
            variant: "ghost"
            buttonSize: Theme.controlHeightSm
            iconSize: 14
            active: inspector.animFavoritesOnly
            tooltip: qsTr("Favorites")
            onClicked: inspector.animFavoritesOnly = !inspector.animFavoritesOnly
        }

        ThemedChip {
            anchors.verticalCenter: parent.verticalCenter
            text: qsTr("All")
            variant: "secondary"
            selected: !inspector.animFavoritesOnly && inspector.animCategory.length === 0
            onClicked: {
                inspector.animFavoritesOnly = false
                inspector.animCategory = ""
            }
        }

        Repeater {
            model: inspector.animCategories
            delegate: ThemedChip {
                required property var modelData
                required property int index
                anchors.verticalCenter: parent.verticalCenter
                text: modelData.label
                variant: "secondary"
                accentColor: Theme.categoryColor(index)
                selected: !inspector.animFavoritesOnly && inspector.animCategory === modelData.id
                onClicked: {
                    inspector.animFavoritesOnly = false
                    inspector.animCategory = modelData.id
                }
            }
        }
    }
}
