pragma ComponentBehavior: Bound
import QtQuick
import Drift

// The pointer surface of a keyframe view: forwards mouse and touch to a KeyframeInteraction and draws
// its marquee, readout and tooltip. The view decides what a wheel does and what scrolls.
Item {
    id: root

    required property var interaction
    // Takes the keyboard focus when pressed, so the keys the interaction understands reach it.
    property Item focusTarget: null
    // A time view pinch-zooms when set.
    property var pinchTimeView: null
    property bool handlesWheel: true
    property int buttons: Qt.LeftButton | Qt.RightButton | Qt.MiddleButton

    readonly property alias area: area
    readonly property alias readout: readout

    signal wheelMoved(var wheel)

    // A digit typed while a key is dragged opens the readout's entry; true when it was taken.
    function handleKey(event) {
        if (!root.interaction.entryAvailable() || !/^[0-9.\-]$/.test(event.text))
            return false
        readout.beginEntry(event.text)
        return true
    }

    KeyframesMarquee {
        x: root.interaction.marquee.x
        y: root.interaction.marquee.y
        width: root.interaction.marquee.width
        height: root.interaction.marquee.height
        visible: root.interaction.marqueeActive && width > 2 && height > 2
    }

    LiveReadout {
        id: readout
        visible: root.interaction.readout !== ""
        text: root.interaction.readout
        pointer: root.interaction.pointer
        editable: !touch
        unit: root.interaction.entryUnit
        onApplied: (text, value) => root.interaction.applyTyped(value)
        onTabPressed: root.interaction.entryTarget = root.interaction.entryTarget === "time" ? "value" : "time"
    }

    Timer {
        interval: 16
        repeat: true
        running: root.interaction.busy
        onTriggered: root.interaction.autoScroll()
    }

    ThemedToolTip {
        parent: root
        delay: 600
        x: root.interaction.pointer.x + Theme.spacingXl
        y: root.interaction.pointer.y + Theme.spacingXl
        text: root.interaction.hoverTip
        visible: root.interaction.hoverTip !== "" && !area.pressed
    }

    MouseArea {
        id: area

        function deviceOf(mouse) {
            return mouse.source === Qt.MouseEventNotSynthesized ? DragSession.Mouse : DragSession.Touch
        }

        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: root.buttons
        pressAndHoldInterval: Theme.touchLiftInterval
        cursorShape: root.interaction.cursorShape

        onPressed: (mouse) => {
            if (root.focusTarget)
                root.focusTarget.forceActiveFocus()
            root.interaction.press(mouse.x, mouse.y, mouse.button, mouse.modifiers, deviceOf(mouse))
        }
        onPressAndHold: {
            if (root.interaction.longPress())
                Haptics.pickUp()
        }
        onPositionChanged: (mouse) => {
            if (pressed)
                root.interaction.move(mouse.x, mouse.y, mouse.modifiers)
            else
                root.interaction.hover(mouse.x, mouse.y, deviceOf(mouse))
        }
        onReleased: (mouse) => root.interaction.release(mouse.x, mouse.y, mouse.button, mouse.modifiers)
        onCanceled: root.interaction.cancelPress()
        onExited: root.interaction.clearHover()
        onDoubleClicked: (mouse) => {
            if (mouse.button === Qt.LeftButton)
                root.interaction.doubleClick(mouse.x, mouse.y, deviceOf(mouse))
        }
        onWheel: (wheel) => {
            if (root.handlesWheel)
                root.wheelMoved(wheel)
            else
                wheel.accepted = false
        }
    }

    PinchHandler {
        property real startPps: 1

        enabled: root.pinchTimeView !== null
        target: null
        acceptedDevices: PointerDevice.TouchScreen | PointerDevice.TouchPad
        onActiveChanged: {
            if (!active)
                return
            startPps = root.pinchTimeView.pxPerSecond
            root.interaction.cancelPress()
        }
        onActiveScaleChanged: {
            if (active)
                root.pinchTimeView.zoomAt(centroid.position.x, startPps * activeScale / root.pinchTimeView.pxPerSecond)
        }
    }
}
