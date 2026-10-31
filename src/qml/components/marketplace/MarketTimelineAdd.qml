import QtQuick
import Drift
// "Add to timeline" and "keep in the media bin" for Drift Assets and sound effects. Both install
// through MarketInstall; "use" then puts the resulting bin asset on the timeline when the
// installer reports it ready. justAddedId stays set briefly so the button can say "Added".
QtObject {
    id: root

    property var pending: ({})
    property string justAddedId: ""

    function keyOf(item) {
        return item.source === "sfx" ? item.id : DriftAssets.installKey(item)
    }

    function request(item, mode) {
        const next = Object.assign({}, pending)
        next[keyOf(item)] = { id: item.id, mode: mode || "use" }
        pending = next
        MarketInstall.install(item)
    }

    function finish(key, binAssetId) {
        const entry = pending[key]
        if (entry === undefined)
            return
        const next = Object.assign({}, pending)
        delete next[key]
        pending = next
        if (binAssetId === "")
            return
        if (entry.mode === "keep") {
            AppController.setLastMessage(qsTr("Added to the media bin"), "success")
            return
        }
        AppController.addClipsFromAssets([binAssetId])
        justAddedId = entry.id
        addedTimer.restart()
    }

    function fail(key, message) {
        const next = Object.assign({}, pending)
        delete next[key]
        pending = next
        AppController.setLastMessage(message, "error")
    }

    property Timer addedTimer: Timer {
        interval: 1500
        onTriggered: root.justAddedId = ""
    }

    property Connections assets: Connections {
        target: DriftAssets
        function onReady(key, binAssetId) { root.finish(key, binAssetId) }
        function onFailed(key, message) { if (root.pending[key] !== undefined) root.fail(key, message) }
    }

    property Connections sounds: Connections {
        target: Sfx
        function onReady(id, binAssetId) { root.finish(id, binAssetId) }
        function onFailed(id, message) { if (root.pending[id] !== undefined) root.fail(id, message) }
    }
}
