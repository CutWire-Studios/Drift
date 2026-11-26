pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Window
import Drift

// Themed modal dialog chrome. Put page content in `contentItem`; use footer buttons
// via acceptText / rejectText (or set showFooter: false and supply your own footer).
Dialog {
    id: root

    property string acceptText: qsTr("OK")
    property string rejectText: qsTr("Cancel")
    property bool showFooter: true
    property bool showAccept: true
    property bool showReject: true
    property string acceptVariant: "primary"
    property string rejectVariant: "secondary"
    // Extra footer buttons left of Cancel: [{ id, text, variant? }]. A click emits extraClicked(id)
    // and leaves the dialog open; the handler decides whether to close it.
    property var extraButtons: []
    signal extraClicked(string id)
    // Natural width; clamped against the window so the dialog always fits.
    property real preferredWidth: Theme.dialogWidthMd
    // Set false for dialogs where Enter must not commit (destructive confirms).
    property bool acceptOnReturn: true

    // A Popup is not an Item, so SafeArea cannot attach to the dialog; the overlay fills
    // the window and does carry them. Without this a full-height dialog on a phone still
    // ran under the status bar at the top and the gesture pill at the bottom, which is
    // where the footer buttons are.
    // Overlay.overlay is non-null once the dialog is parented, but SafeArea itself is only
    // populated once that overlay is actually attached to a window — briefly undefined for
    // every dialog preloaded at startup, before any window is shown.
    readonly property var _safeMargins: (Overlay.overlay && Overlay.overlay.SafeArea)
                                         ? Overlay.overlay.SafeArea.margins : undefined
    readonly property real safeTop: _safeMargins ? _safeMargins.top : 0
    readonly property real safeBottom: _safeMargins ? _safeMargins.bottom : 0
    readonly property real safeLeft: _safeMargins ? _safeMargins.left : 0
    readonly property real safeRight: _safeMargins ? _safeMargins.right : 0
    // The soft keyboard covers the bottom of the window but is not a system bar, so SafeArea
    // does not report it; whichever is taller is what the dialog has to stay clear of.
    readonly property real clearBottom: Math.max(safeBottom, Theme.keyboardInset)
    readonly property real safeWidth: Math.max(
        0, (Overlay.overlay ? Overlay.overlay.width : preferredWidth) - safeLeft - safeRight)
    readonly property real safeHeight: Math.max(
        0, (Overlay.overlay ? Overlay.overlay.height : 720) - safeTop - clearBottom)

    modal: true
    // A popup positions itself relative to the item it is declared in, so a dialog owned by
    // a panel deep in a scrolled column lands wherever that item is — off-screen, with only
    // the scrim showing. Parent every dialog to the overlay so x/y below mean window space.
    parent: Overlay.overlay
    // Centred in the safe area rather than in the window. Popup.anchors carries `centerIn`
    // and nothing else — no offsets — so the position is computed instead: the insets are
    // rarely equal, and a window-centred dialog at full clamped height still ran under
    // whichever bar was the taller.
    x: Math.round(root.safeLeft + (root.safeWidth - width) / 2)
    y: Math.round(root.safeTop + (root.safeHeight - height) / 2)

    Behavior on y {
        enabled: root.opened
        NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
    }
    standardButtons: Dialog.NoButton
    padding: Theme.spacing2xl

    width: Theme.compact ? root.safeWidth - Theme.dialogMargin
                         : Math.min(preferredWidth, root.safeWidth - Theme.dialogMargin)

    // Height was never clamped, only width. A tall dialog centered in the overlay
    // therefore overflowed both edges, and since the footer is the last thing laid
    // out, its buttons went off-screen with no way to scroll to them — Cancel was
    // reachable only by Escape. Clamping shrinks the content area instead, so the
    // title and the footer buttons always stay on screen.
    height: Math.min(implicitHeight, root.safeHeight - Theme.dialogMargin)

    // Room a contentItem may occupy before the dialog would be clamped. Content
    // that can grow without bound (long option lists, disclosure sections) should
    // put itself in a Flickable capped at this, so it scrolls rather than crops.
    readonly property real availableContentHeight: {
        return Math.max(120, root.safeHeight - Theme.dialogMargin
                             - dialogHeader.implicitHeight - dialogFooter.implicitHeight
                             - topPadding - bottomPadding)
    }

    // Enter/Return commits accept. A Dialog is a Popup, not an Item, so `Keys`
    // cannot attach to it (it silently warned and never fired); a Shortcut is a
    // QObject and works regardless of which control holds focus. Gated on
    // `visible` so only the open dialog's shortcut is live — no cross-dialog
    // ambiguity when several are instantiated. Escape is handled by Popup's
    // closePolicy.
    Shortcut {
        sequences: ["Return", "Enter"]
        enabled: root.visible && root.acceptOnReturn && root.showAccept && acceptButton.enabled
        onActivated: root.accept()
    }

    // Give the dialog focus on open so the key handlers above are live and the
    // default action is visibly selected.
    onOpened: {
        const host = Overlay.overlay ? Overlay.overlay.Window.window : null
        if (host && host.pushOverlayModal)
            host.pushOverlayModal()
        if (showAccept)
            acceptButton.forceActiveFocus()
        else
            root.forceActiveFocus()
        Haptics.detent()
    }

    onClosed: {
        const host = Overlay.overlay ? Overlay.overlay.Window.window : null
        if (host && host.popOverlayModal)
            host.popOverlayModal()
    }

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

    background: ThemeSlot {
        slot: "ThemedDialogBackground"
        control: root
        fallback: Component {
            Rectangle {
                color: Theme.c.themedDialog.background
                border.width: Theme.c.themedDialog.borderWidth
                border.color: Theme.c.themedDialog.border
                radius: Theme.c.themedDialog.radius

                // Attached here rather than on the Dialog: a Dialog is a Popup, not an Item,
                // and Accessible only attaches to Item or Action — the same constraint the
                // Return shortcut above works around.
                Accessible.role: Accessible.Dialog
                Accessible.name: root.title

                MouseArea {
                    anchors.fill: parent
                    acceptedButtons: Qt.AllButtons
                    hoverEnabled: true
                    preventStealing: true
                    onPressed: (mouse) => { mouse.accepted = true }
                    onWheel: (wheel) => { wheel.accepted = true }
                }
            }
        }
    }

    Overlay.modal: Rectangle {
        color: Theme.c.themedDialog.scrim

        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.AllButtons
            hoverEnabled: true
            preventStealing: true
            onPressed: (mouse) => { mouse.accepted = true }
            onWheel: (wheel) => { wheel.accepted = true }
        }
    }

    header: Item {
        id: dialogHeader
        implicitHeight: root.title.length > 0 ? (Theme.touchInput && Theme.compact ? Theme.androidMinTouchTarget : 44) : 0
        width: root.width
        visible: root.title.length > 0

        Text {
            anchors.left: parent.left
            anchors.leftMargin: Theme.spacing2xl
            anchors.right: parent.right
            anchors.rightMargin: Theme.spacing2xl
            anchors.verticalCenter: parent.verticalCenter
            text: root.title
            color: Theme.c.themedDialog.foreground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeBase
            font.weight: Font.Medium
            elide: Text.ElideRight
        }

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: Theme.c.themedDialog.borderWidth
            color: Theme.c.themedDialog.border
        }
    }

    footer: Item {
        id: dialogFooter
        visible: root.showFooter
        readonly property bool phone: Theme.touchInput && Theme.compact
        implicitHeight: visible ? (phone ? Theme.androidMinTouchTarget + 2 * Theme.spacingLg : 56) : 0
        width: root.width

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            height: Theme.c.themedDialog.borderWidth
            color: Theme.c.themedDialog.border
            visible: root.showFooter
        }

        Row {
            id: footerRow
            anchors.right: parent.right
            anchors.rightMargin: Theme.spacing2xl
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.spacingLg
            readonly property int shown: (root.showReject ? 1 : 0) + (root.showAccept ? 1 : 0) + root.extraButtons.length
            readonly property real phoneWidth: shown > 0 ? Math.floor((root.width - 2 * Theme.spacing2xl - (shown - 1) * spacing) / shown) : 0

            Repeater {
                model: root.extraButtons
                delegate: ThemedButton {
                    required property var modelData
                    width: dialogFooter.phone ? footerRow.phoneWidth : implicitWidth
                    height: dialogFooter.phone ? Theme.androidMinTouchTarget : implicitHeight
                    text: modelData.text
                    variant: modelData.variant ?? "secondary"
                    onClicked: root.extraClicked(modelData.id)
                }
            }

            ThemedButton {
                width: dialogFooter.phone ? footerRow.phoneWidth : implicitWidth
                height: dialogFooter.phone ? Theme.androidMinTouchTarget : implicitHeight
                visible: root.showReject
                text: root.rejectText
                variant: root.rejectVariant
                onClicked: root.reject()
            }

            ThemedButton {
                id: acceptButton
                width: dialogFooter.phone ? footerRow.phoneWidth : implicitWidth
                height: dialogFooter.phone ? Theme.androidMinTouchTarget : implicitHeight
                visible: root.showAccept
                text: root.acceptText
                variant: root.acceptVariant
                onClicked: root.accept()
            }
        }
    }
}
