import QtQuick
import Drift
import ".."

// One preset in the Animate gallery: a looping sprite sheet of the preset playing on sample
// text, with its label under it. An empty presetId is the "None" tile.
//
// The sprite only runs while `active` — the owner gates that on the section being open, the
// tile sitting inside the scroll viewport and the transport being stopped, so a long gallery
// does not burn a dozen frame timers off-screen.
Column {
    id: tile

    property string slot: "in"
    property string presetId: ""
    property string label: qsTr("None")
    property bool selected: false
    property bool active: false
    property real tileWidth: 104
    // The scroll viewport this tile lives in, plus a counter the owner bumps whenever the
    // viewport moves so the (non-reactive) mapToItem is re-run.
    property var viewport: null
    property int viewportRevision: 0

    signal clicked()

    readonly property bool inViewport: {
        void tile.viewportRevision
        void tile.y
        if (!tile.viewport || !tile.viewport.contentItem)
            return true
        const p = tile.mapToItem(tile.viewport.contentItem, 0, 0)
        const top = tile.viewport.contentY
        const bottom = top + tile.viewport.height
        return p.y + tile.height > top - 20 && p.y < bottom + 20
    }

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

        AnimatedSprite {
            anchors.fill: parent
            anchors.margins: 1
            visible: tile.presetId.length > 0
            source: tile.presetId.length > 0
                    ? "image://textanim/" + tile.slot + "/" + tile.presetId + "?w=104&h=58&frames=24"
                    : ""
            frameCount: 24
            frameWidth: 104
            frameHeight: 58
            frameRate: 12
            loops: AnimatedSprite.Infinite
            interpolate: false
            running: tile.active && tile.inViewport && tile.presetId.length > 0
        }

        Text {
            anchors.centerIn: parent
            visible: tile.presetId.length === 0
            text: qsTr("None")
            color: Theme.mutedForeground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeXs
        }

        HoverHandler { id: tileHover }

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
