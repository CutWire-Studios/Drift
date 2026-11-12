import QtQuick
import Drift

Canvas {
    property var editor
    id: presetChip
    required property var modelData
    width: 44
    height: 24
    onPaint: {
        const ctx = getContext("2d")
        editor.paintBar(ctx, width, height, presetChip.modelData.stops || [])
    }

    Rectangle {
        anchors.fill: parent
        color: "transparent"
        radius: Theme.c.gradientPresetSwatch.radius
        border.width: presetHover.hovered ? Theme.c.gradientPresetSwatch.borderWidthFocus : Theme.c.gradientPresetSwatch.borderWidth
        border.color: presetHover.hovered ? Theme.c.gradientPresetSwatch.borderHover : Theme.c.gradientPresetSwatch.border
    }
    HoverHandler { id: presetHover }
    ThemedToolTip {
        text: presetChip.modelData.label
        visible: presetHover.hovered
    }
    MouseArea {
        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        onClicked: editor.commitGradient({ "kind": presetChip.modelData.kind,
                                         "angle": presetChip.modelData.angle,
                                         "stops": editor.cloneStops(presetChip.modelData.stops || []) })
    }

    AssetFavoriteButton {
        id: favoriteButton
        anchors.left: parent.left
        anchors.top: parent.top
        buttonSize: 14
        iconSize: 9
        visible: favoriteButton.favorited || presetHover.hovered || Theme.touchUi
        tabId: "textGradients"
        itemId: presetChip.modelData.id
    }
}
