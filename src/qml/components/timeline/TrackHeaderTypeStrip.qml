import QtQuick
import Drift

// Left-edge bar in the colour of the track's clips.
Rectangle {
    property string trackType: ""
    property bool transformLayer: false
    property real rowHeight: 0

    width: 3
    height: rowHeight
    color: {
        if (transformLayer)
            return Theme.c.trackHeaderTypeStrip.transform
        switch (trackType) {
        case "audio": return Theme.c.trackHeaderTypeStrip.audio
        case "text": return Theme.c.trackHeaderTypeStrip.text
        case "subtitle": return Theme.c.trackHeaderTypeStrip.subtitle
        case "shape": return Theme.c.trackHeaderTypeStrip.graphic
        case "adjustment": return Theme.c.trackHeaderTypeStrip.effect
        default: return Theme.c.trackHeaderTypeStrip.video
        }
    }
}
