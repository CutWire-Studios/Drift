import QtQuick
import Drift

// The playhead as the timeline should draw it. Playback publishes the playhead once per
// project frame, which at 24 or 30 fps would step the needle visibly on a 60 Hz or faster
// panel. While playing, this extrapolates from the last published position at the playback
// rate on every display frame; otherwise it is the published position unchanged.
QtObject {
    id: root

    property bool active: true
    // Never run further ahead of the last published position than this, so a stall (a slow
    // composite, the end of the timeline) holds the needle instead of letting it wander off.
    readonly property real maxLeadSeconds: 0.1

    property real seconds: EditorState.playheadSeconds

    property real _anchorSeconds: 0
    property real _anchorMs: 0

    readonly property bool _running: active && EditorState.playing

    function _reanchor() {
        _anchorSeconds = EditorState.playheadSeconds
        _anchorMs = Date.now()
        if (!_running)
            seconds = _anchorSeconds
    }

    property Connections _playhead: Connections {
        target: EditorState
        function onPlayheadSecondsChanged() { root._reanchor() }
        function onPlayingChanged() { root._reanchor() }
    }

    property FrameAnimation _frames: FrameAnimation {
        running: root._running
        onTriggered: {
            const rate = EditorState.playback ? EditorState.playback.playbackRate : 1
            const lead = Math.min(root.maxLeadSeconds * rate,
                                  (Date.now() - root._anchorMs) / 1000 * rate)
            root.seconds = root._anchorSeconds + Math.max(0, lead)
        }
    }
}
