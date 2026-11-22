import QtQuick
import Drift

// Drawn rather than a ThemedSwitch: the whole row is the target, and a real
// switch nested inside a button either fights it for the press or has to be
// disabled, which greys it out exactly when its state is what you came to read.
Rectangle {
    id: switchTrack

    property bool checked: false

    width: 40
    height: 22
    radius: Theme.radiusPill
    color: switchTrack.checked ? Theme.c.androidSheetActionSwitch.backgroundChecked
                              : Theme.c.androidSheetActionSwitch.background
    border.width: Theme.c.androidSheetActionSwitch.borderWidth
    border.color: switchTrack.checked ? Theme.c.androidSheetActionSwitch.borderChecked
                                      : Theme.c.androidSheetActionSwitch.border

    Behavior on color {
        ColorAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
    }

    Rectangle {
        y: 2
        x: switchTrack.checked ? parent.width - width - 2 : 2
        width: 18
        height: 18
        radius: Theme.radiusPill
        color: switchTrack.checked ? Theme.c.androidSheetActionSwitch.handleChecked : Theme.c.androidSheetActionSwitch.handle

        Behavior on x {
            NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
        }
    }
}
