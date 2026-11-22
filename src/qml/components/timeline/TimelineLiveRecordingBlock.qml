import QtQuick
import Drift

// Live voiceover recording block, waveform, and REC badge on a track.
Rectangle {
    property var panel
    property int trackIndex: -1
    id: liveRecordingBlock
    visible: EditorState.isRecordingAudio && EditorState.recordingTrackIndex === trackIndex
    x: (EditorState.playheadSeconds - EditorState.audioRecordSeconds) * panel.pxPerSecond
    width: Math.max(2, EditorState.audioRecordSeconds * panel.pxPerSecond)
    height: parent.height
    color: Qt.alpha(Theme.c.timelineLiveRecordingBlock.background, Theme.c.timelineLiveRecordingBlock.backgroundOpacity)
    border.color: EditorState.isAudioRecordingPaused ? Theme.c.timelineLiveRecordingBlock.borderPaused : Theme.c.timelineLiveRecordingBlock.border
    border.width: Theme.c.timelineLiveRecordingBlock.borderWidth
    radius: Theme.c.timelineLiveRecordingBlock.radius
    clip: true
    z: 2

    // Real-time live audio waveform canvas
    // Capped and pinned to the newest end: the block grows with the
    // take, and a canvas as wide as a long recording at high zoom
    // would be a huge framebuffer. Draws only the peaks it covers.
    Canvas {
        id: liveWaveCanvas
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.margins: 1
        width: Math.min(Math.max(0, parent.width - 2), 4096)
        visible: parent.width > 4

        Connections {
            target: EditorState
            function onAudioRecordLivePeaksChanged() {
                liveWaveCanvas.requestPaint()
            }
        }
        readonly property color waveformGuide: Theme.c.timelineLiveRecordingBlock.waveformGuide
        readonly property color waveform: Theme.c.timelineLiveRecordingBlock.waveform
        onWaveformGuideChanged: requestPaint()
        onWaveformChanged: requestPaint()
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()

        onPaint: {
            var ctx = getContext("2d");
            ctx.clearRect(0, 0, width, height);
            var total = EditorState.audioRecordLivePeakCount;
            if (total === 0)
                return;

            var mid = height / 2;
            var half = mid * 0.85;
            var w = Math.max(1, Math.floor(width));
            var full = Math.max(w, liveRecordingBlock.width - 2);
            var first = total * (full - w) / full;
            var cols = EditorState.audioRecordLivePeakColumns(first, total - first, w);

            // Centerline guideline
            ctx.strokeStyle = waveformGuide;
            ctx.lineWidth = 1;
            ctx.beginPath();
            ctx.moveTo(0, mid);
            ctx.lineTo(w, mid);
            ctx.stroke();

            // Mirrored waveform fill
            ctx.fillStyle = waveform;
            ctx.beginPath();
            for (var x = 0; x < w; x++) {
                var amp = Math.max(1.0, cols[x] * half);
                if (x === 0) ctx.moveTo(x, mid - amp);
                else ctx.lineTo(x, mid - amp);
                ctx.lineTo(x + 1, mid - amp);
            }
            for (var xb = w - 1; xb >= 0; xb--) {
                var ampB = Math.max(1.0, cols[xb] * half);
                ctx.lineTo(xb + 1, mid + ampB);
                ctx.lineTo(xb, mid + ampB);
            }
            ctx.closePath();
            ctx.fill();
        }
    }

    // Badge tag in upper-left corner of the recording clip
    Rectangle {
        visible: liveRecordingBlock.width > 55
        anchors.left: parent.left
        anchors.leftMargin: 4
        anchors.top: parent.top
        anchors.topMargin: 3
        height: 18
        radius: Theme.c.timelineLiveRecordingBlock.badgeRadius
        color: Theme.c.timelineLiveRecordingBlock.badgeBackground
        border.color: EditorState.isAudioRecordingPaused ? Theme.c.timelineLiveRecordingBlock.borderPaused : Theme.c.timelineLiveRecordingBlock.border
        border.width: Theme.c.timelineLiveRecordingBlock.borderWidth
        width: liveRecRow.implicitWidth + 8

        Row {
            id: liveRecRow
            anchors.centerIn: parent
            spacing: 4

            Rectangle {
                width: 6
                height: 6
                radius: Theme.c.timelineLiveRecordingBlock.dotRadius
                color: EditorState.isAudioRecordingPaused ? Theme.c.timelineLiveRecordingBlock.recordPaused : Theme.c.timelineLiveRecordingBlock.border
                anchors.verticalCenter: parent.verticalCenter
                SequentialAnimation on opacity {
                    running: liveRecordingBlock.visible && !EditorState.isAudioRecordingPaused
                    loops: Animation.Infinite
                    NumberAnimation { to: 0.2; duration: 400 }
                    NumberAnimation { to: 1.0; duration: 400 }
                }
            }

            Text {
                text: (EditorState.isAudioRecordingPaused ? qsTr("PAUSED ") : qsTr("REC "))
                      + EditorState.audioRecordSeconds.toFixed(1) + "s"
                font.pixelSize: 10
                font.bold: true
                color: Theme.c.timelineLiveRecordingBlock.foreground
                anchors.verticalCenter: parent.verticalCenter
            }
        }
    }
}
