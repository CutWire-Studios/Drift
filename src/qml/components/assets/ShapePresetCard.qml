import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Window
import Drift
// One shape in the shapes browser.
Column {
    id: root
    required property var modelData
    required property real cardSize
    property bool pickerMode: false
    // The shape the phone page last added.
    property bool picked: false
    signal added()
    width: cardSize
    spacing: Theme.spacingSm

    opacity: shapeDrag.active ? Theme.c.shapePresetCard.dragOpacity : 1
    scale: shapeDrag.active ? 1.04 : (shapeDrag.pressed ? 0.97 : 1.0)

    Behavior on opacity {
        NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
    }
    Behavior on scale {
        NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
    }

    Rectangle {
        width: cardSize
        height: cardSize
        radius: Theme.c.shapePresetCard.radius
        color: shapeDrag.hovered ? Theme.c.shapePresetCard.backgroundHover : Theme.c.shapePresetCard.background
        border.width: root.picked ? Theme.c.shapePresetCard.borderWidthPicked : Theme.c.shapePresetCard.borderWidth
        border.color: root.picked ? Theme.c.shapePresetCard.borderPicked
                                  : shapeDrag.hovered ? Theme.c.shapePresetCard.borderHover : "transparent"

        Behavior on color {
            ColorAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
        }
        Behavior on border.color {
            ColorAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
        }

        // The catalog entry drawn with its real default style, so the card
        // shows what lands on the timeline.
        Image {
            anchors.fill: parent
            anchors.margins: Theme.pagePadding
            source: "image://shape/" + root.modelData.id
            sourceSize: Qt.size(cardSize * 2, cardSize * 2)
            fillMode: Image.PreserveAspectFit
            asynchronous: true
            cache: true
            scale: shapeDrag.pressed ? 0.94 : shapeDrag.hovered ? 1.06 : 1.0
            Behavior on scale {
                NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
            }
        }

        ThemedToolTip {
            text: qsTr("%1 — click to add, or drag to the timeline or preview").arg(root.modelData.label)
            visible: shapeDrag.hovered && !shapeDrag.active
        }

        AssetDragSource {
            id: shapeDrag
            hasContextMenu: true
            pickerMode: root.pickerMode
            onContextMenuRequested: cardMenu.popup()
            anchors.fill: parent
            kind: "shape"
            payload: root.modelData.id
            label: root.modelData.label
            glyph: Theme.icons.shapes
            onTapped: {
                EditorState.addShapeClip(root.modelData.id, -1)
                added()
            }
        }

        AssetQuickAddButton {
            z: 5
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.margins: 4
            revealed: shapeDrag.hovered
            tooltip: qsTr("Add at playhead")
            onClicked: shapeDrag.tapped(1)
        }

        ThemedContextMenu {
            id: cardMenu

            property bool favorited: false

            onAboutToShow: favorited = EditorState.isAssetFavorite("shapes", root.modelData.id)

            ThemedMenuItem {
                text: qsTr("Add at playhead")
                icon.name: Theme.icons.plus
                onTriggered: shapeDrag.tapped(1)
            }
            ThemedMenuItem {
                text: cardMenu.favorited ? qsTr("Remove from favorites") : qsTr("Add to favorites")
                icon.name: Theme.icons.star
                onTriggered: EditorState.toggleAssetFavorite("shapes", root.modelData.id)
            }
        }

        AssetFavoriteButton {
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 3
            tabId: "shapes"
            itemId: root.modelData.id
        }
    }

    Text {
        width: parent.width
        text: modelData.label
        color: Theme.c.shapePresetCard.foregroundMuted
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSizeCard
        horizontalAlignment: Text.AlignHCenter
        elide: Text.ElideRight
    }
}
