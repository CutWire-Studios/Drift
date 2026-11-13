import QtQuick
import Drift

Column {
    property real tileWidth: 150
    property var host
    signal chosen(string lookId)
    id: lookTile
    required property var modelData
    readonly property bool selected: host.lookId === modelData.id
    width: tileWidth
    spacing: 4

    Rectangle {
        width: parent.width
        height: Math.round(width * 0.56)
        radius: Theme.c.textLookTile.radius
        color: Theme.c.textLookTile.background
        border.width: lookTile.selected ? Theme.c.textLookTile.borderWidthFocus : Theme.c.textLookTile.borderWidth
        border.color: lookTile.selected ? Theme.c.textLookTile.borderSelected
                                        : (lookHover.hovered ? Theme.c.textLookTile.borderHover : Theme.c.textLookTile.border)
        clip: true

        Behavior on border.color {
            ColorAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
        }

        Image {
            anchors.fill: parent
            anchors.margins: 2
            asynchronous: true
            cache: false
            fillMode: Image.PreserveAspectFit
            source: host.previewSource(lookTile.modelData.id)
            sourceSize.width: 300
            sourceSize.height: 168
        }

        HoverHandler { id: lookHover }

        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: {
                Haptics.select()
            lookTile.chosen(lookTile.modelData.id)
            }
        }

        AssetFavoriteButton {
            id: favoriteButton
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.margins: 3
            visible: favoriteButton.favorited || lookHover.hovered || Theme.touchUi
            tabId: "textLooks"
            itemId: lookTile.modelData.id
        }
    }

    Text {
        width: parent.width
        text: lookTile.modelData.label
        elide: Text.ElideRight
        horizontalAlignment: Text.AlignHCenter
        color: lookTile.selected ? Theme.c.textLookTile.foregroundSelected : Theme.c.textLookTile.foreground
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSizeXs
    }
}
