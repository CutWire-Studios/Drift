import QtQuick
import Drift

// Tint and bottom hairline of one nested adjustment lane.
Rectangle {
    property string kind
    readonly property color tint: kind === "audioEffects" ? Theme.c.timelineAdjustmentLaneChrome.audioTint
                                  : kind === "mask" ? Theme.c.timelineAdjustmentLaneChrome.maskTint
                                  : Theme.c.timelineAdjustmentLaneChrome.videoTint
    anchors.fill: parent
    color: Qt.rgba(tint.r, tint.g, tint.b, 0.14)
    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 1
        color: Theme.c.timelineAdjustmentLaneChrome.edge
    }
}
