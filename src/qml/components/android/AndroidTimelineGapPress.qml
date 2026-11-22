import QtQuick
import Drift

// Long-press a gap to ripple everything after it left; a tap clears the
// selection like any other empty stretch of track.
Item {
    id: gapItem

    required property var modelData
    property int trackIndex: -1
    property real pxPerSecond: 1
    property bool multiSelectActive: false

    signal gapHeld(int trackIndex, real startSeconds)

    x: modelData.start * gapItem.pxPerSecond
    // The gap's own width, never more: padding it to a minimum
    // spilled it over the next clip's edge, where it swallowed
    // that clip's taps and its trim handle.
    width: Math.max(0, (modelData.end - modelData.start) * gapItem.pxPerSecond)
    height: parent ? parent.height : 0
    z: 1

    Rectangle {
        anchors.fill: parent
        visible: gapPress.pressed
        color: Qt.alpha(Theme.c.androidTimelineGapPress.background, Theme.c.androidTimelineGapPress.backgroundOpacity)
    }

    MouseArea {
        id: gapPress
        anchors.fill: parent
        pressAndHoldInterval: 400
        property bool held: false
        onPressed: held = false
        onPressAndHold: {
            held = true
            Haptics.pickUp()
            gapItem.gapHeld(gapItem.trackIndex, gapItem.modelData.start)
        }
        onClicked: if (!held && !gapItem.multiSelectActive)
                       EditorState.clearSelection()
    }
}
