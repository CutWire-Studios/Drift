import QtQuick
import Drift

// The preview's on-screen controls, kept off the picture until they are wanted: they fade in when
// the pointer moves over the preview and out again after a couple of idle seconds. They hold
// while hovered, while one of their popups is open, and while a tool is mid-drag.
Item {
    id: osd

    // The PreviewViewport whose zoom the chip reports and resets.
    property var viewport
    property bool interacting: false
    // Controls outside this item that fade with it (the fullscreen transport), and are in use.
    property bool held: false

    readonly property bool pinned: interacting || held || cluster.menuOpen || cluster.menuBusy
                                   || cluster.hovered
    property bool awake: false
    readonly property bool shown: awake || pinned

    function wake() {
        awake = true
        idleTimer.restart()
    }

    // Passive, so the tools underneath still get their own hover and cursors.
    HoverHandler {
        id: pointerHover
        readonly property point position: point.position
        onPositionChanged: osd.wake()
        onHoveredChanged: if (hovered) osd.wake()
    }

    Timer {
        id: idleTimer
        interval: 2000
        onTriggered: osd.awake = false
    }

    Item {
        id: controls
        anchors.fill: parent
        opacity: osd.shown ? 1 : 0
        visible: opacity > 0

        Behavior on opacity {
            NumberAnimation {
                duration: osd.shown ? Theme.durationFast : Theme.durationSlow
                easing.type: Theme.easing
            }
        }

        PreviewOsdCluster {
            id: cluster
            anchors.top: parent.top
            anchors.right: parent.right
            anchors.margins: Theme.spacingLg
            viewport: osd.viewport
        }
    }
}
