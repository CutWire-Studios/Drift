import QtQuick

// The toast card's input floor (#574). The card used to have only a hover and a swipe handler.
// Handlers in items below still get the press, and the preview's bodyDrag and marqueeDrag are
// their own QML types (they declare properties), so Qt let them take the drag from the swipe and
// the clip moved. Other buttons, the wheel and touch went through to the preview as well.
// This takes every press and wheel over the card; the card's buttons sit above it and keep
// theirs. It also carries swipe-to-dismiss, which may take the drag over from this area, from a
// button the drag started on, or from a handler below, and approves no takeover, so nothing
// underneath can take the drag back.
//
// QtQuick only: tests/tst_toastinput.cpp loads this file on its own.
MouseArea {
    id: shield

    property bool swipeEnabled: true
    readonly property bool swiping: swipeHandler.active

    // The drag's translation so far.
    signal swipeMoved(real dx, real dy)
    // The swipe let go; release velocity in px/s.
    signal swipeReleased(real vx, real vy)

    acceptedButtons: Qt.AllButtons
    // Swipe takes the drag from this area, so stealing stays allowed while it can. Without it,
    // keep the drag here, or the preview's DragHandler would take it.
    preventStealing: !shield.swipeEnabled
    onWheel: (wheel) => { wheel.accepted = true }

    DragHandler {
        id: swipeHandler
        target: null
        enabled: shield.swipeEnabled
        // No ApprovesTakeOver*: once swiping, nothing underneath can take the drag back.
        grabPermissions: PointerHandler.CanTakeOverFromItems
                         | PointerHandler.CanTakeOverFromHandlersOfSameType
                         | PointerHandler.CanTakeOverFromHandlersOfDifferentType
                         | PointerHandler.ApprovesCancellation
        onTranslationChanged: shield.swipeMoved(translation.x, translation.y)
        onActiveChanged: {
            if (!active)
                shield.swipeReleased(centroid.velocity.x, centroid.velocity.y)
        }
    }
}
