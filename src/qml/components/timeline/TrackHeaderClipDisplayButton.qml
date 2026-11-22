import QtQuick
import Drift

// Cycles a video track's clips through filmstrip, filmstrip plus waveform, and waveform.
IconGlyph {
    id: displayButton

    property var column
    property int trackIndex: -1
    property int clipDisplay: 1
    property bool shown: false
    // Compact headers park this in the bottom-left corner; desktop keeps it in the icon row.
    property bool corner: false

    visible: shown
    anchors.verticalCenter: parent.verticalCenter
    anchors.leftMargin: 4
    anchors.bottomMargin: (trackIndex < column.tracks.length - 1 ? Theme.trackGap : 0) + 4

    states: State {
        name: "corner"
        when: displayButton.corner
        AnchorChanges {
            target: displayButton
            anchors.verticalCenter: undefined
            anchors.left: displayButton.parent.left
            anchors.bottom: displayButton.parent.bottom
        }
    }
    glyph: column.clipDisplayGlyph(clipDisplay)
    iconSize: corner ? Theme.iconSizeMd : 16
    iconColor: clipDisplay === 0 ? Theme.c.trackHeaderClipDisplayButton.foregroundMuted : Theme.c.trackHeaderClipDisplayButton.accent

    ThemedToolTip {
        visible: !displayButton.corner && waveMouse.containsMouse
        text: qsTr("Clips show: %1 (click to change)")
                  .arg(displayButton.column.clipDisplayLabel(displayButton.clipDisplay))
    }

    MouseArea {
        id: waveMouse
        anchors.fill: parent
        anchors.margins: displayButton.corner ? -8 : -4
        anchors.topMargin: Theme.touchInput ? -14 : (displayButton.corner ? -8 : -4)
        anchors.bottomMargin: Theme.touchInput ? -14 : (displayButton.corner ? -8 : -4)
        hoverEnabled: !displayButton.corner
        cursorShape: displayButton.corner ? Qt.ArrowCursor : Qt.PointingHandCursor
        onClicked: {
            Haptics.press()
            EditorState.setTrackClipDisplay(
                displayButton.trackIndex,
                displayButton.column.nextClipDisplay(displayButton.clipDisplay))
        }
    }
}
