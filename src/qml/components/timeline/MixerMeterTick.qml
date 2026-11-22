import QtQuick
import Drift

// Hairline tick on a level meter at one labelled scale point.
Rectangle {
    id: tick

    required property var modelData
    property var meterItem

    x: 1
    width: meterItem.width - 2
    height: 1
    y: 1 + meterItem.innerH * (1.0 - meterItem.mixer.dbToRatio(modelData))
    color: Theme.c.mixerMeterTick.foreground
    opacity: Theme.c.mixerMeterTick.foregroundOpacity
}
