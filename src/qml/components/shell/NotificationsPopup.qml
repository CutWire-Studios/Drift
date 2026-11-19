import QtQuick
import QtQuick.Controls.Basic
import Drift

// The header bell's dropdown. Everything is marked read on close rather than open, so
// the unread dots still say what is new while the list is up.
Popup {
    id: root

    signal updateRequested()

    width: 380
    // Fits the rows; only the empty state keeps a floor so its message has room (#97).
    readonly property real fitHeight: Math.min(480, Toasts.history.count > 0 ? list.implicitHeight
                                                                            : Math.max(160, list.implicitHeight))
    height: fitHeight
    // Clear empties the list while the popup fades out: keep its size until it is gone.
    onAboutToHide: height = height
    onAboutToShow: height = Qt.binding(() => root.fitHeight)
    padding: 0
    modal: true
    dim: false
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    onClosed: Toasts.markAllRead()

    background: Rectangle {
        color: Theme.c.notificationsPopup.background
        border.width: Theme.c.notificationsPopup.borderWidth
        border.color: Theme.c.notificationsPopup.border
        radius: Theme.c.notificationsPopup.radius
    }

    // Opacity plus scale, matching ThemedContextMenu.
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

    contentItem: NotificationList {
        id: list

        onUpdateRequested: {
            root.close()
            root.updateRequested()
        }
        onActionTaken: root.close()
    }
}
