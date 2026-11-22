import QtQuick
import Drift

// Drag to widen the header column; double-click restores the default. The strip on the edge is a
// HandleKnob, so a mouse grabs its thin reach and a finger its wider one.
Item {
    id: widthHandle
    property bool resizable: false
    // The phone timeline keeps its own width so desktop and phone settings do not fight.
    property bool phone: false
    readonly property var column: parent
    readonly property real defaultWidth: phone ? Theme.androidTrackLabelsWidth : Theme.trackLabelsWidth

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

    HandleKnob {
        id: mouseStrip
        x: widthHandle.width - 3 - width / 2
        y: 0
        width: touchHitSize
        height: widthHandle.height
        mouseHitHeight: height
        touchHitHeight: height
        shape: "none"
        space: widthHandle
        cursor: Qt.SplitHCursor

        property real dragStartX: 0
        property real dragStartWidth: 0

        TimelineCursorRequest {
            kind: "trackResizeH"
            active: mouseStrip.hovered || mouseStrip.active
        }

        onPressed: (point, modifiers, device) => {
            dragStartX = point.x
            dragStartWidth = widthHandle.column.labelsWidth
            EditorState.drag.begin("trackHeaderWidth", device, qsTr("Resize track labels"))
        }
        onMoved: (point, modifiers, device) => {
            const w = Math.round(dragStartWidth + point.x - dragStartX)
            widthHandle.setWidth(w)
            EditorState.drag.update(qsTr("%1 px").arg(w))
            widthHandle.column.setDragPointer(mapToItem(null, point.x, point.y).x, mapToItem(null, point.x, point.y).y)
        }
        onReleased: EditorState.drag.commit()


        Connections {
            target: EditorState.drag
            function onCancelled() {
                if (mouseStrip.active)
                    widthHandle.setWidth(mouseStrip.dragStartWidth)
            }
        }
    }

    TapHandler {
        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchScreen
        onDoubleTapped: widthHandle.setWidth(widthHandle.defaultWidth)
    }

    Rectangle {
        anchors.right: parent.right
        width: 2
        height: parent.height
        color: Theme.c.trackHeaderResizeHandle.foreground
        opacity: mouseStrip.active || mouseStrip.hovered ? 1 : 0

        Behavior on opacity {
            NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
        }
    }
}
