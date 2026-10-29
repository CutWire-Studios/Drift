pragma Singleton
import QtQuick

// The editor main window (Main or AndroidMain), reachable from any window. Panels may live in
// secondary windows, where Window.window is that window and not the one that owns the editor
// functions (configureAndAddAsset, openAddonManager, ...). Null until the shell's
// Component.onCompleted, so bindings must tolerate it.
QtObject {
    property var main: null
    // {panel, tab} asks a panel to switch tab; only the panel named by `panel` applies it. A fresh
    // object each time, so a repeat request still lands; read on creation too, since the panel
    // may not exist yet.
    property var panelRequest: null
    property var keyframesActionHandler: null
    property var maskingActionHandler: null
    property var trackingActionHandler: null
    // Whether the pointer is over the preview panel; masking tool keys are only claimed there.
    property bool previewHovered: false
    // Vertices placed by the polygon tool so far. Escape cancels them before it leaves the tool.
    property int pendingPolygonPoints: 0
    signal maskingCancelRequested()
    // A brush stroke is being dragged; Escape must not drop the tool and its overlay mid-stroke.
    property bool brushStrokeActive: false
    // Armed by the Tracking panel; the tracker overlay consumes the next click.
    property string trackerPlaceMode: ""
    // The tracker an "addPoint" click extends.
    property string trackerPlaceId: ""
    // Shared by the Tracking panel and the tracker overlay, which can both change the selection.
    property string selectedTrackerId: ""
    // "Follow an object" flow: trackers are placed on the host video while the follower clip stays
    // selected. Both clear when the selection moves off the follower.
    property string trackerHostId: ""
    property string trackerFollowerId: ""
    // Where trackers live for the selection. An adjustment layer has none of its own, so it uses
    // the topmost video clip under the playhead and carries the layer's name as viaAdjustment.
    function trackerTarget() {
        if (trackerHostId !== "") {
            const ref = EditorState.tracking.clipRefById(trackerHostId)
            if (ref.track !== undefined)
                return { kind: "clip", track: ref.track, clip: ref.clip, name: ref.name }
        }
        const selected = EditorState.masking.maskTarget()
        if (selected.kind !== "adjustment")
            return selected
        let host = null
        for (const box of EditorState.preview.clipsAtPlayhead()) {
            if (box.kind === "video")
                host = box
        }
        return host ? { kind: "clip", track: host.track, clip: host.clip, name: host.name,
                        viaAdjustment: selected.name || qsTr("Adjustment layer") }
                    : selected
    }
    // The frame trackers work in for trackerTarget().
    function trackerFrame(target) {
        return target.viaAdjustment !== undefined || trackerHostId !== ""
               ? EditorState.preview.trackerFrameOf(target.track, target.clip)
               : EditorState.preview.maskingFrame()
    }

    // Tracking in both directions: backward from the playhead, then forward from where it started.
    property string bothTrackerId: ""
    property string bothClipId: ""
    property real bothStartSeconds: 0
    function trackBoth(clipId, id) {
        bothClipId = clipId
        bothTrackerId = id
        bothStartSeconds = EditorState.playheadSeconds
        EditorState.tracking.track(clipId, id, false, false)
        if (!EditorState.tracking.busy)
            continueBoth()
    }
    function continueBoth() {
        const id = bothTrackerId
        bothTrackerId = ""
        EditorState.playheadSeconds = bothStartSeconds
        EditorState.tracking.track(bothClipId, id, true, false)
    }
    function stopTracking() {
        bothTrackerId = ""
        EditorState.tracking.cancel()
    }
    property Connections trackingWatcher: Connections {
        target: EditorState.tracking
        function onBusyChanged() {
            if (!EditorState.tracking.busy && ShellBridge.bothTrackerId !== "")
                Qt.callLater(ShellBridge.continueBoth)
        }
    }

    property var subtitleActionHandler: null
    property var sourceLabActionHandler: null
    property var sourceLabRangesHandler: null

    // The phone shell (AndroidMain) is the one that hosts AndroidEditor's sheets. Touch sizing
    // alone does not decide this: a tablet can run the desktop shell.
    readonly property bool phoneShell: EditorState.preferences.appliedShell === "mobile"
    // Deep inspector controls ask the phone editor for the easing sheet through here.
    signal easingSheetRequested(int track, int clip, string prop, string label, real seconds)

    // Dialogs the navigator cannot open itself. The header handles them.
    signal dialogRequested(string name, var params)

    // Search targets by id. One id can have several live items, such as the generic Properties
    // panel and a pinned one, so each id maps to a list.
    property var _anchors: ({})
    function registerAnchor(id, item) {
        ShellBridge._anchors[id] = (ShellBridge._anchors[id] ?? []).concat([item])
    }
    function unregisterAnchor(id, item) {
        ShellBridge._anchors[id] = (ShellBridge._anchors[id] ?? []).filter(a => a && a !== item)
    }
    // The last registered item that is on screen. A hidden duplicate does not count.
    function anchorItem(id) {
        const list = ShellBridge._anchors[id] ?? []
        for (let i = list.length - 1; i >= 0; --i) {
            if (ShellBridge._onScreen(list[i]))
                return list[i]
        }
        return null
    }
    function _onScreen(item) {
        if (!item)
            return false
        for (let p = item; p; p = p.parent) {
            if (!p.visible)
                return false
        }
        return true
    }

    // Deep links from global search: action:<id>, workspace:<id>, panel:<type>[#anchor|#tab=<n>[/anchor]],
    // settings:<page>[#anchor], dialog:<name>[?key=value], theme:<light|dark|id>. Anything else is ignored.
    function navigate(target) {
        const colon = target.indexOf(":")
        if (colon < 0)
            return
        const scheme = target.slice(0, colon)
        const rest = target.slice(colon + 1)
        switch (scheme) {
        case "action":
            ShellBridge.main.dispatchAction(rest)
            break
        case "workspace":
            Workspaces.activeId = rest
            break
        case "panel": {
            const [spec, anchorId] = ShellBridge._splitOn(rest, "#")
            ShellBridge._navigatePanel(spec, anchorId)
            break
        }
        case "settings": {
            const [page, anchorId] = ShellBridge._splitOn(rest, "#")
            ShellBridge.main.openSettings(page)
            if (anchorId)
                ShellBridge._seekAnchor(anchorId)
            break
        }
        case "dialog": {
            const [name, query] = ShellBridge._splitOn(rest, "?")
            ShellBridge._openDialog(name, ShellBridge._parseParams(query))
            break
        }
        case "theme":
            if (rest === "light" || rest === "dark")
                Theme.setDarkMode(rest === "dark")
            else if (ThemeManager.select(rest) && ThemeManager.lastWarning)
                Toasts.warning(ThemeManager.lastWarning)
            break
        }
    }

    function _splitOn(text, marker) {
        const at = text.indexOf(marker)
        return at < 0 ? [text, ""] : [text.slice(0, at), text.slice(at + 1)]
    }

    function _parseParams(query) {
        const params = {}
        for (const pair of query.split("&")) {
            if (pair.length === 0)
                continue
            const [key, value] = ShellBridge._splitOn(pair, "=")
            params[key] = value
        }
        return params
    }

    function _openDialog(name, params) {
        const main = ShellBridge.main
        switch (name) {
        case "settings": main.openSettings(); break
        case "extras": main.openExtras(); break
        case "addonManager":
            if (params.kind !== undefined)
                main.openAddonManager(params.kind)
            else
                main.openAddonManager()
            break
        case "debugInfo": main.openDebugInfo(); break
        case "about": main.openAboutDialog(); break
        case "update": main.openUpdateDialog(); break
        case "agentAccess": main.openAgentAccess(); break
        default: ShellBridge.dialogRequested(name, params)
        }
    }

    function _navigatePanel(spec, anchorId) {
        const main = ShellBridge.main
        if (main.previewFullscreen)
            main.togglePreviewFullscreen()
        const pool = main.panelPool
        const dotted = /^(assets|properties)\.(.+)$/.exec(spec)
        const base = dotted ? dotted[1] : spec
        const numbered = /^tab=(\d+)(?:\/(.+))?$/.exec(anchorId)
        const request = numbered ? ({ panel: spec, tab: Number(numbered[1]) })
                                 : (base !== spec ? ({ panel: base, tab: dotted[2] }) : null)
        const seek = numbered ? (numbered[2] ?? "") : anchorId

        if (pool.inLayout(spec)) {
            pool.reveal(spec)
            if (numbered)
                ShellBridge.panelRequest = request
        } else if (base !== spec && pool.inLayout(base)) {
            pool.reveal(base)
            ShellBridge.panelRequest = request
        } else {
            const direct = Workspaces.workspaceWithPanel(spec)
            const hosting = direct || (base !== spec ? Workspaces.workspaceWithPanel(base) : "")
            if (hosting !== "") {
                Workspaces.activeId = hosting
                if (!pool.reveal(spec))
                    pool.reveal(base)
                if (request && (numbered || !direct))
                    ShellBridge.panelRequest = request
            } else {
                main.openFloatingPanel(base)
                if (request)
                    ShellBridge.panelRequest = request
            }
        }
        if (seek !== "")
            ShellBridge._seekAnchor(seek)
    }

    property string _anchorWanted: ""
    property int _anchorPolls: 0
    // Content can be built late (Loaders, lazy panels), so the anchor is polled for about a second.
    property Timer anchorTimer: Timer {
        interval: 50
        repeat: true
        onTriggered: {
            ShellBridge._anchorPolls++
            if (ShellBridge._tryAnchor() || ShellBridge._anchorPolls >= 20)
                stop()
        }
    }
    function _seekAnchor(id) {
        ShellBridge._anchorWanted = id
        ShellBridge._anchorPolls = 0
        if (!ShellBridge._tryAnchor())
            ShellBridge.anchorTimer.restart()
    }
    function _tryAnchor() {
        const item = ShellBridge.anchorItem(ShellBridge._anchorWanted)
        if (item === null)
            return false
        ShellBridge.anchorTimer.stop()
        ShellBridge.revealItem(item)
        return true
    }

    // Scrolls the nearest Flickable so the item sits about a third down, then flashes it.
    function revealItem(item) {
        let flick = item.parent
        while (flick && !("flicking" in flick && "contentY" in flick))
            flick = flick.parent
        if (flick) {
            const top = item.mapToItem(flick.contentItem, 0, 0).y
            const max = flick.originY + Math.max(0, flick.contentHeight - flick.height)
            flick.contentY = Math.min(max, Math.max(flick.originY, top - flick.height / 3))
        }
        ShellBridge.anchorHighlight.createObject(item)
    }

    property Component anchorHighlight: Component {
        Rectangle {
            id: highlight
            anchors.fill: parent
            z: 1000
            color: Theme.c.searchHighlight.background
            radius: Theme.c.searchHighlight.radius
            NumberAnimation on opacity {
                from: 1
                to: 0
                duration: 900
                onStopped: highlight.destroy()
            }
        }
    }
}
