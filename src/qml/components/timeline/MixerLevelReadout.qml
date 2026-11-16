import QtQuick
import Drift
import "TimelineWheel.js" as TimelineWheel

// dB readout under the fader. Scroll nudges the level; double-click returns to 0 dB.
Rectangle {
    id: readout
    property var channel
    anchors.bottom: parent.bottom
    anchors.bottomMargin: Theme.spacingMd
    anchors.horizontalCenter: parent.horizontalCenter
    width: Math.min(parent.width - Theme.spacingLg, 64)
    height: 24
    radius: Theme.c.mixerLevelReadout.radius
    color: readoutMouse.containsMouse ? Theme.c.mixerLevelReadout.backgroundHover : Theme.c.mixerLevelReadout.background
    border.width: Theme.c.mixerLevelReadout.borderWidth
    border.color: Theme.c.mixerLevelReadout.border

    Text {
        anchors.centerIn: parent
        text: readout.channel.mixer.formatDb(readout.channel.curDb)
        color: readout.channel.armed || readout.channel.curDb > 0.05 ? Theme.c.mixerLevelReadout.foregroundHot : Theme.c.mixerLevelReadout.foreground
        font.family: Theme.monoFontFamily
        font.pixelSize: Theme.fontSizeXs
    }

    MouseArea {
        id: readoutMouse
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.SizeVerCursor
        property real wheelAccum: 0
        onDoubleClicked: readout.channel.volumeSet(1.0)
        onWheel: (wheel) => {
            const n = TimelineWheel.notches(wheelAccum, TimelineWheel.oneAxis(wheel.angleDelta.x, wheel.angleDelta.y))
            wheelAccum = n.rest
            if (n.steps !== 0)
                readout.channel.nudgeDb(0.5 * n.steps)
        }
    }

    ThemedToolTip {
        visible: readoutMouse.containsMouse
        text: readout.channel.armed
              ? qsTr("Mic gain %1 dB (%2%) — scroll to adjust, double-click for 0 dB")
                    .arg(readout.channel.mixer.formatDb(readout.channel.curDb)).arg(Math.round(readout.channel.volume * 100))
              : qsTr("%1 dB — scroll to adjust, double-click for 0 dB").arg(readout.channel.mixer.formatDb(readout.channel.curDb))
    }
}
