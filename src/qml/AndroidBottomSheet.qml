import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Window
import Drift
// Modal bottom sheet host for Assets / Properties on phone.
// Drag the header (or any non-scrolling empty area) up to expand, down to
// collapse or dismiss.
Popup {
    id: root

    // Children of AndroidBottomSheet land in the sheet panel, not the full-screen scrim.
    default property alias sheetContent: contentHost.data
    property string title: ""
    property real sheetHeightFraction: Theme.androidSheetHeightFraction
    property real sheetExpandedFraction: Theme.androidSheetExpandedFraction
    property bool expanded: false
    // A non-blocking sheet keeps playback running and the preview scrubbable behind it:
    // no scrim, no overlay-modal guard, and taps outside the panel reach the editor. Used
    // by the sheets whose whole job is to show you a change happening — properties and
    // canvas. The browsers stay blocking; a card lifted out of one is a drag the editor
    // underneath must not be able to interrupt.
    property bool blocking: true
    // Labelled commit on editing sheets, beside the X rather than instead of it: dismissing
    // and confirming are different answers, and one button makes you guess which you got.
    property string doneText: ""
    property alias headerActions: header.actions

    signal doneRequested()

    modal: root.blocking
    dim: false
    focus: true
    padding: 0
    margins: 0
    // Scrim / area above the panel dismisses; empty sheet area must not.
    closePolicy: Popup.CloseOnEscape
    parent: Overlay.overlay
    width: Overlay.overlay ? Overlay.overlay.width : 0
    // Stays the full overlay height: the keyboard moves the panel, not this popup, so its
    // appearing never reads as a resize to the re-snap in onHeightChanged.
    height: Overlay.overlay ? Overlay.overlay.height : 0
    x: 0
    y: 0

    // SafeArea attaches to Items, and a Popup is not one, so the insets are read off the
    // sheet's own content item — which fills the overlay and therefore carries them.
    // Same reason as the insets below: a Popup is not an Item, so Window.window does not
    // attach to it either. The overlay is one and carries both.
    readonly property var hostWindow: Overlay.overlay ? Overlay.overlay.Window.window : null

    readonly property real safeTop: sheetRoot.SafeArea.margins.top
    readonly property real safeBottom: sheetRoot.SafeArea.margins.bottom
    readonly property real safeLeft: sheetRoot.SafeArea.margins.left
    readonly property real safeRight: sheetRoot.SafeArea.margins.right

    // The keyboard covers the gesture bar, so while it is up it replaces that inset rather
    // than adding to it. Lift is animated so the panel rides up with the keyboard.
    readonly property real keyboardInset: Theme.keyboardInset
    property real _lift: keyboardInset
    Behavior on _lift {
        NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
    }
    // Room above the keyboard that the panel may occupy.
    readonly property real usableHeight: Math.max(0, height - keyboardInset)

    readonly property real collapsedHeight: Math.round(height * sheetHeightFraction)
    // 92% of the screen clears the status bar on a plain device but not one with a
    // cutout, where the sheet header slid underneath it. Clamped against the real inset.
    readonly property real expandedHeight:
        Math.round(Math.min(height * sheetExpandedFraction,
                            height - safeTop - Theme.spacingLg,
                            usableHeight - Theme.spacingLg))
    readonly property real dismissHeight: Math.round(height * Theme.androidSheetDismissFraction)

    // Source of truth for panel height (drag + snap both write here).
    property real panelHeight: 0
    property bool _dragging: false

    // A card lifted out of this sheet keeps the touch grab of the item that
    // started the gesture, so the sheet must not close while the drag is live —
    // closing takes that item off screen and the drag dies with it. It slides out
    // of the way instead, and closes for real when the finger lifts. Latched: the
    // panel moves as it goes, so re-testing the pointer against it would flap.
    property bool steppedAside: false
    property real _asideFade: steppedAside ? 0 : 1
    // Where the finger was when the card was lifted, -1 until the first move reports it.
    property real _liftStartY: -1

    Behavior on _asideFade {
        NumberAnimation { duration: Theme.durationBase; easing.type: Theme.easing }
    }

    Connections {
        target: TouchDrag

        function onSceneYChanged() {
            if (!root.opened || root.steppedAside || !TouchDrag.active)
                return
            if (root._liftStartY < 0)
                root._liftStartY = TouchDrag.sceneY
            // Either the finger has left the sheet, or it has clearly set off upwards — towards
            // the timeline and the preview, the only places a card can go. Waiting for it to
            // clear the sheet's top edge meant dragging over the very targets the sheet hid.
            const aboveSheet = TouchDrag.sceneY < panel.mapToItem(null, 0, 0).y - Theme.spacingLg
            const pulledUp = root._liftStartY - TouchDrag.sceneY > Theme.touchLiftStepAsideDistance
            if (!aboveSheet && !pulledUp)
                return
            root.steppedAside = true
            TouchDrag.clearOfSource = true
        }

        function onActiveChanged() {
            if (TouchDrag.active) {
                if (root.opened) {
                    root._liftStartY = -1
                    // An expanded sheet covers the preview entirely; back to its resting size
                    // so there is somewhere to drop.
                    if (root.expanded)
                        root.collapse()
                }
                return
            }
            if (!root.opened || !root.steppedAside)
                return
            // callLater, not close(): the drag ended inside the released handler of
            // an item this popup owns, and tearing it down under its own event is
            // the one way to lose the drop.
            Qt.callLater(root.close)
        }
    }

    // Unlatched only once the popup is gone, so the panel does not slide back up
    // through the exit transition on its way out.
    Connections {
        target: root
        function onClosed() { root.steppedAside = false }
    }

    onOpened: {
        const host = root.hostWindow
        // The guard exists to stop taps on empty dialog chrome reaching the timeline's
        // PointerHandlers. A non-blocking sheet wants exactly the opposite.
        if (root.blocking && host && host.pushOverlayModal)
            host.pushOverlayModal()
        Haptics.press()
    }

    onClosed: {
        const host = root.hostWindow
        if (root.blocking && host && host.popOverlayModal)
            host.popOverlayModal()
    }

    function expand() {
        expanded = true
        Haptics.detent()
        animateTo(expandedHeight)
    }

    function collapse() {
        expanded = false
        Haptics.detent()
        animateTo(collapsedHeight)
    }

    function animateTo(h) {
        heightAnimation.stop()
        heightAnimation.from = root.panelHeight
        heightAnimation.to = Math.max(0, h)
        heightAnimation.start()
    }

    // Slide out, then hide. Popup.close() takes the panel off screen in the frame it
    // is called, so every dismissal — close button, scrim tap, Back, switching
    // sheets — used to blink rather than move, which read as a glitch on the one
    // surface the phone shell opens most. Every in-app path goes through here;
    // closePolicy still calls close() directly, and the exit transition below keeps
    // even that from snapping.
    function dismiss() {
        if (!opened) {
            close()
            return
        }
        Haptics.drop()
        closeAnimation.stop()
        closeAnimation.from = root.panelHeight
        closeAnimation.start()
    }

    function beginDrag(globalY) {
        heightAnimation.stop()
        root._dragging = true
        Haptics.pickUp()
        return { startGlobalY: globalY, startHeight: root.panelHeight }
    }

    function updateDrag(startGlobalY, startHeight, globalY) {
        const dy = globalY - startGlobalY
        const next = startHeight - dy
        const lo = Math.round(root.height * 0.12)
        const hi = root.expandedHeight
        root.panelHeight = Math.max(lo, Math.min(hi, next))
    }

    function finishDrag(finalHeight, velocityY) {
        // Snap by resting height first. Velocity only tips the decision when the
        // sheet is already clearly moving toward a neighbour state — a fast drag
        // alone must not dismiss (that was closing on quick expand/collapse).
        const mid = (collapsedHeight + expandedHeight) * 0.5
        const nearCollapsed = finalHeight <= collapsedHeight * 1.08
        const clearlyBelowCollapsed = finalHeight < dismissHeight
        const flungDown = velocityY > 1600
        const flungUp = velocityY < -1600

        if (clearlyBelowCollapsed)
            return root.dismiss()
        // Downward fling only dismisses when already at/under the collapsed size.
        if (flungDown && nearCollapsed && finalHeight < collapsedHeight * 0.95)
            return root.dismiss()

        if (flungUp || finalHeight >= mid)
            return expand()

        collapse()
    }

    onAboutToShow: {
        expanded = false
        _dragging = false
        steppedAside = false
        _detentBeforeKeyboard = null
        heightAnimation.stop()
        closeAnimation.stop()
        // From zero, not straight to the resting height: the sheet has to be seen to
        // come up from the rail that opened it, or it just materialises over the
        // timeline with no hint of where it came from or how to put it back.
        panelHeight = 0
        animateTo(collapsedHeight)
    }

    // The panel's resting detent from before the keyboard came up, null while it is not up.
    property var _detentBeforeKeyboard: null

    // The panel sits above the keyboard, so a field in a collapsed sheet would be a sliver
    // of room; take the expanded detent while typing and put it back afterwards.
    onKeyboardInsetChanged: {
        if (!opened)
            return
        if (keyboardInset > 0) {
            const item = root.contentItem && root.contentItem.Window.window
                         ? root.contentItem.Window.window.activeFocusItem : null
            let inside = false
            for (let p = item; p; p = p.parent) {
                if (p === contentHost) {
                    inside = true
                    break
                }
            }
            if (inside && root._detentBeforeKeyboard === null) {
                root._detentBeforeKeyboard = root.expanded
                if (!root.expanded)
                    root.expand()
            }
        } else if (root._detentBeforeKeyboard !== null) {
            const wasExpanded = root._detentBeforeKeyboard
            root._detentBeforeKeyboard = null
            if (!wasExpanded && root.expanded)
                root.collapse()
        }
    }

    onHeightChanged: {
        if (!opened || _dragging || heightAnimation.running)
            return
        panelHeight = expanded ? expandedHeight : collapsedHeight
    }

    // panelHeight is written imperatively, so a sheet whose resting height is derived
    // from its own content (AndroidAddMenu) would keep whatever height it was given
    // before that content existed. Re-snap when the target moves under us.
    onCollapsedHeightChanged: {
        if (!opened || _dragging || expanded || closeAnimation.running)
            return
        animateTo(collapsedHeight)
    }

    NumberAnimation {
        id: heightAnimation
        target: root
        property: "panelHeight"
        duration: Theme.durationBase + 40
        easing.type: Theme.easing
    }

    // Exit is shorter than the entrance: arriving is an announcement, leaving is an
    // acknowledgement, and a dismissal that takes as long as the reveal reads as lag.
    NumberAnimation {
        id: closeAnimation
        target: root
        property: "panelHeight"
        to: 0
        duration: Math.round((Theme.durationBase + 40) * 0.65)
        easing.type: Theme.easing
        onFinished: root.close()
    }

    // Backstop for the paths that reach close() without dismiss() — closePolicy's
    // Escape, and anything Qt closes on our behalf.
    exit: Transition {
        NumberAnimation {
            property: "opacity"
            from: 1.0
            to: 0.0
            duration: Theme.durationFast
            easing.type: Theme.easing
        }
    }

    background: AndroidSheetScrim {
        sheet: root
    }

    contentItem: Item {
        id: sheetRoot
        anchors.fill: parent

        MouseArea {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.bottom: panel.top
            visible: root.blocking
            acceptedButtons: Qt.AllButtons
            hoverEnabled: true
            onClicked: root.dismiss()
        }

        Item {
            id: panel
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.bottomMargin: root._lift
            height: Math.min(root.panelHeight, root.usableHeight)

            // Translated rather than resized or hidden while a lifted card is in
            // flight: `height` feeds the drag maths and `visible: false` would drop
            // the grab the gesture is riding on.
            transform: Translate {
                y: (1 - root._asideFade) * (root.panelHeight + root.safeBottom)
            }

            ThemeSlot {
                anchors.fill: parent
                slot: "AndroidSheetBackground"
                control: root
                fallback: Component {
                    Item {
                        Rectangle {
                            anchors.fill: parent
                            color: Theme.c.androidBottomSheet.background
                            radius: Theme.c.androidBottomSheet.radius
                        }
                        Rectangle {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.bottom: parent.bottom
                            height: Theme.c.androidBottomSheet.radius
                            color: Theme.c.androidBottomSheet.background
                        }
                    }
                }
            }

            // PointerHandlers (preview pinch, clip taps) hit-test their parent's
            // bounds, not z-order. A sibling MouseArea behind the body never saw
            // empty-chrome presses — they punched through. The grabber has to be
            // an ancestor of header and content; Flickables still steal because
            // stealTouches is false.
            AndroidSheetDragArea {
                id: panelGrab
                anchors.fill: parent
                stealTouches: false
                sheet: root

                TapHandler {
                    acceptedButtons: Qt.AllButtons
                    grabPermissions: PointerHandler.CanTakeOverFromHandlersOfSameType
                                     | PointerHandler.CanTakeOverFromHandlersOfDifferentType
                }

                PinchHandler {
                    target: null
                    grabPermissions: PointerHandler.CanTakeOverFromHandlersOfSameType
                                     | PointerHandler.CanTakeOverFromHandlersOfDifferentType
                }

            AndroidSheetHeader {
                id: header
                title: root.title
                doneText: root.doneText
                safeLeft: root.safeLeft
                safeRight: root.safeRight
                sheet: root
            }

            Item {
                id: contentHost
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: header.bottom
                anchors.bottom: parent.bottom
                // The sheet is anchored to the screen edge, so its last row and its
                // side edges sit under the gesture bar and any landscape cutout.
                anchors.bottomMargin: root.keyboardInset > 0 ? 0 : root.safeBottom
                anchors.leftMargin: root.safeLeft
                anchors.rightMargin: root.safeRight
                clip: true
            }
            }
        }

        // The lifted card, following the finger. It lives out here rather than in
        // the panel so it survives the panel stepping aside — that is the whole
        // point of the gesture: the sheet leaves, the item you picked up does not.
        TouchDragGhost {
            visible: root.opened && TouchDrag.active
            z: 100
        }
    }
}
