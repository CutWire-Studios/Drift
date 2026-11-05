import QtQuick
import Drift

// Visual host for the Toasts queue. Instantiated once per shell, on top of the
// whole window so any panel's message lands in the same place.
//
// Top of the window, never the bottom: at the bottom it sat on the timeline, the
// one panel that is always being worked in (#289). One toast at a time; older
// ones are in the header bell.
Item {
    id: root

    // Clearance for the shell's header bar, which the toast must not cover.
    property real topMargin: Theme.spacingLg
    // Desktop: top-right corner, clear of the transport. Phone: centered.
    property bool alignRight: true

    // Sits above panel content but must never swallow clicks aimed at the editor.
    anchors.fill: parent
    z: 1000

    readonly property real maxWidth: Math.min(alignRight ? 380 : 460,
                                              root.width - Theme.spacingLg * 2)

    function _accent(severity) {
        switch (severity) {
        case "error": return Theme.c.toastHost.accentError
        case "warning": return Theme.c.toastHost.accentWarning
        case "success": return Theme.c.toastHost.accentSuccess
        default: return Theme.c.toastHost.accent
        }
    }

    function _glyph(severity) {
        switch (severity) {
        case "error": return Theme.icons.error
        case "warning": return Theme.icons.warning
        case "success": return Theme.icons.success
        default: return Theme.icons.info
        }
    }

    Item {
        id: lane
        anchors.top: parent.top
        anchors.topMargin: root.topMargin + Theme.spacingLg
        anchors.right: root.alignRight ? parent.right : undefined
        anchors.rightMargin: Theme.spacingLg
        anchors.horizontalCenter: root.alignRight ? undefined : parent.horizontalCenter
        width: root.maxWidth
        height: 0

        Repeater {
            model: Toasts.model

            delegate: Item {
                id: toast

                required property int index
                required property int toastId
                required property string severity
                required property string message
                required property int repeats
                required property int timeout
                required property bool exiting
                required property string actionText

                readonly property bool front: {
                    void Toasts.revision
                    return Toasts.frontId() === toast.toastId
                }
                readonly property int behind: {
                    void Toasts.revision
                    return toast.front ? Toasts.queuedBehind() : 0
                }
                // Passing info/success get a one-line pill; anything that needs reading
                // or answering keeps the full card.
                readonly property bool compact: (severity === "info" || severity === "success")
                                                && actionText.length === 0

                // A toast queued behind another is never drawn until it reaches the front;
                // once drawn it stays visible through its exit.
                property bool shown: false
                property real enterOffset: -Theme.spacingXl
                property real swipeX: 0
                property real swipeY: 0
                property bool swipedOut: false

                visible: shown
                opacity: 0
                width: compact ? Math.min(root.maxWidth, card.contentWidth) : root.maxWidth
                height: card.height
                x: (root.alignRight ? lane.width - width : (lane.width - width) / 2) + swipeX
                y: enterOffset + Math.min(0, swipeY)

                function _reveal() {
                    if (shown || exiting)
                        return
                    shown = true
                    enterAnimation.start()
                }

                onFrontChanged: if (front) _reveal()
                Component.onCompleted: {
                    if (front)
                        _reveal()
                    switch (toast.severity) {
                    case "error": Haptics.error(); break
                    case "warning": Haptics.confirm(); break
                    case "success": Haptics.success(); break
                    default: Haptics.press(); break
                    }
                }

                // Toasts are the app's only error surface, and had no role at all —
                // so every failure was silent to assistive tech.
                Accessible.role: Accessible.AlertMessage
                Accessible.name: toast.message
                Accessible.description: toast.severity

                ThemeSlot {
                    id: card

                    readonly property real contentWidth: toastRow.implicitWidth
                                                         + Theme.spacingXl + Theme.spacingSm
                                                         + Theme.spacingLg

                    width: parent.width
                    height: Math.max(toast.compact ? Theme.controlHeight + Theme.spacingMd : 0,
                                     toastRow.height + (toast.compact ? Theme.spacingMd
                                                                      : Theme.spacingXl) * 2)
                    slot: "ToastCardBackground"
                    control: toast
                    fallback: Component {
                        Rectangle {
                            anchors.fill: parent
                            radius: toast.compact ? Theme.c.toastHost.radiusCompact
                                                  : Theme.c.toastHost.radius
                            color: Theme.c.toastHost.background
                            border.width: Theme.c.toastHost.borderWidth
                            border.color: Theme.c.toastHost.border

                            // Severity is carried by a leading accent bar rather than a full
                            // color wash, so the message stays readable in both themes. The pill
                            // already says it with its glyph.
                            Rectangle {
                                visible: !toast.compact
                                anchors.left: parent.left
                                anchors.top: parent.top
                                anchors.bottom: parent.bottom
                                anchors.topMargin: Theme.c.toastHost.radius
                                anchors.bottomMargin: Theme.c.toastHost.radius
                                width: Theme.c.toastHost.accentWidth
                                radius: width / 2
                                color: root._accent(toast.severity)
                            }
                        }
                    }

                    Row {
                        id: toastRow
                        z: 1
                        anchors.left: parent.left
                        anchors.leftMargin: toast.compact ? Theme.spacingXl : Theme.spacingXl + Theme.spacingSm
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: Theme.spacingMd

                        readonly property real fixedWidth: Theme.iconSizeBase + dismissButton.width
                                                           + (moreButton.visible ? moreButton.width + spacing : 0)
                                                           + (actionButton.visible ? actionButton.width + spacing : 0)
                                                           + spacing * 2
                        readonly property real availableWidth: root.maxWidth - Theme.spacingXl
                                                               - Theme.spacingSm - Theme.spacingLg

                        IconGlyph {
                            anchors.verticalCenter: parent.verticalCenter
                            glyph: root._glyph(toast.severity)
                            iconSize: Theme.iconSizeBase
                            iconColor: root._accent(toast.severity)
                        }

                        Text {
                            id: messageText
                            anchors.verticalCenter: parent.verticalCenter
                            width: toast.compact
                                   ? Math.min(implicitWidth, toastRow.availableWidth - toastRow.fixedWidth)
                                   : toastRow.availableWidth - toastRow.fixedWidth
                            text: toast.repeats > 1
                                  ? qsTr("%1  (×%2)").arg(toast.message).arg(toast.repeats)
                                  : toast.message
                            color: Theme.c.toastHost.foreground
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSizeSm
                            wrapMode: toast.compact ? Text.NoWrap : Text.WordWrap
                            maximumLineCount: toast.compact ? 1 : 3
                            elide: Text.ElideRight
                        }

                        ThemedButton {
                            id: actionButton
                            anchors.verticalCenter: parent.verticalCenter
                            visible: toast.actionText.length > 0
                            text: toast.actionText
                            variant: "primary"
                            onClicked: Toasts.trigger(toast.toastId)
                        }

                        // How many more are waiting; opens the full list in the bell.
                        ThemedButton {
                            id: moreButton
                            anchors.verticalCenter: parent.verticalCenter
                            visible: toast.behind > 0
                            text: qsTr("+%1").arg(toast.behind)
                            variant: "ghost"
                            tooltip: qsTr("%n more notification(s)", "", toast.behind)
                            onClicked: Toasts.historyRequested()
                        }

                        IconButton {
                            id: dismissButton
                            anchors.verticalCenter: parent.verticalCenter
                            glyph: Theme.icons.x
                            iconSize: Theme.iconSizeMd
                            variant: "ghost"
                            tooltip: qsTr("Dismiss")
                            onClicked: Toasts.acknowledge(toast.toastId)
                        }
                    }

                    // Hovering surfaces the full text, which may be elided.
                    ThemedToolTip {
                        text: toast.message
                        visible: toastHover.hovered && messageText.truncated
                    }

                    HoverHandler { id: toastHover }

                    // Flick sideways (or up, toward the edge it came from) to dismiss.
                    DragHandler {
                        id: swipe
                        target: null
                        enabled: !toast.exiting
                        onTranslationChanged: {
                            toast.swipeX = translation.x
                            toast.swipeY = translation.y
                        }
                        onActiveChanged: {
                            if (active)
                                return
                            const farX = Math.abs(toast.swipeX) > toast.width * 0.3
                                         || Math.abs(centroid.velocity.x) > 800
                            const farY = toast.swipeY < -card.height * 0.5
                                         || centroid.velocity.y < -600
                            if (farX || farY) {
                                toast.swipedOut = true
                                Toasts.acknowledge(toast.toastId)
                            } else {
                                springBack.start()
                            }
                        }
                    }
                }

                // Hovering or holding the toast pauses the countdown; it restarts in full
                // on leave so there is time to finish reading.
                // timeout 0 means "stays until dismissed" (errors).
                Timer {
                    running: toast.front && toast.timeout > 0 && !toast.exiting
                             && !toastHover.hovered && !swipe.active
                    interval: toast.timeout
                    onTriggered: Toasts.dismiss(toast.toastId)
                }

                ParallelAnimation {
                    id: enterAnimation
                    NumberAnimation {
                        target: toast
                        property: "opacity"
                        from: 0
                        to: 1
                        duration: Theme.durationSlow
                        easing.type: Theme.easing
                    }
                    NumberAnimation {
                        target: toast
                        property: "enterOffset"
                        from: -Theme.spacingXl
                        to: 0
                        duration: Theme.durationSlow
                        easing.type: Theme.easing
                    }
                }

                ParallelAnimation {
                    id: springBack
                    NumberAnimation {
                        target: toast
                        property: "swipeX"
                        to: 0
                        duration: Theme.durationBase
                        easing.type: Theme.easing
                    }
                    NumberAnimation {
                        target: toast
                        property: "swipeY"
                        to: 0
                        duration: Theme.durationBase
                        easing.type: Theme.easing
                    }
                }

                // The row is only removed once the exit has finished — otherwise the
                // delegate would be destroyed mid-frame and the toast would just blink
                // out. A swipe carries on in the direction it was thrown; otherwise the
                // toast retreats upward, the arrival played backwards.
                onExitingChanged: {
                    if (!toast.exiting)
                        return
                    enterAnimation.stop()
                    springBack.stop()
                    if (!toast.shown) {
                        Toasts.remove(toast.toastId)
                        return
                    }
                    if (toast.swipedOut && Math.abs(toast.swipeX) >= -Math.min(0, toast.swipeY)) {
                        exitX.to = (toast.swipeX < 0 ? -1 : 1) * (toast.width + Theme.spacingLg)
                        exitY.to = toast.swipeY
                    } else {
                        exitX.to = toast.swipeX
                        exitY.to = Math.min(toast.swipeY, 0) - Theme.spacingXl
                    }
                    exitAnimation.start()
                }

                ParallelAnimation {
                    id: exitAnimation
                    NumberAnimation {
                        target: toast
                        property: "opacity"
                        to: 0
                        duration: Theme.durationBase
                        easing.type: Theme.easing
                    }
                    NumberAnimation {
                        id: exitX
                        target: toast
                        property: "swipeX"
                        duration: Theme.durationBase
                        easing.type: Theme.easing
                    }
                    NumberAnimation {
                        id: exitY
                        target: toast
                        property: "swipeY"
                        duration: Theme.durationBase
                        easing.type: Theme.easing
                    }
                    onFinished: Toasts.remove(toast.toastId)
                }
            }
        }
    }
}
