import QtQuick
import Drift

// Hairline in the gap between two header rows.
Rectangle {
    property real rowHeight: 0

    y: rowHeight + Math.floor(Theme.trackGap / 2)
    width: parent.width
    height: 1
    visible: Theme.trackGap > 0
    color: Theme.c.trackHeaderSeparator.border
}
