import QtQuick
import QtMultimedia
import Drift

// Audition surface for noise removal. Loaded for one clip: a short window of it is run through the
// model, the original and the result are compared side by side, and only then is the whole clip
// rendered. Hosted by DenoiseWindow and by the Noise Removal panel.
Item {
    id: root

    property int trackIndex: -1
    property int clipIndex: -1
    property real clipDurationSeconds: 0
    property bool showCancel: true

    // Rendered snippets for the A/B. Empty until the first preview lands.
    property string originalPath: ""
    property string cleanPath: ""
    property bool showClean: true

    readonly property real previewSpan: 8
    readonly property bool hasPreview: originalPath !== "" && cleanPath !== ""
    readonly property bool busy: EditorState.denoise.running

    signal cancelRequested()
    signal applied()

    function openFor(track, clip, durationSeconds) {
        root.trackIndex = track
        root.clipIndex = clip
        root.clipDurationSeconds = durationSeconds
        root.originalPath = ""
        root.cleanPath = ""
        root.showClean = true
        windowSlider.value = 0
        EditorState.denoise.preview(track, clip, 0)
    }

    function stopPlayback() {
        player.pause()
    }

    function release() {
        player.stop()
        if (EditorState.denoise.running)
            EditorState.denoise.cancel()
    }

    Connections {
        target: EditorState.denoise
        function onPreviewReady(originalPath, cleanPath) {
            player.stop()
            root.originalPath = originalPath
            root.cleanPath = cleanPath
        }
        function onFinished(ok, message) {
            if (ok)
                root.applied()
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

    Flickable {
        id: scroller
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: footer.top
        clip: true
        contentWidth: width
        contentHeight: content.height + Theme.spacingLg * 2
        boundsBehavior: Flickable.StopAtBounds

        Column {
            id: content
            x: Theme.spacingLg
            y: Theme.spacingLg
            width: scroller.width - Theme.spacingLg * 2
            spacing: Theme.spacingLg

            ThemedLabel {
                width: parent.width
                wrapMode: Text.WordWrap
                text: qsTr("A short section of the clip is previewed here. Confirming runs the whole clip and adds the result as a new audio track above this one — the original is left untouched.")
            }

            Column {
                width: parent.width
                spacing: Theme.spacingMd

                Repeater {
                    model: [
                        { label: qsTr("Original"), path: root.originalPath, clean: false },
                        { label: qsTr("Noise removed"), path: root.cleanPath, clean: true }
                    ]

                    delegate: DenoiseWaveformLane {
                        width: content.width
                        showClean: root.showClean
                        hasPreview: root.hasPreview
                        sourcePlayer: player
                        onPicked: (clean) => { root.showClean = clean }
                    }
                }
            }

            Flow {
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

            Column {
                width: parent.width
                spacing: Theme.spacingSm
                visible: root.clipDurationSeconds > root.previewSpan

                Item {
                    width: parent.width
                    height: fromLabel.height

                    ThemedLabel {
                        id: fromLabel
                        text: qsTr("Preview from")
                    }

                    ThemedLabel {
                        anchors.right: parent.right
                        text: windowSlider.value.toFixed(1) + qsTr("s")
                    }
                }

                ThemedSlider {
                    id: windowSlider
                    label: qsTr("Preview from")
                    width: parent.width
                    from: 0
                    to: Math.max(0.001, root.clipDurationSeconds - root.previewSpan)
                    enabled: !EditorState.denoise.running
                    // Re-rendering on every tick would queue a model run per pixel of drag.
                    onPressedChanged: {
                        if (!pressed)
                            EditorState.denoise.preview(root.trackIndex, root.clipIndex, value)
                    }
                }
            }

            Column {
                width: parent.width
                spacing: Theme.spacingSm
                visible: EditorState.denoise.running

                LabelledProgressRing {
                    anchors.horizontalCenter: parent.horizontalCenter
                    ringSize: 64
                    value: EditorState.denoise.progress
                    indeterminate: EditorState.denoise.progress <= 0
                }

                ThemedLabel {
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    text: EditorState.denoise.status
                }

                ThemedButton {
                    anchors.horizontalCenter: parent.horizontalCenter
                    variant: "destructive"
                    text: qsTr("Cancel")
                    onClicked: EditorState.denoise.cancel()
                }
            }
        }
    }

    Row {
        id: footer
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: Theme.spacingLg
        spacing: Theme.spacingMd

        WorkspaceLink {
            rowHeight: Theme.controlHeight
            target: ({ panel: "recorder" })
        }

        ThemedButton {
            visible: root.showCancel
            variant: "ghost"
            text: qsTr("Cancel")
            onClicked: root.cancelRequested()
        }

        ThemedButton {
            variant: "primary"
            text: qsTr("Remove noise")
            enabled: root.hasPreview && !EditorState.denoise.running
            onClicked: {
                player.stop()
                EditorState.denoise.apply(root.trackIndex, root.clipIndex)
            }
        }
    }
}
