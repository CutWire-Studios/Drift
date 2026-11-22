import QtQuick
import Drift

Column {
    property var editor
    anchors.centerIn: parent
    width: Math.min(220, parent.width - 24)
    spacing: 10
    visible: editor.cues.length === 0

    IconGlyph {
        anchors.horizontalCenter: parent.horizontalCenter
        glyph: Theme.icons.captions
        iconSize: 24
        iconColor: Theme.c.subtitleCueEmptyState.icon
    }
    Text {
        width: parent.width
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.WordWrap
        text: qsTr("No subtitles yet. Move to a time inside this clip and add one below.")
        color: Theme.c.subtitleCueEmptyState.foregroundMuted
        font.family: Theme.fontFamily
        font.pixelSize: Theme.touchInput && Theme.compact ? Theme.fontSizeSm : Theme.fontSizeXs
    }
}
