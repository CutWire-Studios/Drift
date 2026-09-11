import QtQuick
import QtQuick.Window
import QtMultimedia
import Drift 1.0
import "components"

// Audition surface for noise removal. Opened for one clip: a short window of it is run through the
// model, the original and the result are compared side by side, and only then is the whole clip
// rendered. A window rather than a dialog so the timeline stays visible, matching SegmentationWindow.
Window {
    id: root

    property int trackIndex: -1
    property int clipIndex: -1
    property real clipDurationSeconds: 0

    // Rendered snippets for the A/B. Empty until the first preview lands.
    property string originalPath: ""
    property string cleanPath: ""
    property bool showClean: true

    readonly property real previewSpan: 8
    readonly property bool hasPreview: originalPath !== "" && cleanPath !== ""

    width: 760
    height: 560
    minimumWidth: 560
    minimumHeight: 460
    title: qsTr("Remove noise")
    color: Theme.appBackground

    function openFor(track, clip, durationSeconds) {
        root.trackIndex = track
        root.clipIndex = clip
        root.clipDurationSeconds = durationSeconds
        root.originalPath = ""
        root.cleanPath = ""
        root.showClean = true
        windowSlider.value = 0
        root.show()
        root.raise()
        root.requestActivate()
        EditorState.previewDenoise(track, clip, 0)
    }

    // Backgrounding on Android pauses every transport. This window owns its own audition
    // player, which AndroidMain's application-state handler has no other route to; without
    // this the call threw and took the rest of that handler — the recovery flush included —
    // down with it.
    function stopPlayback() {
        player.pause()
    }

    onClosing: {
        player.stop()
        if (EditorState.denoising)
            EditorState.cancelDenoise()
    }

    Connections {
        target: EditorState
        function onDenoisePreviewReady(originalPath, cleanPath) {
            player.stop()
            root.originalPath = originalPath
            root.cleanPath = cleanPath
        }
        function onDenoiseFinished(ok, message) {
            if (ok)
                root.close()
        }
    }

    MediaPlayer {
        id: player
        audioOutput: AudioOutput {}
        // A bare absolute path in a url property would be resolved against this file's directory;
        // the snippets live in the app data cache, so they have to be spelled out as file: URLs.
        source: root.hasPreview
                ? Qt.resolvedUrl("file://" + (root.showClean ? root.cleanPath : root.originalPath))
                : ""
    }

    // Switching A/B mid-audition should land on the same moment in the other take, otherwise the
    // comparison is worthless.
    onShowCleanChanged: {
        if (!root.hasPreview)
            return
        const wasPlaying = player.playbackState === MediaPlayer.PlayingState
        const at = player.position
        player.stop()
        player.position = at
        if (wasPlaying)
            player.play()
    }

    Column {
        id: content
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: footer.top
        anchors.margins: Theme.spacingLg
        spacing: Theme.spacingLg

        ThemedLabel {
            width: parent.width
            wrapMode: Text.WordWrap
            text: qsTr("A short section of the clip is previewed here. Confirming runs the whole clip and adds the result as a new audio track above this one — the original is left untouched.")
        }

        // ----- Before / after --------------------------------------------------------------
        Column {
            width: parent.width
            spacing: Theme.spacingMd

            Repeater {
                model: [
                    { label: qsTr("Original"), path: root.originalPath, clean: false },
                    { label: qsTr("Noise removed"), path: root.cleanPath, clean: true }
                ]

                delegate: Column {
                    id: lane
                    required property var modelData
                    width: content.width
                    spacing: Theme.spacingSm

                    Row {
                        width: parent.width
                        spacing: Theme.spacingMd

                        ThemedLabel {
                            text: lane.modelData.label
                            tone: root.showClean === lane.modelData.clean ? "default" : "muted"
                        }

                        ThemedLabel {
                            visible: root.showClean === lane.modelData.clean && root.hasPreview
                            tone: "muted"
                            size: "sm"
                            text: qsTr("· playing")
                        }
                    }

                    Rectangle {
                        width: parent.width
                        height: 80
                        radius: Theme.radiusMd
                        color: Theme.panelBackground
                        border.width: Theme.borderWidth
                        border.color: root.showClean === lane.modelData.clean
                                      ? Theme.panelSecondaryForeground : Theme.panelBorder

                        SkeletonBox {
                            anchors.fill: parent
                            anchors.margins: Theme.spacingSm
                            visible: !lane.modelData.path
                        }

                        Canvas {
                            id: wave
                            anchors.fill: parent
                            anchors.margins: Theme.spacingSm
                            visible: !!lane.modelData.path

                            property var peaks: lane.modelData.path
                                                ? EditorState.waveformPeaks(lane.modelData.path) : []

                            onPeaksChanged: requestPaint()
                            onWidthChanged: requestPaint()
                            onHeightChanged: requestPaint()

                            // Peaks are computed off-thread, so the first read usually comes back
                            // empty and arrives on this signal instead.
                            Connections {
                                target: EditorState
                                function onWaveformReady(path) {
                                    if (path === lane.modelData.path)
                                        wave.peaks = EditorState.waveformPeaks(path)
                                }
                            }

                            onPaint: {
                                var ctx = getContext("2d");
                                ctx.clearRect(0, 0, width, height);
                                if (!peaks || peaks.length === 0)
                                    return;
                                ctx.fillStyle = Theme.panelWaveformColor;
                                var mid = height / 2;
                                var w = Math.max(1, Math.floor(width));
                                var n = peaks.length;
                                for (var x = 0; x < w; x++) {
                                    var i0 = Math.floor(x * n / w);
                                    var i1 = Math.floor((x + 1) * n / w);
                                    if (i1 <= i0)
                                        i1 = Math.min(n, i0 + 1);
                                    var peak = 0;
                                    for (var i = i0; i < i1; i++) {
                                        if (peaks[i] > peak)
                                            peak = peaks[i];
                                    }
                                    var amp = peak * mid * 0.9;
                                    if (amp > 0.5)
                                        ctx.fillRect(x, mid - amp, 1, amp * 2);
                                }
                            }
                        }

                        // Playhead over whichever take is being auditioned.
                        Rectangle {
                            visible: root.showClean === lane.modelData.clean
                                     && player.duration > 0
                                     && player.playbackState !== MediaPlayer.StoppedState
                            width: 1
                            color: Theme.panelSecondaryForeground
                            anchors.top: parent.top
                            anchors.bottom: parent.bottom
                            x: player.duration > 0
                               ? (player.position / player.duration) * parent.width : 0
                        }

                        MouseArea {
                            anchors.fill: parent
                            enabled: root.hasPreview
                            onClicked: root.showClean = lane.modelData.clean
                        }
                    }
                }
            }
        }

        // ----- Transport + window position -------------------------------------------------
        Row {
            width: parent.width
            spacing: Theme.spacingMd

            ThemedButton {
                variant: "secondary"
                text: player.playbackState === MediaPlayer.PlayingState ? qsTr("Stop") : qsTr("Play")
                enabled: root.hasPreview
                onClicked: player.playbackState === MediaPlayer.PlayingState
                           ? player.stop() : player.play()
            }

            ThemedToggleButton {
                text: qsTr("Original")
                checked: !root.showClean
                enabled: root.hasPreview
                onClicked: root.showClean = false
            }

            ThemedToggleButton {
                text: qsTr("Noise removed")
                checked: root.showClean
                enabled: root.hasPreview
                onClicked: root.showClean = true
            }
        }

        Row {
            width: parent.width
            spacing: Theme.spacingMd
            visible: root.clipDurationSeconds > root.previewSpan

            ThemedLabel {
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("Preview from")
            }

            ThemedSlider {
                id: windowSlider
                label: qsTr("Preview from")
                width: parent.width - 260
                anchors.verticalCenter: parent.verticalCenter
                from: 0
                to: Math.max(0.001, root.clipDurationSeconds - root.previewSpan)
                enabled: !EditorState.denoising
                // Re-rendering on every tick would queue a model run per pixel of drag.
                onPressedChanged: {
                    if (!pressed)
                        EditorState.previewDenoise(root.trackIndex, root.clipIndex, value)
                }
            }

            ThemedLabel {
                anchors.verticalCenter: parent.verticalCenter
                text: windowSlider.value.toFixed(1) + qsTr("s")
            }
        }

        // ----- Progress ---------------------------------------------------------------------
        Column {
            width: parent.width
            spacing: Theme.spacingSm
            visible: EditorState.denoising

            LabelledProgressRing {
                anchors.horizontalCenter: parent.horizontalCenter
                ringSize: 64
                value: EditorState.denoiseProgress
                indeterminate: EditorState.denoiseProgress <= 0
            }

            ThemedLabel {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                text: EditorState.denoiseStatus
            }

            ThemedButton {
                anchors.horizontalCenter: parent.horizontalCenter
                variant: "destructive"
                text: qsTr("Cancel")
                onClicked: EditorState.cancelDenoise()
            }
        }
    }

    // ----- Footer ---------------------------------------------------------------------------
    Row {
        id: footer
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: Theme.spacingLg
        spacing: Theme.spacingMd

        ThemedButton {
            variant: "ghost"
            text: qsTr("Cancel")
            onClicked: root.close()
        }

        ThemedButton {
            variant: "primary"
            text: qsTr("Remove noise")
            enabled: root.hasPreview && !EditorState.denoising
            onClicked: {
                player.stop()
                EditorState.applyDenoise(root.trackIndex, root.clipIndex)
            }
        }
    }
}
