import QtQuick
import Drift

// Left gutter — matches track-label width so the lane lines up with the tracks.
Item {
    property var lane
    width: lane.labelsWidth
    height: parent.height

    clip: true

    Column {
        anchors.fill: parent
        anchors.margins: 6
        spacing: 2

        Text {
            width: parent.width
            elide: Text.ElideRight
            text: qsTr("Subtitles")
            color: Theme.c.subtitleCueGutter.foregroundMuted
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeXs
            font.weight: Font.Medium
        }
        Text {
            width: parent.width
            elide: Text.ElideRight
            visible: parent.width >= 60
            text: qsTr("%n captions", "", lane.cues.length)
            color: Theme.c.subtitleCueGutter.foregroundMuted
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeXs
            opacity: 0.8
        }
    }

    Rectangle {
        anchors.right: parent.right
        width: 1
        height: parent.height
        color: Theme.c.subtitleCueGutter.border
    }
}
