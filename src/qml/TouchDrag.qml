pragma Singleton
import QtQuick
import Drift

// Lift-and-drop for the phone shell's asset sheet.
//
// Qt's Drag.Automatic is a platform drag: there is no touch gesture that starts
// one on Android, and it cannot cross a modal Popup — which is exactly what the
// asset sheet is. So the browsers lift their cards into this instead. The item
// that started the gesture keeps the touch grab and feeds coordinates in here,
// the sheet steps aside rather than closing (closing would destroy the grab
// mid-flight), and the timeline registers itself as the drop target and does its
// own hit-testing on release.
QtObject {
    id: touchDrag

    // "" while idle. Otherwise one of the kinds listed in AssetDrag.qml — the drop
    // target switches on this.
    property string kind: ""
    // Asset index for "media", a catalog id for every other kind.
    property var payload: null
    property string label: ""
    // Ghost art: an image source when the card has a thumbnail, a glyph otherwise.
    property string thumbnail: ""
    property string glyph: ""

    readonly property bool active: kind !== ""

    // Finger position, in scene coordinates.
    property real sceneX: 0
    property real sceneY: 0

    // Drop targets register here (the timeline, the preview). Each one says whether a
    // scene point is its to take (touchDropContains), and owns its own landing preview
    // and drop; the finger's position picks which one is active.
    property var dropTargets: []
    property var activeTarget: null

    function registerTarget(target) {
        if (dropTargets.indexOf(target) < 0)
            dropTargets = dropTargets.concat([target])
    }

    function unregisterTarget(target) {
        dropTargets = dropTargets.filter(t => t !== target)
        if (activeTarget === target)
            activeTarget = null
    }

    function _targetAt(x, y) {
        for (let i = 0; i < dropTargets.length; ++i) {
            const t = dropTargets[i]
            if (t.visible !== false && t.touchDropContains(x, y))
                return t
        }
        return null
    }
    // True while the finger is over a spot the drop target would accept, so the
    // ghost can say so.
    property bool overTarget: false
    // Set by the sheet once the finger has left it. Until then the drop target is
    // still behind the sheet, and letting go there would land a clip at a spot the
    // sheet is covering — nothing anyone aimed at.
    property bool clearOfSource: false

    function begin(dragKind, dragPayload, opts) {
        const o = opts || {}
        kind = dragKind
        payload = dragPayload
        label = o.label || ""
        thumbnail = o.thumbnail || ""
        glyph = o.glyph || ""
        overTarget = false
        clearOfSource = false
        // The timeline's new-track ghost sizes itself from the dragged asset's
        // track type, which it reads off here exactly as the desktop drop does.
        if (dragKind === "media")
            EditorState.draggingAssetIndex = dragPayload
    }

    function moveTo(x, y) {
        sceneX = x
        sceneY = y
        if (!active)
            return
        // clearOfSource is latched by the sheet from its own sceneY handler, so it
        // is already up to date for this position.
        const target = clearOfSource ? _targetAt(x, y) : null
        if (activeTarget && activeTarget !== target)
            activeTarget.clearTouchDrop()
        activeTarget = target
        overTarget = target ? target.updateTouchDrop(kind, payload, x, y) : false
    }

    function finish() {
        if (!active)
            return
        const target = clearOfSource ? _targetAt(sceneX, sceneY) : null
        if (activeTarget && activeTarget !== target)
            activeTarget.clearTouchDrop()
        if (target)
            target.performTouchDrop(kind, payload, sceneX, sceneY)
        _reset()
    }

    function cancel() {
        if (!active)
            return
        if (activeTarget)
            activeTarget.clearTouchDrop()
        _reset()
    }

    function _reset() {
        kind = ""
        payload = null
        label = ""
        thumbnail = ""
        glyph = ""
        overTarget = false
        clearOfSource = false
        activeTarget = null
        EditorState.draggingAssetIndex = -1
    }
}
