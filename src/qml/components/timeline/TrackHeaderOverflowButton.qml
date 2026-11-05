import QtQuick
import Drift

IconGlyph {
    id: overflowGlyph

    property bool shown: false
    signal activated()

    visible: shown
    glyph: Theme.icons.ellipsis
    iconSize: 16
    iconColor: Theme.c.trackHeaderOverflowButton.foregroundMuted
    anchors.verticalCenter: parent.verticalCenter

    Accessible.role: Accessible.Button
    Accessible.name: qsTr("More track actions")
    Accessible.onPressAction: overflowGlyph.activated()

    ThemedToolTip {
        visible: overflowMouse.containsMouse
        text: qsTr("More track actions")
    }

    MouseArea {
        id: overflowMouse
        anchors.fill: parent
        anchors.margins: -4
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: overflowGlyph.activated()
    }
}
