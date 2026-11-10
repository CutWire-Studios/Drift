pragma ComponentBehavior: Bound
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
    // In-window popup: a separate menu window stopped taking clicks on Windows (#473).
    popupType: Popup.Item
    // Non-negative margins make the Popup shift/flip to stay inside the window.
    margins: Theme.spacingSm
    // Capped to the window so a tall menu scrolls (the Menu's ListView turns interactive)
    // instead of running past the bottom edge.
    readonly property real _maxHeight: {
        const avail = Overlay.overlay ? Overlay.overlay.height : 0
        return avail > 0 ? avail - 2 * Theme.spacingSm : Number.POSITIVE_INFINITY
    }
    implicitHeight: Math.min(Math.max(implicitBackgroundHeight + topInset + bottomInset,
                                      implicitContentHeight + topPadding + bottomPadding),
                             _maxHeight)
    // Escape closes; clicking outside dismisses.
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    background: ThemeSlot {
        slot: "ThemedContextMenuBackground"
        control: root
        fallback: Component {
            Rectangle {
                radius: Theme.c.themedContextMenu.radius
                color: Theme.c.themedContextMenu.background
                border.width: Theme.c.themedContextMenu.borderWidth
                border.color: Theme.c.themedContextMenu.border
            }
        }
    }

    onAboutToShow: Haptics.press()

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
