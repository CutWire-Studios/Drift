import QtQuick
import Drift
import "TimelineWheel.js" as TimelineWheel

// Pan knob: 270° sweep centred at 12 o'clock, drag or scroll to move, double-click to centre.
Item {
    id: panArea
    property var channel
    height: 62

    Canvas {
        id: panKnob
        visible: !panArea.channel.master
        width: 40
        height: 40
        anchors.horizontalCenter: parent.horizontalCenter
        antialiasing: true

        readonly property real value: panArea.channel.pan
        readonly property bool hot: panMouse.containsMouse || panMouse.pressed
        readonly property color track: Theme.c.mixerPanControl.track
        readonly property color accent: Theme.c.mixerPanControl.accent
        readonly property color knob: hot ? Theme.c.mixerPanControl.knobBackgroundHover : Theme.c.mixerPanControl.knobBackground
        readonly property color knobBorder: Theme.c.mixerPanControl.knobBorder
        readonly property color indicator: Theme.c.mixerPanControl.indicator
        onValueChanged: requestPaint()
        onHotChanged: requestPaint()
        onTrackChanged: requestPaint()
        onAccentChanged: requestPaint()
        onKnobChanged: requestPaint()
        onKnobBorderChanged: requestPaint()
        onIndicatorChanged: requestPaint()

        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()
            const cx = width / 2
            const cy = height / 2
            const r = 17
            // 270 degree sweep with centre at 12 o'clock.
            const center = 1.5 * Math.PI
            const target = center + value * 0.75 * Math.PI

            ctx.lineCap = "round"
            ctx.lineWidth = Theme.c.mixerPanControl.trackBorderWidth
            ctx.strokeStyle = "" + track
            ctx.beginPath()
            ctx.arc(cx, cy, r, 0.75 * Math.PI, 2.25 * Math.PI, false)
            ctx.stroke()

            if (Math.abs(value) > 0.005) {
                ctx.strokeStyle = "" + accent
                ctx.beginPath()
                ctx.arc(cx, cy, r, Math.min(center, target), Math.max(center, target), false)
                ctx.stroke()
            }

            ctx.beginPath()
            ctx.arc(cx, cy, 12, 0, 2 * Math.PI, false)
            ctx.fillStyle = "" + knob
            ctx.fill()
            ctx.lineWidth = Theme.c.mixerPanControl.borderWidth
            ctx.strokeStyle = "" + knobBorder
            ctx.stroke()

            ctx.lineWidth = Theme.c.mixerPanControl.indicatorBorderWidth
            ctx.strokeStyle = "" + indicator
            ctx.beginPath()
            ctx.moveTo(cx + 4 * Math.cos(target), cy + 4 * Math.sin(target))
            ctx.lineTo(cx + 11 * Math.cos(target), cy + 11 * Math.sin(target))
            ctx.stroke()
        }

        MouseArea {
            id: panMouse
            anchors.fill: parent
            anchors.margins: -4
            hoverEnabled: true
            cursorShape: Qt.SizeVerCursor
            preventStealing: true

            property point last
            property real dragPan: 0
            property real wheelAccum: 0

            onPressed: (mouse) => {
                last = Qt.point(mouse.x, mouse.y)
                dragPan = panArea.channel.pan
            }
            onPositionChanged: (mouse) => {
                if (!pressed)
                    return
                // Up or right pans right; Shift for fine control.
                const fine = (mouse.modifiers & Qt.ShiftModifier) ? 0.2 : 1.0
                const d = ((mouse.x - last.x) - (mouse.y - last.y)) / 100.0 * fine
                last = Qt.point(mouse.x, mouse.y)
                dragPan = Math.max(-1.0, Math.min(1.0, dragPan + d))
                panArea.channel.panPreviewed(Math.abs(dragPan) < 0.04 ? 0.0 : dragPan)
            }
            onReleased: panArea.channel.panCommitted()
            onDoubleClicked: panArea.channel.panSet(0.0)
            onWheel: (wheel) => {
                const n = TimelineWheel.notches(wheelAccum, TimelineWheel.oneAxis(wheel.angleDelta.x, wheel.angleDelta.y))
                wheelAccum = n.rest
                if (n.steps !== 0)
                    panArea.channel.nudgePan(0.05 * n.steps)
            }
        }

        ThemedToolTip {
            visible: panMouse.containsMouse && !panMouse.pressed
            text: qsTr("Pan %1 — drag to adjust, double-click to center").arg(panArea.channel.mixer.formatPan(panArea.channel.pan))
        }
    }

    Text {
        visible: !panArea.channel.master
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        text: panArea.channel.mixer.formatPan(panArea.channel.pan)
        color: Math.abs(panArea.channel.pan) > 0.005 ? Theme.c.mixerPanControl.foreground : Theme.c.mixerPanControl.foregroundMuted
        font.family: Theme.monoFontFamily
        font.pixelSize: Theme.fontSizeXs
    }
}
