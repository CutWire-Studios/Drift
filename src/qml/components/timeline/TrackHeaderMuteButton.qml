import QtQuick
import Drift

// Mute toggle on a video or audio track header.
IconGlyph {
    id: muteGlyph

    property int trackIndex: -1
    property bool shown: false
    property bool trackMuted: false

    visible: shown
    glyph: trackMuted ? Theme.icons.volumeOff : Theme.icons.volumeHigh
    iconSize: 16
    iconColor: trackMuted ? Theme.c.trackHeaderMuteButton.destructive : Theme.c.trackHeaderMuteButton.foregroundMuted
    anchors.verticalCenter: parent.verticalCenter

    // The tooltip above never renders on touch, so the same string has to
    // reach tooling and screen readers through the accessible name instead.
    Accessible.role: Accessible.CheckBox
    Accessible.name: trackMuted ? qsTr("Unmute track") : qsTr("Mute track")
    Accessible.checked: trackMuted
    Accessible.onToggleAction: muteMouse.clicked(null)

    ThemedToolTip {
        visible: muteMouse.containsMouse
        text: muteGlyph.trackMuted ? qsTr("Unmute track") : qsTr("Mute track")
    }

    MouseArea {
        id: muteMouse
        anchors.fill: parent
        anchors.leftMargin: Theme.touchInput ? -5 : -4
        anchors.rightMargin: Theme.touchInput ? -5 : -4
        anchors.topMargin: Theme.touchInput ? -14 : -4
        anchors.bottomMargin: Theme.touchInput ? -14 : -4
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: {
            Haptics.toggle(!muteGlyph.trackMuted)
            EditorState.setTrackMuted(muteGlyph.trackIndex, !muteGlyph.trackMuted)
        }
    }
}
