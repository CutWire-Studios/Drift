import QtQuick
import Drift

// Solo toggle on an audio-capable track header, drawn as the "S" badge DAWs use.
Rectangle {
    id: soloBadge

    property int trackIndex: -1
    property bool shown: true
    property bool trackSolo: false

    visible: shown
    width: 16
    height: 16
    radius: Theme.c.trackHeaderSoloButton.radius
    anchors.centerIn: parent
    color: trackSolo ? Theme.c.trackHeaderSoloButton.accent : "transparent"

    Behavior on color {
        ColorAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
    }

    Accessible.role: Accessible.CheckBox
    Accessible.name: trackSolo ? qsTr("Unsolo track") : qsTr("Solo track")
    Accessible.checked: trackSolo
    Accessible.onToggleAction: soloMouse.clicked(null)

    Text {
        anchors.centerIn: parent
        text: qsTr("S")
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSizeXs
        font.bold: true
        color: soloBadge.trackSolo ? Theme.c.trackHeaderSoloButton.accentForeground
                                   : Theme.c.trackHeaderSoloButton.foregroundMuted
    }

    ThemedToolTip {
        visible: soloMouse.containsMouse
        text: soloBadge.trackSolo ? qsTr("Unsolo track") : qsTr("Solo track")
    }

    MouseArea {
        id: soloMouse
        anchors.fill: parent
        anchors.margins: -4
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: {
            Haptics.toggle(!soloBadge.trackSolo)
            EditorState.setTrackSolo(soloBadge.trackIndex, !soloBadge.trackSolo)
        }
    }
}
