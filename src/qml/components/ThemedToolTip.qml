import QtQuick
import QtQuick.Controls.Basic
import Drift

// Themed tooltip with a consistent reveal delay.
//
// Tooltips used to be raw `ToolTip` instances with no `delay`, so they flashed
// the instant the pointer crossed a control — noisy in dense toolbars. Everything
// routes through here so the timing and chrome match app-wide.
ToolTip {
    id: root

    delay: Theme.tooltipDelay
    // Long enough to read a full sentence of explanatory text.
    timeout: 6000
    padding: Theme.spacingLg

    contentItem: Text {
        text: root.text
        color: Theme.panelForeground
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSizeXs
        wrapMode: Text.WordWrap
        // Keep long explanations readable instead of one very wide line.
        width: Math.min(implicitWidth, 260)
    }

    background: Rectangle {
        color: Theme.panelBackground
        border.width: Theme.borderWidth
        border.color: Theme.panelBorder
        radius: Theme.radiusSm
    }

    // A hair of scale for presence, but durationFast both ways and a shallower
    // 0.97: tooltips are high-frequency, and anything slower reads as lag.
    enter: Transition {
        ParallelAnimation {
            NumberAnimation {
                property: "opacity"
                from: 0.0
                to: 1.0
                duration: Theme.durationFast
                easing.type: Theme.easing
            }
            NumberAnimation {
                property: "scale"
                from: 0.97
                to: 1.0
                duration: Theme.durationFast
                easing.type: Theme.easing
            }
        }
    }

    exit: Transition {
        ParallelAnimation {
            NumberAnimation {
                property: "opacity"
                from: 1.0
                to: 0.0
                duration: Theme.durationFast
                easing.type: Theme.easing
            }
            NumberAnimation {
                property: "scale"
                from: 1.0
                to: 0.97
                duration: Theme.durationFast
                easing.type: Theme.easing
            }
        }
    }
}
