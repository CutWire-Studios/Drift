import QtQuick
import Drift
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
    property string favoriteTab: ""
    readonly property alias hovered: animFrame.hovered

    signal clicked()

    width: tileWidth
    spacing: 4

    TextAnimPresetFrame {
        id: animFrame
        slot: tile.slot
        presetId: tile.presetId
        selected: tile.selected
        playing: tile.playing
        tileWidth: tile.tileWidth
        favoriteTab: tile.favoriteTab
        onClicked: tile.clicked()
    }

    Text {
        width: parent.width
        text: tile.label
        elide: Text.ElideRight
        horizontalAlignment: Text.AlignHCenter
        color: tile.selected ? Theme.c.textAnimPresetTile.foregroundSelected : Theme.c.textAnimPresetTile.foreground
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSizeXs
    }
}
