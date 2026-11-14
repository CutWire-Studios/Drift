import QtQuick
import Drift

// Drag to widen the header column; double-click restores the default. A mouse grabs the thin
// strip on the edge; a finger gets a wider zone that only reacts to touch presses.
Item {
    id: widthHandle
    property bool resizable: false
    // The phone timeline keeps its own width so desktop and phone settings do not fight.
    property bool phone: false
    readonly property real defaultWidth: phone ? Theme.androidTrackLabelsWidth : Theme.trackLabelsWidth
    property real touchStartWidth: 0

    function setWidth(w) {
        if (phone)
            EditorState.preferences.androidTrackLabelsWidth = w
        else
            EditorState.preferences.trackLabelsWidth = w
    }

    visible: resizable
    anchors.right: parent.right
    anchors.top: parent.top
    anchors.bottom: parent.bottom
    width: 20
    z: 50

    MouseArea {
        id: mouseStrip
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: 6
        hoverEnabled: true
        TimelineCursorRequest {
            kind: "trackResizeH"
            active: mouseStrip.containsMouse || mouseStrip.pressed
        }
        preventStealing: true
        onPressed: (mouse) => mouse.accepted = mouse.source === Qt.MouseEventNotSynthesized
        onPositionChanged: (mouse) => {
            if (pressed)
                widthHandle.setWidth(widthHandle.x + x + mouse.x + width / 2)
        }
        onDoubleClicked: widthHandle.setWidth(widthHandle.defaultWidth)
    }

    DragHandler {
        id: touchDrag
        target: null
        acceptedDevices: PointerDevice.TouchScreen
        xAxis.enabled: true
        yAxis.enabled: false
        onActiveChanged: {
            if (active)
                widthHandle.touchStartWidth = widthHandle.parent.width
        }
        onTranslationChanged: {
            if (active)
                widthHandle.setWidth(widthHandle.touchStartWidth + translation.x)
        }
    }

    TapHandler {
        acceptedDevices: PointerDevice.TouchScreen
        onDoubleTapped: widthHandle.setWidth(widthHandle.defaultWidth)
    }

    Rectangle {
        anchors.right: parent.right
        width: 2
        height: parent.height
        color: Theme.c.trackHeaderResizeHandle.foreground
        opacity: mouseStrip.pressed || mouseStrip.containsMouse || touchDrag.active ? 1 : 0

        Behavior on opacity {
            NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
        }
    }
}
