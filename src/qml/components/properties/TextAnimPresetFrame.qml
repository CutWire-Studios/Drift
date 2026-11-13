import QtQuick
import Drift

Rectangle {
    property string slot: "in"
    property string presetId: ""
    property bool selected: false
    property bool playing: false
    property real tileWidth: 104
    // Empty hides the star; the "None" tile has no preset to favourite.
    property string favoriteTab: ""
    readonly property alias hovered: tileHover.hovered
    signal clicked()
    id: root
    width: root.tileWidth
    height: Math.round(root.tileWidth * 58 / 104)
    radius: Theme.c.textAnimPresetFrame.radius
    color: Theme.c.textAnimPresetFrame.background
    border.width: root.selected ? Theme.c.textAnimPresetFrame.borderWidthFocus : Theme.c.textAnimPresetFrame.borderWidth
    border.color: root.selected ? Theme.c.textAnimPresetFrame.borderSelected
                                : (tileHover.hovered ? Theme.c.textAnimPresetFrame.borderHover : Theme.c.textAnimPresetFrame.border)
    clip: true

    Behavior on border.color {
        ColorAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
    }

    readonly property string spriteBase: root.presetId.length > 0
                                         ? "image://textanim/" + root.slot + "/" + root.presetId + "?w=104&h=58"
                                         : ""

    Image {
        anchors.fill: parent
        anchors.margins: 1
        visible: root.presetId.length > 0 && !root.playing
        source: root.spriteBase.length > 0 ? root.spriteBase + "&still=1" : ""
        fillMode: Image.PreserveAspectFit
        asynchronous: true
    }

    AnimatedSprite {
        anchors.fill: parent
        anchors.margins: 1
        visible: root.playing
        // Only the playing tile renders its 24-frame sheet; the image cache keeps it for a replay.
        source: root.playing && root.spriteBase.length > 0 ? root.spriteBase + "&frames=24" : ""
        frameCount: 24
        frameWidth: 104
        frameHeight: 58
        frameRate: 12
        loops: AnimatedSprite.Infinite
        interpolate: false
        running: root.playing
    }

    Text {
        anchors.centerIn: parent
        visible: root.presetId.length === 0
        text: qsTr("None")
        color: Theme.c.textAnimPresetFrame.foregroundMuted
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSizeXs
    }

    // A tap synthesises hover on a phone, so hover only means anything with a pointer.
    HoverHandler { id: tileHover; enabled: !Theme.touchUi }

    MouseArea {
        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        onClicked: {
            Haptics.select()
            root.clicked()
        }
    }

    AssetFavoriteButton {
        id: favoriteButton
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.margins: 3
        visible: root.favoriteTab.length > 0 && root.presetId.length > 0
                 && (favoriteButton.favorited || tileHover.hovered || Theme.touchUi)
        tabId: root.favoriteTab
        itemId: root.presetId
    }
}
