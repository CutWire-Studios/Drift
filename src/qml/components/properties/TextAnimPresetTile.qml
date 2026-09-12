import QtQuick
import Drift
import ".."

// One preset in the Animate gallery: the preset's settled pose on sample text, with its label
// under it, that plays as a looping sprite sheet while `playing`. An empty presetId is the
// "None" tile.
//
// The owner drives `playing` — hover on a desktop, the selected tile on a phone — so at most
// one sprite runs at a time and the gallery never burns a dozen frame timers.
Column {
    id: tile

    property string slot: "in"
    property string presetId: ""
    property string label: qsTr("None")
    property bool selected: false
    property bool playing: false
    property real tileWidth: 104
    readonly property alias hovered: tileHover.hovered

    signal clicked()

    width: tileWidth
    spacing: 4

    Rectangle {
        id: frame
        width: tile.tileWidth
        height: Math.round(tile.tileWidth * 58 / 104)
        radius: Theme.radiusSm
        color: Theme.textStylePreviewBg
        border.width: tile.selected ? Theme.borderWidthFocus : Theme.borderWidth
        border.color: tile.selected ? Theme.primary
                                    : (tileHover.hovered ? Theme.panelMuted : Theme.textStylePreviewBorder)
        clip: true

        Behavior on border.color {
            ColorAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
        }

        readonly property string spriteBase: tile.presetId.length > 0
                                             ? "image://textanim/" + tile.slot + "/" + tile.presetId + "?w=104&h=58"
                                             : ""

        Image {
            anchors.fill: parent
            anchors.margins: 1
            visible: tile.presetId.length > 0 && !tile.playing
            source: frame.spriteBase.length > 0 ? frame.spriteBase + "&still=1" : ""
            fillMode: Image.PreserveAspectFit
            asynchronous: true
        }

        AnimatedSprite {
            anchors.fill: parent
            anchors.margins: 1
            visible: tile.playing
            source: frame.spriteBase.length > 0 ? frame.spriteBase + "&frames=24" : ""
            frameCount: 24
            frameWidth: 104
            frameHeight: 58
            frameRate: 12
            loops: AnimatedSprite.Infinite
            interpolate: false
            running: tile.playing
        }

        Text {
            anchors.centerIn: parent
            visible: tile.presetId.length === 0
            text: qsTr("None")
            color: Theme.mutedForeground
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
                tile.clicked()
            }
        }
    }

    Text {
        width: parent.width
        text: tile.label
        elide: Text.ElideRight
        horizontalAlignment: Text.AlignHCenter
        color: tile.selected ? Theme.primary : Theme.panelForeground
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSizeXs
    }
}
