import QtQuick
import Drift

// One bookmark flag in the seek strip: diamond, stem, drag to move, click to jump.
Item {
    id: bookmarkDelegate

    required property int index
    required property var modelData
    property Item row
    property var panel

    signal contextMenu(bool atPointer, Item anchor, real ax, real ay,
                       int bookmarkIndex, string label, real seconds, string color)
    signal renameRequested(int bookmarkIndex, string label)

    // The flag's own colour, or the theme's when this bookmark has none.
    readonly property color markerColor: modelData.color && modelData.color.length > 0
                                         ? modelData.color
                                         : Theme.c.timelineBookmarkMarker.foreground

    // The stem is a single pixel wide, so it can only ever cover one whole column. Centring the
    // diamond on the item instead of on that column put it half a pixel to the stem's right, and
    // a fractional x smeared the stem across two columns on top of that (#559). Both now hang off
    // stemX, and the item itself lands on a whole pixel.
    readonly property real stemX: Math.round((width - 1) / 2)
    readonly property real markCenterX: stemX + 0.5

    x: Math.round((bookmarkMouse.dragging ? bookmarkMouse.dragSeconds : modelData.seconds)
                  * panel.pxPerSecond - width / 2)
    width: 10
    height: parent.height
    z: bookmarkMouse.containsMouse || bookmarkMouse.dragging ? 4 : 3

    // Stem into the ruler so markers read as jump flags, not dots.
    Rectangle {
        x: bookmarkDelegate.stemX
        y: 0
        width: 1
        height: parent.height + 6
        color: bookmarkDelegate.markerColor
        opacity: 0.7
    }

    Rectangle {
        x: bookmarkDelegate.markCenterX - width / 2
        anchors.verticalCenter: parent.verticalCenter
        width: 8
        height: 8
        rotation: 45
        radius: Theme.c.timelineBookmarkMarker.radius
        color: bookmarkDelegate.markerColor
        border.width: bookmarkMouse.containsMouse ? Theme.c.timelineBookmarkMarker.borderWidth : 0
        border.color: Theme.c.timelineBookmarkMarker.border
    }

    ThemedToolTip {
        visible: bookmarkMouse.containsMouse && !bookmarkMouse.dragging
        text: modelData.label + " @ " + panel.formatTime(modelData.seconds)
    }

    MouseArea {
        id: bookmarkMouse
        anchors.fill: parent
        anchors.margins: -6
        hoverEnabled: true
        cursorShape: pressed ? Qt.ClosedHandCursor : Qt.PointingHandCursor
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        // Stop the seek strip from eating bookmark clicks / drags.
        preventStealing: true

        property bool dragging: false
        property bool didDrag: false
        property bool heldOpen: false
        property real dragSeconds: modelData.seconds
        property real pressX: 0
        property bool pressedByTouch: false

        pressAndHoldInterval: 400
        onPressAndHold: (mouse) => {
            if (!pressedByTouch || didDrag || !(mouse.buttons & Qt.LeftButton))
                return
            heldOpen = true
            bookmarkDelegate.contextMenu(true, bookmarkMouse, mouse.x, mouse.y,
                                         bookmarkDelegate.index, bookmarkDelegate.modelData.label,
                                         bookmarkDelegate.modelData.seconds,
                                         bookmarkDelegate.modelData.color || "")
        }

        onPressed: (mouse) => {
            if (mouse.button !== Qt.LeftButton)
                return
            pressedByTouch = mouse.source !== Qt.MouseEventNotSynthesized
            heldOpen = false
            didDrag = false
            dragging = false
            pressX = mouse.x
            dragSeconds = modelData.seconds
        }
        onPositionChanged: (mouse) => {
            if (!pressed || !(mouse.buttons & Qt.LeftButton))
                return
            if (!didDrag && Math.abs(mouse.x - pressX) < 3)
                return
            didDrag = true
            dragging = true
            const globalX = mapToItem(bookmarkDelegate.row, mouse.x, 0).x
            dragSeconds = EditorState.snapTime(Math.max(0, globalX / bookmarkDelegate.panel.pxPerSecond))
        }
        onReleased: (mouse) => {
            if (mouse.button === Qt.LeftButton && didDrag) {
                EditorState.updateBookmark(bookmarkDelegate.index, dragSeconds, modelData.label)
            }
            dragging = false
            // Keep didDrag through onClicked (fires after release).
            Qt.callLater(function() { didDrag = false })
        }
        onClicked: (mouse) => {
            if (mouse.button === Qt.RightButton) {
                bookmarkDelegate.contextMenu(false, bookmarkMouse, mouse.x, mouse.y,
                                             bookmarkDelegate.index, bookmarkDelegate.modelData.label,
                                             bookmarkDelegate.modelData.seconds,
                                             bookmarkDelegate.modelData.color || "")
                return
            }
            if (!didDrag && !heldOpen)
                EditorState.goToBookmark(bookmarkDelegate.index)
        }
        onDoubleClicked: (mouse) => {
            if (mouse.button !== Qt.LeftButton)
                return
            bookmarkDelegate.renameRequested(bookmarkDelegate.index, bookmarkDelegate.modelData.label)
        }
    }
}
