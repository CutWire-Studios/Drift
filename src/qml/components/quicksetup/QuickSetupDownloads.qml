import QtQuick
import Drift

QtObject {
    id: root

    readonly property string stickersId: "stickers.harmonyos"

    property int revision: 0
    property var transfers: ({})
    property var requested: ({})
    property var choices: ({})
    property bool failureSeen: false
    property bool optionalActive: false
    property string make: EditorState.preferences.whatYouMake

    readonly property var recommendedIds: {
        switch (make) {
        case "podcasts":
        case "tutorials": return ["whisper.small"]
        case "films": return ["looks.extra"]
        default: return []
        }
    }
    readonly property bool catalogReady: {
        const _r = revision
        return Addons.catalog.length > 0
    }
    readonly property var requiredEntries: {
        const _r = revision
        const out = Addons.quickSetupAddons().filter(e => e.required)
        for (let i = 0; i < out.length; ++i)
            out[i].missing = isMissing(out[i])
        return out
    }
    readonly property var optionalEntries: {
        const _r = revision
        const out = []
        const stickers = Addons.quickSetupAddons().find(e => e.id === stickersId)
        for (const id of recommendedIds)
            out.push(entryFor(id))
        if (stickers)
            out.push(stickers)
        for (let i = 0; i < out.length; ++i)
            out[i].missing = isMissing(out[i])
        return out
    }
    readonly property var wantedEntries: requiredEntries.concat(optionalActive ? optionalEntries.filter(e => isChecked(e)) : [])
    readonly property int doneCount: wantedEntries.filter(e => e.installed).length
    readonly property bool allDone: doneCount === wantedEntries.length
    readonly property bool catalogFailed: !Addons.refreshing && !catalogReady && !allDone
    readonly property bool anyFailed: wantedEntries.some(e => rowState(e) === "failed")
    readonly property bool showWarning: catalogFailed || anyFailed
    readonly property bool busy: !allDone && !showWarning
    readonly property real overall: {
        let sum = 0
        for (const e of wantedEntries) {
            if (e.installed)
                sum += 1
            else if (e.state === "downloading" && transfers[e.id])
                sum += transfers[e.id].fraction * 0.95
            else if (e.state === "installing")
                sum += 0.95
        }
        return wantedEntries.length > 0 ? sum / wantedEntries.length : 1
    }

    function inCatalog(id) {
        const rows = Addons.catalog
        for (let i = 0; i < rows.length; ++i) {
            if (rows[i].id === id)
                return true
        }
        return false
    }

    function isMissing(e) {
        return catalogReady && e.state === "available" && !inCatalog(e.id)
    }

    function entryFor(id) {
        const row = Addons.entry(id)
        const state = row.state || "available"
        return {
            id: id,
            name: row.name || id,
            description: row.description || "",
            sizeBytes: row.downloadSize || 0,
            state: state,
            error: row.error || "",
            installed: state === "installed" || state === "update-available",
            required: false
        }
    }

    function isChecked(e) {
        if (e.installed)
            return true
        if (e.id in choices)
            return choices[e.id]
        return recommendedIds.indexOf(e.id) >= 0
    }

    function rowState(e) {
        if (e.installed)
            return "installed"
        if (e.state === "downloading" || e.state === "installing")
            return e.state
        if (e.state === "failed" || e.state === "needs-newer-app" || e.missing)
            return "failed"
        return "queued"
    }

    function rowError(e) {
        if (e.state === "needs-newer-app")
            return qsTr("Needs a newer version of Drift.")
        if (e.missing)
            return qsTr("Not available in the download store.")
        return e.error.length > 0 ? e.error : qsTr("The download failed.")
    }

    function install(id) {
        const next = requested
        next[id] = true
        requested = next
        Addons.install(id)
    }

    function startMissing() {
        if (!catalogReady)
            return
        for (const e of wantedEntries) {
            if (!e.installed && !e.missing && e.state === "available" && !requested[e.id])
                install(e.id)
        }
    }

    function setChecked(id, on) {
        const next = choices
        next[id] = on
        choices = next
        const e = optionalEntries.find(o => o.id === id)
        if (!e || e.installed)
            return
        if (on) {
            if (catalogReady && !e.missing)
                install(id)
        } else {
            const req = requested
            req[id] = false
            requested = req
            if (e.state === "downloading" || e.state === "installing")
                Addons.cancel(id)
        }
    }

    function retry() {
        const next = requested
        for (const e of wantedEntries) {
            if (rowState(e) === "failed")
                next[e.id] = false
        }
        requested = next
        if (!catalogReady)
            Addons.refresh(true)
        else
            startMissing()
    }

    onRevisionChanged: startMissing()
    onOptionalActiveChanged: startMissing()
    onRecommendedIdsChanged: revision++
    onShowWarningChanged: {
        if (showWarning)
            failureSeen = true
    }

    Component.onCompleted: {
        Addons.refresh()
        revision++
    }

    property Connections addonSignals: Connections {
        target: Addons
        function onCatalogChanged() { root.revision++ }
        function onStatusChanged() { root.revision++ }
        function onRefreshingChanged() { root.revision++ }
        function onProgressChanged(id, fraction, phase) {
            const next = root.transfers
            next[id] = { fraction: fraction, phase: phase }
            root.transfers = next
        }
        function onTransferSucceeded(id) { root.revision++ }
        function onTransferFailed(id, reason) { root.revision++ }
    }
}
