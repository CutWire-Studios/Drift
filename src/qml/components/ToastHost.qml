import QtQuick
import QtQuick.Controls.Basic
import Drift

// Visual host for the Toasts queue. Instantiated once, in Main.qml, on top of
// the whole window so any panel's message lands in the same place.
Item {
    id: root

    // Sits above panel content but must never swallow clicks aimed at the editor.
    anchors.fill: parent
    z: 1000

    function _accent(severity) {
        switch (severity) {
        case "error": return Theme.destructive
        case "warning": return Theme.warning
        case "success": return Theme.constructive
        default: return Theme.primary
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

    Column {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: Theme.spacing3xl
        spacing: Theme.spacingLg
        width: Math.min(460, root.width - Theme.spacing3xl * 2)

        Repeater {
            model: Toasts.model

            delegate: Rectangle {
                id: toast

                required property int index
                required property int toastId
                required property string severity
                required property string message
                required property int repeats
                required property int timeout
                required property bool exiting

                width: parent.width
                height: toastRow.implicitHeight + Theme.spacingXl * 2
                radius: Theme.radiusMd
                color: Theme.panelBackground
                border.width: Theme.borderWidth
                border.color: Theme.panelBorder

                // Toasts are the app's only error surface, and had no role at all —
                // so every failure was silent to assistive tech.
                Accessible.role: Accessible.AlertMessage
                Accessible.name: toast.message
                Accessible.description: toast.severity

                // Severity is carried by a leading accent bar rather than a full
                // color wash, so the message stays readable in both themes.
                Rectangle {
                    anchors.left: parent.left
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    width: 3
                    radius: Theme.radiusMd
                    color: root._accent(toast.severity)
                }

                Row {
                    id: toastRow
                    anchors.left: parent.left
                    anchors.leftMargin: Theme.spacingXl + Theme.spacingSm
                    anchors.right: parent.right
                    anchors.rightMargin: Theme.spacingLg
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: Theme.spacingMd

                    IconGlyph {
                        anchors.verticalCenter: parent.verticalCenter
                        glyph: root._glyph(toast.severity)
                        iconSize: Theme.iconSizeBase
                        iconColor: root._accent(toast.severity)
                    }

                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        width: parent.width - parent.spacing * 2
                               - Theme.iconSizeBase - dismissButton.width
                        text: toast.repeats > 1
                              ? qsTr("%1  (×%2)").arg(toast.message).arg(toast.repeats)
                              : toast.message
                        color: Theme.panelForeground
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSizeSm
                        wrapMode: Text.WordWrap
                        maximumLineCount: 3
                        elide: Text.ElideRight
                    }

                    IconButton {
                        id: dismissButton
                        anchors.verticalCenter: parent.verticalCenter
                        glyph: Theme.icons.x
                        iconSize: Theme.iconSizeMd
                        variant: "ghost"
                        tooltip: qsTr("Dismiss")
                        onClicked: Toasts.dismiss(toast.toastId)
                    }
                }

                // timeout 0 means "stays until dismissed" (errors).
                Timer {
                    running: toast.timeout > 0 && !toast.exiting
                    interval: toast.timeout
                    onTriggered: Toasts.dismiss(toast.toastId)
                }

                // Exit is the reverse of the `add:` transition below, so a
                // dismissal reads as the arrival played backwards. It runs as an
                // explicit animation rather than a binding so it does not fight
                // `add:` over the same properties, and the row is only actually
                // removed once it has finished — otherwise the delegate would be
                // destroyed mid-frame and the toast would just blink out.
                onExitingChanged: if (toast.exiting) exitAnimation.start()

                ParallelAnimation {
                    id: exitAnimation
                    NumberAnimation {
                        target: toast
                        property: "opacity"
                        to: 0
                        duration: Theme.durationFast
                        easing.type: Theme.easing
                    }
                    NumberAnimation {
                        target: toast
                        property: "scale"
                        to: 0.96
                        duration: Theme.durationFast
                        easing.type: Theme.easing
                    }
                    onFinished: Toasts.remove(toast.toastId)
                }

                // Hovering pauses nothing but does surface the full text, which
                // may be elided at three lines.
                ThemedToolTip {
                    text: toast.message
                    visible: toastHover.hovered && toast.message.length > 120
                }

                HoverHandler { id: toastHover }
            }
        }

        // Enter and exit share a shape, so a dismissal reads as the reverse of the
        // arrival rather than the toast blinking out of existence. Exit is faster:
        // the entrance is an announcement, the dismissal is just an acknowledgement.
        //
        // `y` is deliberately left to the Column positioner here — driving it from
        // a transition as well fights the layout and jitters.
        add: Transition {
            ParallelAnimation {
                NumberAnimation {
                    properties: "opacity"
                    from: 0
                    to: 1
                    duration: Theme.durationBase
                    easing.type: Theme.easing
                }
                NumberAnimation {
                    properties: "scale"
                    from: 0.96
                    to: 1
                    duration: Theme.durationBase
                    easing.type: Theme.easing
                }
            }
        }

        move: Transition {
            NumberAnimation {
                properties: "y"
                duration: Theme.durationBase
                easing.type: Theme.easingInOut
            }
        }

    }
}
