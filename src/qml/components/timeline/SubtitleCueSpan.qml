import QtQuick
import Drift

// Clip span backdrop behind the subtitle cues.
Rectangle {
    property var lane
    x: lane.xForSeconds(0)
    width: Math.max(2, lane.clipDuration * lane.pxPerSecond)
    height: parent.height
    color: Theme.c.subtitleCueSpan.background
    opacity: Theme.c.subtitleCueSpan.backgroundOpacity
}
