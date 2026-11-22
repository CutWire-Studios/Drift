import QtQuick
import Drift

// One labelled tick on the phone timeline ruler.
Item {
    id: tick

    required property int index
    property int firstVisibleTick: 0
    property real tickStepSeconds: 1
    property real pxPerSecond: 1
    property var formatTick: function(seconds) { return "" }

    readonly property real tickSeconds: (tick.firstVisibleTick + tick.index) * tick.tickStepSeconds

    x: tick.tickSeconds * tick.pxPerSecond
    width: tick.tickStepSeconds * tick.pxPerSecond
    height: parent ? parent.height : 0

    Rectangle {
        x: 0
        y: parent.height - 10
        width: Theme.c.androidTimelineRulerTick.tickWidth
        height: 8
        color: Qt.alpha(Theme.c.androidTimelineRulerTick.tick, Theme.c.androidTimelineRulerTick.tickOpacity)
    }
    Text {
        x: 4
        y: 4
        text: tick.formatTick(tick.tickSeconds)
        color: Theme.c.androidTimelineRulerTick.foreground
        font.pixelSize: Theme.fontSizeTick
        font.family: Theme.fontFamily
    }
}
