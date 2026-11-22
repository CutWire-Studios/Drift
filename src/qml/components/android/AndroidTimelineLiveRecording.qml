import QtQuick
import Drift

// Live voiceover recording indicator and waveform on this track.
Rectangle {
    id: liveRecordingBlock
    property int trackIndex: -1
    property real pxPerSecond: 1
    visible: EditorState.isRecordingAudio && EditorState.recordingTrackIndex === liveRecordingBlock.trackIndex
    x: (EditorState.playheadSeconds - EditorState.audioRecordSeconds) * liveRecordingBlock.pxPerSecond
    width: Math.max(2, EditorState.audioRecordSeconds * liveRecordingBlock.pxPerSecond)
    height: parent.height
    color: Qt.alpha(Theme.c.androidTimelineLiveRecording.background, Theme.c.androidTimelineLiveRecording.backgroundOpacity)
    border.color: EditorState.isAudioRecordingPaused
                  ? Theme.c.androidTimelineLiveRecording.borderPaused
                  : Theme.c.androidTimelineLiveRecording.border
    border.width: Theme.c.androidTimelineLiveRecording.borderWidth
    radius: Theme.c.androidTimelineLiveRecording.radius
    clip: true
    z: 2

    // Real-time live audio waveform canvas
    Canvas {
        id: liveWaveCanvas
        anchors.fill: parent
        anchors.margins: 1
        visible: parent.width > 4

        Connections {
            target: EditorState
            function onAudioRecordLivePeaksChanged() {
                liveWaveCanvas.requestPaint()
            }
        }
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()

        onPaint: {
            var ctx = getContext("2d");
            ctx.clearRect(0, 0, width, height);
            var n = EditorState.audioRecordLivePeakCount;
            if (n === 0)
                return;

            var mid = height / 2;
            var half = mid * 0.85;
            var w = Math.max(1, Math.floor(width));
            var cols = EditorState.audioRecordLivePeakColumns(0, n, w);

            // Centerline guideline
            ctx.strokeStyle = Theme.c.androidTimelineLiveRecording.waveStroke;
            ctx.lineWidth = Theme.c.androidTimelineLiveRecording.borderWidth;
            ctx.beginPath();
            ctx.moveTo(0, mid);
            ctx.lineTo(w, mid);
            ctx.stroke();

            // Mirrored waveform fill
            ctx.fillStyle = Theme.c.androidTimelineLiveRecording.waveFill;
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
        radius: Theme.c.androidTimelineLiveRecording.badgeRadius
        color: Theme.c.androidTimelineLiveRecording.badgeBackground
        border.color: EditorState.isAudioRecordingPaused
                      ? Theme.c.androidTimelineLiveRecording.borderPaused
                      : Theme.c.androidTimelineLiveRecording.border
        border.width: Theme.c.androidTimelineLiveRecording.borderWidth
        width: liveRecRow.implicitWidth + 8

        Row {
            id: liveRecRow
            anchors.centerIn: parent
            spacing: 4

            Rectangle {
                width: 6
                height: 6
                radius: Theme.c.androidTimelineLiveRecording.indicatorRadius
                color: EditorState.isAudioRecordingPaused
                       ? Theme.c.androidTimelineLiveRecording.indicatorPaused
                       : Theme.c.androidTimelineLiveRecording.indicator
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
                color: Theme.c.androidTimelineLiveRecording.foreground
                anchors.verticalCenter: parent.verticalCenter
            }
        }
    }
}
