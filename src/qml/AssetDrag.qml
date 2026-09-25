pragma Singleton
import QtQuick
import Drift

// Every kind of asset a browser can drag, and the desktop half of dragging one.
//
// The phone lifts cards into TouchDrag; the desktop uses a platform drag (Drag.Automatic), which
// carries a mime type per kind. Both name kinds from the list below, and both drop targets hand
// the kind and payload to AppController::planAssetDrop / dropAsset, so a kind means the same
// thing wherever it lands.
QtObject {
    id: assetDrag

    // media is the asset index as text (and keeps text/plain, which is also how a dropped path
    // arrives); every other kind carries a catalog id.
    readonly property var mimeKeys: ({
        "media": "text/plain",
        "effect": "application/x-drift-effect",
        "audioEffect": "application/x-drift-audio-effect",
        "template": "application/x-drift-effect-template",
        "effectStack": "application/x-drift-effect-stack",
        "transition": "application/x-drift-transition",
        "shape": "application/x-drift-shape",
        "mask": "application/x-drift-mask",
        "sticker": "application/x-drift-sticker",
        "emoji": "application/x-drift-emoji",
        "textStyle": "application/x-drift-text-style",
        "adjustment": "application/x-drift-adjustment"
    })
    readonly property string labelKey: "application/x-drift-asset-label"

    // The drag in flight from this app, if any. A platform drop may not see the mime data until
    // release (Wayland withholds it), so targets read the payload from here first.
    property var current: ({ "kind": "", "payload": "", "label": "" })
    readonly property bool active: current.kind !== ""

    function mimeKey(kind) {
        return mimeKeys[kind] || ""
    }

    function mimeData(kind, payload, label) {
        const data = {}
        data[mimeKey(kind)] = String(payload)
        data[labelKey] = label || ""
        return data
    }

    function allKeys() {
        const keys = []
        for (const kind in mimeKeys)
            keys.push(mimeKeys[kind])
        return keys
    }

    // Keys of the kinds that create a clip rather than act on one.
    function placeableKeys() {
        return ["shape", "sticker", "emoji", "textStyle", "adjustment"].map(k => mimeKeys[k])
    }

    function kindFromKeys(keys) {
        for (const kind in mimeKeys) {
            if (kind !== "media" && keys.indexOf(mimeKeys[kind]) >= 0)
                return kind
        }
        return keys.indexOf("text/plain") >= 0 ? "media" : ""
    }

    function payloadFromDrop(drop, kind) {
        if (current.kind === kind)
            return current.payload
        return drop.getDataAsString(mimeKey(kind))
    }

    function labelFromDrop(drop, kind) {
        if (current.kind === kind)
            return current.label
        return drop.getDataAsString(labelKey)
    }

    function begin(kind, payload, label) {
        current = { "kind": kind, "payload": String(payload), "label": label || "" }
        if (kind === "media")
            EditorState.draggingAssetIndex = Number(payload)
    }

    // Deferred: the drop handler runs after the drag source's active flag drops, and still
    // needs `current` to read the payload.
    function end() {
        Qt.callLater(function() {
            if (assetDrag.current.kind === "media")
                EditorState.draggingAssetIndex = -1
            assetDrag.current = { "kind": "", "payload": "", "label": "" }
        })
    }
}
