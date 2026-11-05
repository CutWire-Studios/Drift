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

    // While playing, a publish just behind the extrapolated needle (the clock held for a sink
    // re-sync, then resumed) holds the needle until it is passed instead of stepping it back.
    // Seeks, loops and anything further back than the lead still snap.
    property real _holdSeconds: -1

    function _reanchor(snap) {
        const published = EditorState.playheadSeconds
        const rate = EditorState.playback ? EditorState.playback.playbackRate : 1
        const behind = seconds - published
        _holdSeconds = (!snap && _running && rate > 0 && behind > 0 && behind <= maxLeadSeconds * rate)
                ? seconds : -1
        _anchorSeconds = published
        _anchorMs = Date.now()
        if (!_running)
            seconds = _anchorSeconds
    }

    property Connections _playhead: Connections {
        target: EditorState
        function onPlayheadSecondsChanged() { root._reanchor(false) }
        function onPlayingChanged() { root._reanchor(true) }
    }

    property FrameAnimation _frames: FrameAnimation {
        running: root._running
        onTriggered: {
            const rate = EditorState.playback ? EditorState.playback.playbackRate : 1
            const lead = Math.min(root.maxLeadSeconds * rate,
                                  (Date.now() - root._anchorMs) / 1000 * rate)
            const next = root._anchorSeconds + Math.max(0, lead)
            if (root._holdSeconds >= 0 && next < root._holdSeconds)
                return
            root._holdSeconds = -1
            root.seconds = next
        }
    }
}
