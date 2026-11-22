import QtQuick
import Drift

// Rule-of-thirds lines inside the crop frame, the usual cue for judging a reframe.
Item {
    Repeater {
        model: 2

        Rectangle {
            required property int index
            width: Theme.c.cropFrame.guideWidth
            height: parent.height
            x: parent.width * (index + 1) / 3
            color: Theme.c.cropFrame.guide
        }
    }
    Repeater {
        model: 2

        Rectangle {
            required property int index
            height: Theme.c.cropFrame.guideWidth
            width: parent.width
            y: parent.height * (index + 1) / 3
            color: Theme.c.cropFrame.guide
        }
    }
}
