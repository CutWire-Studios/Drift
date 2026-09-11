import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Window
import Drift
import "components"

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

    readonly property real collapsedHeight: Math.round(height * sheetHeightFraction)
    // 92% of the screen clears the status bar on a plain device but not one with a
    // cutout, where the sheet header slid underneath it. Clamped against the real inset.
    readonly property real expandedHeight:
        Math.round(Math.min(height * sheetExpandedFraction,
                            height - safeTop - Theme.spacingLg))
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

    Behavior on _asideFade {
        NumberAnimation { duration: Theme.durationBase; easing.type: Theme.easing }
    }

    Connections {
        target: TouchDrag

        function onSceneYChanged() {
            if (!root.opened || root.steppedAside || !TouchDrag.active)
                return
            if (TouchDrag.sceneY >= panel.mapToItem(null, 0, 0).y - Theme.spacingLg)
                return
            root.steppedAside = true
            TouchDrag.clearOfSource = true
        }

        function onActiveChanged() {
            if (TouchDrag.active || !root.opened || !root.steppedAside)
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
        heightAnimation.stop()
        closeAnimation.stop()
        // From zero, not straight to the resting height: the sheet has to be seen to
        // come up from the rail that opened it, or it just materialises over the
        // timeline with no hint of where it came from or how to put it back.
        panelHeight = 0
        animateTo(collapsedHeight)
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

    // Shared vertical-drag tracker used by the header and empty body chrome.
    // Tracks overlay coordinates so moving the panel does not cancel the gesture.
    component SheetDragArea: MouseArea {
        id: dragArea
        property bool stealTouches: false
        property real startGlobalY: 0
        property real startHeight: 0
        property real lastGlobalY: 0
        property real lastT: 0
        property real velocityY: 0

        preventStealing: stealTouches
        acceptedButtons: Qt.LeftButton

        function globalY(mouse) {
            return mapToItem(Overlay.overlay, mouse.x, mouse.y).y
        }

        onPressed: (mouse) => {
            const state = root.beginDrag(globalY(mouse))
            startGlobalY = state.startGlobalY
            startHeight = state.startHeight
            lastGlobalY = startGlobalY
            lastT = Date.now()
            velocityY = 0
        }

        onPositionChanged: (mouse) => {
            if (!pressed)
                return
            const gy = globalY(mouse)
            const now = Date.now()
            const dt = Math.max(1, now - lastT)
            root.updateDrag(startGlobalY, startHeight, gy)
            velocityY = (gy - lastGlobalY) / dt * 1000
            lastGlobalY = gy
            lastT = now
        }

        onReleased: {
            root._dragging = false
            root.finishDrag(root.panelHeight, velocityY)
        }

        onCanceled: {
            root._dragging = false
            root.collapse()
        }
    }

    background: Rectangle {
        visible: root.blocking
        color: Qt.rgba(0, 0, 0, 0.45)
        opacity: {
            if (!root.opened || root.height <= 0)
                return 0
            const span = Math.max(1, root.expandedHeight - root.dismissHeight)
            const t = (root.panelHeight - root.dismissHeight) / span
            return (0.25 + 0.75 * Math.max(0, Math.min(1, t))) * root._asideFade
        }
        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.AllButtons
            hoverEnabled: true
            preventStealing: true
            onClicked: root.dismiss()
        }

        readonly property int _stealHandlers: PointerHandler.CanTakeOverFromHandlersOfSameType
                                            | PointerHandler.CanTakeOverFromHandlersOfDifferentType

        TapHandler {
            acceptedButtons: Qt.AllButtons
            grabPermissions: parent._stealHandlers
        }

        PinchHandler {
            target: null
            grabPermissions: parent._stealHandlers
        }
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

        Rectangle {
            id: panel
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: root.panelHeight
            color: Theme.panelBackground
            radius: Theme.radiusMd

            // Translated rather than resized or hidden while a lifted card is in
            // flight: `height` feeds the drag maths and `visible: false` would drop
            // the grab the gesture is riding on.
            transform: Translate {
                y: (1 - root._asideFade) * (root.panelHeight + root.safeBottom)
            }

            // PointerHandlers (preview pinch, clip taps) hit-test their parent's
            // bounds, not z-order. A sibling MouseArea behind the body never saw
            // empty-chrome presses — they punched through. The grabber has to be
            // an ancestor of header and content; Flickables still steal because
            // stealTouches is false.
            SheetDragArea {
                id: panelGrab
                anchors.fill: parent
                stealTouches: false

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

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: Theme.radiusMd
                color: Theme.panelBackground
            }

            // Prominent drag header: handle + title + one dismissal.
            Rectangle {
                id: header
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                height: Theme.androidSheetHeaderHeight
                color: Theme.panelBackground
                z: 2

                Rectangle {
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.top: parent.top
                    anchors.topMargin: 10
                    width: 44
                    height: 5
                    radius: 2.5
                    color: Theme.sheetHandle
                }

                Text {
                    anchors.left: parent.left
                    // Landscape cutout: the header spans the panel edge to edge, so
                    // without this the title ran under the notch on one rotation and
                    // the Close button under the nav bar on the other.
                    anchors.leftMargin: Theme.pagePadding + root.safeLeft
                    anchors.right: doneBtn.visible ? doneBtn.left : closeBtn.left
                    anchors.rightMargin: Theme.spacingMd
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: 10
                    text: root.title.length > 0 ? root.title : qsTr("Sheet")
                    color: Theme.panelForeground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeBase
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                }

                ThemedButton {
                    id: doneBtn
                    anchors.right: parent.right
                    anchors.rightMargin: Theme.spacingSm + root.safeRight
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: 6
                    visible: root.doneText.length > 0
                    variant: "primary"
                    text: root.doneText
                    onClicked: root.doneRequested()
                }

                // Only when there is no Done. Two dismissals side by side asked the user to
                // pick between them, and on the sheets that have both they do the same thing —
                // Done commits and closes, X closes. Back, a drag down and a tap on the scrim
                // all still dismiss, so nothing becomes unreachable by losing the button.
                IconButton {
                    id: closeBtn
                    visible: !doneBtn.visible
                    anchors.right: parent.right
                    anchors.rightMargin: Theme.spacingSm + root.safeRight
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: 4
                    buttonSize: Theme.androidIconButtonSize
                    iconSize: Theme.iconSizeLg
                    glyph: Theme.icons.x
                    variant: "text"
                    tooltip: qsTr("Close")
                    haptic: "none"
                    onClicked: root.dismiss()
                }

                Rectangle {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    height: Theme.borderWidth
                    color: Theme.panelBorder
                }

                // Whole header is the primary drag affordance (except close).
                SheetDragArea {
                    anchors.fill: parent
                    anchors.rightMargin: closeBtn.width + Theme.spacingSm + root.safeRight
                                         + (doneBtn.visible
                                            ? doneBtn.width + Theme.spacingSm : 0)
                    stealTouches: true
                }
            }

            Item {
                id: contentHost
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: header.bottom
                anchors.bottom: parent.bottom
                // The sheet is anchored to the screen edge, so its last row and its
                // side edges sit under the gesture bar and any landscape cutout.
                anchors.bottomMargin: root.safeBottom
                anchors.leftMargin: root.safeLeft
                anchors.rightMargin: root.safeRight
                clip: true
            }
            }
        }

        // The lifted card, following the finger. It lives out here rather than in
        // the panel so it survives the panel stepping aside — that is the whole
        // point of the gesture: the sheet leaves, the item you picked up does not.
        Item {
            id: dragGhost
            readonly property real ghostSize: Math.round(Theme.assetCardWidth * 0.62)
            visible: root.opened && TouchDrag.active
            z: 100
            width: ghostSize
            height: ghostSize
            // Lifted clear of the fingertip, or the thing being placed is under the
            // hand placing it.
            x: sheetRoot.mapFromItem(null, TouchDrag.sceneX, TouchDrag.sceneY).x - width / 2
            y: sheetRoot.mapFromItem(null, TouchDrag.sceneX, TouchDrag.sceneY).y
               - height - Theme.spacingLg
            scale: visible ? 1 : 0.8

            Behavior on scale {
                NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
            }

            Rectangle {
                anchors.fill: parent
                radius: Theme.radiusSm
                color: Theme.panelAccent
                clip: true
                border.width: TouchDrag.overTarget ? 2 : Theme.borderWidth
                border.color: TouchDrag.overTarget ? Theme.primary : Theme.panelBorder
                opacity: 0.94

                Behavior on border.width {
                    NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
                }
                Behavior on border.color {
                    ColorAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
                }

                Image {
                    id: ghostImage
                    anchors.fill: parent
                    visible: TouchDrag.thumbnail.length > 0 && status === Image.Ready
                    source: TouchDrag.thumbnail.length > 0
                            ? EditorState.imageUrl(TouchDrag.thumbnail) : ""
                    fillMode: Image.PreserveAspectCrop
                    asynchronous: true
                }

                IconGlyph {
                    anchors.centerIn: parent
                    visible: !ghostImage.visible && TouchDrag.glyph.length > 0
                    glyph: TouchDrag.glyph
                    iconSize: Theme.iconSizeXl
                    iconColor: Theme.panelForeground
                }

                Text {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    anchors.margins: Theme.spacingXs
                    visible: !ghostImage.visible && TouchDrag.label.length > 0
                    text: TouchDrag.label
                    color: Theme.panelForeground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeXs
                    horizontalAlignment: Text.AlignHCenter
                    elide: Text.ElideRight
                }
            }
        }
    }
}
