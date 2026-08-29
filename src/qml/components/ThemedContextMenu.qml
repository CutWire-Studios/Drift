import QtQuick
import QtQuick.Controls.Basic
import Drift

// Themed right-click menu.
//
// The app previously had no context menus at all: right-clicking a clip, track,
// or ruler did nothing, so several actions were reachable only by unlabelled
// keyboard shortcut or not at all. Bookmarks now have a context menu too.
//
// Use with ThemedMenuItem for entries.
Menu {
    id: root

    implicitWidth: 200
    padding: Theme.spacingSm
    // Escape closes; clicking outside dismisses.
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    background: Rectangle {
        radius: Theme.radiusSm
        color: Theme.panelBackground
        border.width: Theme.borderWidth
        border.color: Theme.panelBorder
    }

    // Opacity alone made the menu appear out of nothing; pairing it with a scale
    // gives it presence, matching ThemedDialog. Enter is deliberate, exit snaps.
    // A QML Popup has no transformOrigin, so this scales from the menu's own
    // centre rather than from the click point — close enough at this size.
    enter: Transition {
        ParallelAnimation {
            NumberAnimation {
                property: "opacity"
                from: 0.0
                to: 1.0
                duration: Theme.durationBase
                easing.type: Theme.easing
            }
            NumberAnimation {
                property: "scale"
                from: 0.96
                to: 1.0
                duration: Theme.durationBase
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
                to: 0.96
                duration: Theme.durationFast
                easing.type: Theme.easing
            }
        }
    }

    // Styles Action-based entries. Inline MenuItem children bypass this
    // delegate, so declare those as ThemedMenuItem (same styling) instead.
    delegate: ThemedMenuItem {}
}
