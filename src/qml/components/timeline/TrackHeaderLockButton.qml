import QtQuick
import Drift

// Lock toggle on a track header: clips on a locked track cannot be edited or selected.
IconGlyph {
    id: lockGlyph

    property int trackIndex: -1
    property bool shown: true
    property bool trackLocked: false

    visible: shown
    glyph: trackLocked ? Theme.icons.lock : Theme.icons.lockOpen
    iconSize: 14
    iconColor: trackLocked ? Theme.c.trackHeaderLockButton.locked : Theme.c.trackHeaderLockButton.foregroundMuted
    anchors.centerIn: parent

    Accessible.role: Accessible.CheckBox
    Accessible.name: trackLocked ? qsTr("Unlock track") : qsTr("Lock track")
    Accessible.checked: trackLocked
    Accessible.onToggleAction: lockMouse.clicked(null)

    ThemedToolTip {
        visible: lockMouse.containsMouse
        text: lockGlyph.trackLocked ? qsTr("Unlock track") : qsTr("Lock track")
    }

    MouseArea {
        id: lockMouse
        anchors.fill: parent
        anchors.margins: -4
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: {
            Haptics.toggle(!lockGlyph.trackLocked)
            EditorState.setTrackLocked(lockGlyph.trackIndex, !lockGlyph.trackLocked)
        }
    }
}
