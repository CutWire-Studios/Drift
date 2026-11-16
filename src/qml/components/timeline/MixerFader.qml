import QtQuick
import Drift
import "TimelineWheel.js" as TimelineWheel

// Vertical channel fader. Shift-drag is fine; double-click or the unity notch is 0 dB.
Item {
    id: fader
    property var channel
    width: 26
    height: parent.height

    readonly property real capH: 16
    readonly property real travel: height - capH
    readonly property real capY: travel * (1.0 - fader.channel.mixer.dbToRatio(fader.channel.curDb))

    Accessible.role: Accessible.Slider
    Accessible.name: fader.channel.master ? qsTr("Master volume") : qsTr("%1 volume").arg(fader.channel.fullName)

    // Groove
    Rectangle {
        id: groove
        anchors.horizontalCenter: parent.horizontalCenter
        y: fader.capH / 2
        width: 3
        height: fader.travel
        radius: Theme.c.mixerFader.trackRadius
        color: Theme.c.mixerFader.trackBackground

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: Math.max(0, parent.height - fader.capY)
            radius: parent.radius
            color: fader.channel.fillColor
        }
    }

    // Unity (0 dB) notch
    Rectangle {
        anchors.horizontalCenter: parent.horizontalCenter
        y: fader.capH / 2 + fader.travel * (1.0 - fader.channel.mixer.dbToRatio(0)) - 0.5
        width: 16
        height: 1
        color: Theme.c.mixerFader.foregroundMuted
    }

    Rectangle {
        id: cap
        anchors.horizontalCenter: parent.horizontalCenter
        y: fader.capY
        width: 24
        height: fader.capH
        radius: Theme.c.mixerFader.handleRadius
        color: faderMouse.pressed ? fader.channel.fillColor
             : faderMouse.containsMouse ? Theme.c.mixerFader.handleBackgroundHover : Theme.c.mixerFader.handleBackground
        border.width: Theme.c.mixerFader.borderWidth
        border.color: faderMouse.pressed ? fader.channel.fillColor : Theme.c.mixerFader.handleBorder

        Behavior on color {
            ColorAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
        }

        Rectangle {
            anchors.centerIn: parent
            width: 12
            height: 1
            color: faderMouse.pressed ? Theme.c.mixerFader.handleForegroundPressed : Theme.c.mixerFader.handleForeground
        }
    }

    MouseArea {
        id: faderMouse
        anchors.fill: parent
        anchors.leftMargin: -4
        anchors.rightMargin: -4
        hoverEnabled: true
        cursorShape: pressed ? Qt.ClosedHandCursor : Qt.SizeVerCursor
        preventStealing: true

        property real lastY: 0
        property real dragRatio: 0
        property real wheelAccum: 0

        function emitRatio() {
            fader.channel.volumePreviewed(fader.channel.mixer.dbToLinear(fader.channel.mixer.snapDb(fader.channel.mixer.ratioToDb(dragRatio))))
        }

        // Grabbing the cap drags relative to where it was; clicking the groove
        // jumps the cap there first. Shift slows the drag for fine trims.
        onPressed: (mouse) => {
            lastY = mouse.y
            if (mouse.y >= fader.capY - 3 && mouse.y <= fader.capY + fader.capH + 3) {
                dragRatio = fader.channel.mixer.dbToRatio(fader.channel.curDb)
            } else {
                dragRatio = Math.max(0, Math.min(1, 1.0 - (mouse.y - fader.capH / 2) / fader.travel))
                emitRatio()
            }
        }
        onPositionChanged: (mouse) => {
            if (!pressed || fader.travel <= 0)
                return
            const fine = (mouse.modifiers & Qt.ShiftModifier) ? 0.2 : 1.0
            dragRatio = Math.max(0, Math.min(1, dragRatio - (mouse.y - lastY) / fader.travel * fine))
            lastY = mouse.y
            emitRatio()
        }
        onReleased: fader.channel.volumeCommitted()
        onDoubleClicked: fader.channel.volumeSet(1.0)
        onWheel: (wheel) => {
            const n = TimelineWheel.notches(wheelAccum, TimelineWheel.oneAxis(wheel.angleDelta.x, wheel.angleDelta.y))
            wheelAccum = n.rest
            if (n.steps !== 0)
                fader.channel.nudgeDb(0.5 * n.steps)
        }
    }

    ThemedToolTip {
        visible: faderMouse.containsMouse || faderMouse.pressed
        text: (fader.channel.armed ? qsTr("Mic gain %1 dB") : qsTr("Volume %1 dB")).arg(fader.channel.mixer.formatDb(fader.channel.curDb))
              + (faderMouse.pressed ? "" : qsTr(" — Shift-drag for fine, double-click for 0 dB"))
    }
}
