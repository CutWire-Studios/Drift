pragma Singleton
import QtQuick

// The editor main window (Main or AndroidMain), reachable from any window. Panels may live in
// secondary windows, where Window.window is that window and not the one that owns the editor
// functions (configureAndAddAsset, openAddonManager, ...). Null until the shell's
// Component.onCompleted, so bindings must tolerate it.
QtObject {
    property var main: null
    // Set to {tab: n} to point the Color panel at a tab. A fresh object each time, so a repeat
    // request still lands; read on creation too, since the panel may not exist yet.
    property var colorTabRequest: null
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
}
