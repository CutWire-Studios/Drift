import QtQuick
import Drift

// Hold-to-lift gesture for the asset browsers' cards: picks the card up into
// TouchDrag, follows the finger, and drops it on the timeline underneath.
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

    // A press that never became a lift. Cards with nothing to offer on tap
    // simply leave this unconnected.
    signal liftTapped()

    anchors.fill: parent
    enabled: Theme.touchUi
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

    onPressed: {
        lifted = false
        suppressTap = false
    }

    onPressAndHold: (mouse) => {
        lifted = true
        suppressTap = true
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

    onClicked: if (!suppressTap) liftArea.liftTapped()
}
