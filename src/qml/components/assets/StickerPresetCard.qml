import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Window
import Drift
// One sticker in the stickers browser.
Column {
    id: root

    required property var modelData
    required property real cardSize
    property bool pickerMode: false
    // The sticker the phone page last added.
    property bool picked: false
    signal added()
    width: cardSize
    spacing: Theme.spacingSm
    opacity: stickerDrag.active ? Theme.c.stickerPresetCard.dragOpacity : 1
    scale: stickerDrag.active ? 1.04 : (stickerDrag.pressed ? 0.97 : 1.0)

    Behavior on opacity {
        NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
    }
    Behavior on scale {
        NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
    }

    Rectangle {
        width: cardSize
        height: cardSize
        radius: Theme.c.stickerPresetCard.radius
        color: stickerDrag.hovered ? Theme.c.stickerPresetCard.backgroundHover : Theme.c.stickerPresetCard.background
        border.width: root.picked ? Theme.c.stickerPresetCard.borderWidthPicked : 0
        border.color: Theme.c.stickerPresetCard.borderPicked
        clip: true

        Behavior on color {
            ColorAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
        }

        SkeletonBox {
            anchors.fill: parent
            anchors.margins: Theme.pagePadding
            visible: stickerImage.status === Image.Loading
        }

        Image {
            id: stickerImage
            anchors.fill: parent
            anchors.margins: Theme.pagePadding
            source: EditorState.imageUrl(modelData.path)
            fillMode: Image.PreserveAspectFit
            asynchronous: true
            sourceSize: Qt.size(Math.ceil((cardSize - Theme.pagePadding * 2) * Screen.devicePixelRatio),
                                 Math.ceil((cardSize - Theme.pagePadding * 2) * Screen.devicePixelRatio))
            opacity: status === Image.Ready ? 1 : 0

            Behavior on opacity {
                NumberAnimation { duration: Theme.durationBase; easing.type: Theme.easing }
            }
        }

        IconGlyph {
            anchors.centerIn: parent
            visible: stickerImage.status === Image.Error
            glyph: Theme.icons.error
            iconSize: Theme.iconSizeLg
            iconColor: Theme.c.stickerPresetCard.icon
        }

        ThemedToolTip {
            text: qsTr("%1 — click to add, or drag to the timeline or preview").arg(modelData.label)
            visible: stickerDrag.hovered && !stickerDrag.active
        }

        AssetDragSource {
            id: stickerDrag
            hasContextMenu: true
            pickerMode: root.pickerMode
            onContextMenuRequested: cardMenu.popup()
            anchors.fill: parent
            kind: "sticker"
            payload: modelData.id
            label: modelData.label
            thumbnail: modelData.path
            onTapped: {
                EditorState.addStickerClip(modelData.id, -1)
                added()
            }
        }

        AssetQuickAddButton {
            z: 5
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.margins: 4
            revealed: stickerDrag.hovered
            tooltip: qsTr("Add at playhead")
            onClicked: stickerDrag.tapped(1)
        }

        ThemedContextMenu {
            id: cardMenu

            property bool favorited: false

            onAboutToShow: favorited = EditorState.isAssetFavorite("stickers", modelData.id)

            ThemedMenuItem {
                text: qsTr("Add at playhead")
                icon.name: Theme.icons.plus
                onTriggered: stickerDrag.tapped(1)
            }
            ThemedMenuItem {
                text: cardMenu.favorited ? qsTr("Remove from favorites") : qsTr("Add to favorites")
                icon.name: Theme.icons.star
                onTriggered: EditorState.toggleAssetFavorite("stickers", modelData.id)
            }
        }

        AssetFavoriteButton {
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 3
            tabId: "stickers"
            itemId: modelData.id
        }
    }

    Text {
        width: parent.width
        text: modelData.label
        color: Theme.c.stickerPresetCard.foregroundMuted
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSizeCard
        horizontalAlignment: Text.AlignHCenter
        elide: Text.ElideRight
    }
}
