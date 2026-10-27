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
    // Armed by the Tracking panel; the tracker overlay consumes the next click.
    property string trackerPlaceMode: ""
    // The tracker an "addPoint" click extends.
    property string trackerPlaceId: ""
    // Shared by the Tracking panel and the tracker overlay, which can both change the selection.
    property string selectedTrackerId: ""
    property var subtitleActionHandler: null
    property var sourceLabActionHandler: null
    property var sourceLabRangesHandler: null

    // The phone shell (AndroidMain) is the one that hosts AndroidEditor's sheets. Touch sizing
    // alone does not decide this: a tablet can run the desktop shell.
    readonly property bool phoneShell: EditorState.preferences.appliedShell === "mobile"
    // Deep inspector controls ask the phone editor for the easing sheet through here.
    signal easingSheetRequested(int track, int clip, string prop, string label, real seconds)
}
