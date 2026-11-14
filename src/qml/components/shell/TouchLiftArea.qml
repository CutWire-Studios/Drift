import QtQuick
import Drift

// Hold-to-lift gesture for the asset browsers' cards: picks the card up into
// TouchDrag, follows the finger, and drops it on the timeline underneath.
//
// Touchscreen presses only. A real mouse press is rejected so it falls through
// to the mouse handlers (TapHandler/DragHandler) beside this on the card; which
// gesture applies follows the device of each press, not Theme.touchUi.
//
// A MouseArea rather than a DragHandler because this drag has to outlive the
// sheet stepping aside — the grab belongs to this item, and it is only still
// there to hold it because the sheet slides away instead of closing.
MouseArea {
    id: liftArea

    // See TouchDrag.kind. `payload` is an asset index or a catalog id. Named
    // dragKind, not kind: the media cards already carry an asset `kind`.
    property string dragKind: ""
    property var payload: null
    property string label: ""
    property string thumbnail: ""
    property string glyph: ""
    // Off where a lift has nowhere to go (inside a modal popup): the card is then tap-only.
    property bool liftEnabled: true

    // A press that never became a lift. Cards with nothing to offer on tap
    // simply leave this unconnected.
    signal liftTapped()
    // A hold on a card that cannot lift.
    signal held()

    anchors.fill: parent
    pressAndHoldInterval: Theme.touchLiftInterval
    // Only once lifted: before that the press still belongs to the grid, which
    // has to be able to scroll out from under a finger that is just resting.
    preventStealing: lifted

    property bool lifted: false
    property bool suppressTap: false

    function _track(mouse) {
        const p = mapToItem(null, mouse.x, mouse.y)
        TouchDrag.moveTo(p.x, p.y)
    }

    onPressed: (mouse) => {
        if (mouse.source === Qt.MouseEventNotSynthesized) {
            mouse.accepted = false
            return
        }
        lifted = false
        suppressTap = false
    }

    onPressAndHold: (mouse) => {
        if (!liftEnabled) {
            Haptics.press()
            liftArea.held()
            return
        }
        lifted = true
        suppressTap = true
        Haptics.pickUp()
        TouchDrag.begin(liftArea.dragKind, liftArea.payload, {
            "label": liftArea.label,
            "thumbnail": liftArea.thumbnail,
            "glyph": liftArea.glyph
        })
        _track(mouse)
    }

    onPositionChanged: (mouse) => {
        if (lifted)
            _track(mouse)
    }

    onReleased: {
        if (!lifted)
            return
        lifted = false
        TouchDrag.finish()
    }

    onCanceled: {
        suppressTap = true
        if (!lifted)
            return
        lifted = false
        TouchDrag.cancel()
    }

    onClicked: if (!suppressTap) {
        Haptics.press()
        liftArea.liftTapped()
    }
}
