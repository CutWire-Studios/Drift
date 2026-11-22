import QtQuick
import Drift

// Voice waveform of mixed video/audio timeline audio, pinned to a texture-sized window.
Canvas {
    property var lane
    property Item viewItem
    id: voiceWaveform
    readonly property real rangeX0: lane.mediaWaveformRange.start * lane.pxPerSecond
    readonly property real rangeX1: (lane.mediaWaveformRange.start
                                     + lane.mediaWaveformRange.duration) * lane.pxPerSecond
    readonly property real windowX0: Math.max(rangeX0, lane.paintOriginX)
    readonly property real windowX1: Math.min(rangeX1, lane.paintOriginX + viewItem.width
                                                       + 2 * lane.paintChunk)
    x: windowX0
    width: Math.max(2, windowX1 - windowX0)
    height: parent.height
    visible: lane.mediaWaveformRange.duration > 0 && windowX1 > windowX0
    opacity: Theme.c.subtitleCueWaveform.foregroundOpacity
    z: 1
    readonly property color wave: Theme.c.subtitleCueWaveform.foreground
    onWaveChanged: requestPaint()

    readonly property real waveStart: windowX0 / lane.pxPerSecond
    readonly property real waveDuration: Math.max(0, windowX1 - windowX0) / lane.pxPerSecond
    // One peak bucket per screen pixel (rounded to 64) so zoom keeps
    // column resolution instead of stretching a fixed 240-peak list.
    readonly property int peakCount: {
        var w = Math.max(1, Math.ceil(width))
        return Math.min(8192, Math.max(64, Math.ceil(w / 64) * 64))
    }

    // Asking for these mixes every A/V clip on the timeline, so it must not
    // happen until the lane is actually open — bindings evaluate while the
    // lane is hidden too, which meant dropping a long clip paid for the whole
    // mix with nothing on screen to show for it.
    property var peaks: {
        void EditorState.tracksRevision
        void lane.waveformRevision
        return (lane.visible && waveDuration > 0)
            ? EditorState.subtitleWaveformColumns(waveStart, waveDuration, peakCount,
                                                  Math.max(1, Math.floor(width)))
            : []
    }
    onPeaksChanged: requestPaint()
    onWidthChanged: requestPaint()
    onHeightChanged: requestPaint()
    onWaveStartChanged: requestPaint()
    onWaveDurationChanged: requestPaint()
    onPeakCountChanged: requestPaint()

    Connections {
        target: EditorState
        function onSubtitleWaveformReady(s, d, n) {
            if (Math.abs(s - voiceWaveform.waveStart) < 1e-6
                && Math.abs(d - voiceWaveform.waveDuration) < 1e-6
                && n === voiceWaveform.peakCount)
                lane.waveformRevision++
        }
    }

    onPaint: {
        var ctx = getContext("2d");
        ctx.clearRect(0, 0, width, height);
        if (!peaks || peaks.length === 0)
            return;
        // One filled column per screen pixel so bars stay contiguous at any zoom.
        ctx.fillStyle = wave;
        var mid = height / 2;
        for (var x = 0; x < peaks.length; x++) {
            var amp = peaks[x] * mid * 0.9;
            if (amp > 0.5)
                ctx.fillRect(x, mid - amp, 1, amp * 2);
        }
    }
}
