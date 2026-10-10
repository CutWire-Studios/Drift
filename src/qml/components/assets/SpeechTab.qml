import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Window
import QtMultimedia
import Drift
import ".."

// Speech tab: local text-to-speech with optional voice cloning, placed at the playhead.
Item {
    id: root

    readonly property bool ready: EditorState.tts.available
    property bool runtimeReady: Addons.runtimeAvailable()
    property string resultText: ""
    property bool resultOk: true

    readonly property string clipKind: {
        const data = EditorState.selectedClipData
        return (data && data.kind) ? data.kind : ""
    }
    readonly property bool clipUsable: clipKind === "video" || clipKind === "audio"
    readonly property bool busy: EditorState.tts.running || EditorState.voices.encoding
    readonly property bool canGenerate: ready && !busy && scriptField.text.trim().length > 0

    function selectVoice(id) {
        const row = EditorState.voices.indexOfId(id)
        voiceBox.currentIndex = row >= 0 ? row : 0
    }

    Component.onCompleted: {
        if (root.ready)
            EditorState.tts.warmUp()
    }

    Connections {
        target: EditorState.tts
        function onAvailableChanged() {
            if (EditorState.tts.available)
                EditorState.tts.warmUp()
        }
        function onFinished(ok, message) {
            root.resultOk = ok
            root.resultText = message
        }
    }

    Connections {
        target: EditorState.voices
        function onVoiceAdded(id) {
            root.selectVoice(id)
            root.resultOk = true
            root.resultText = qsTr("Voice saved")
        }
        function onError(message) {
            root.resultOk = false
            root.resultText = message
        }
    }

    Connections {
        target: Addons
        function onKindChanged(kind) {
            if (kind === "onnxruntime")
                root.runtimeReady = Addons.runtimeAvailable()
        }
    }

    MediaPlayer {
        id: voicePlayer
        audioOutput: AudioOutput {}
    }

    Flickable {
        anchors.fill: parent
        contentWidth: width
        contentHeight: speechColumn.height + Theme.spacing3xl
        clip: true
        ScrollBar.vertical: AppScrollBar { }

        Column {
            id: speechColumn
            x: Theme.pagePadding
            width: parent.width - Theme.pagePadding * 2
            spacing: Theme.spacingLg
            topPadding: Theme.pagePadding

            readonly property real contentWidth: width

            Text {
                width: speechColumn.contentWidth
                wrapMode: Text.WordWrap
                text: root.ready
                      ? qsTr("Turn a script into speech with a built-in or cloned voice. It is generated on this device and added at the playhead.")
                      : qsTr("Speech generation runs on this device and needs the voice model.")
                color: Theme.mutedForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
            }

            ThemedButton {
                visible: !root.ready
                width: speechColumn.contentWidth
                text: root.runtimeReady
                      ? qsTr("Download voice model")
                      : qsTr("Install AI engine first")
                variant: "primary"
                glyph: Theme.icons.download
                tooltip: qsTr("Needed to generate speech")
                onClicked: root.Window.window.openAddonManager(
                    root.runtimeReady ? "tts-model" : "onnxruntime")
            }

            Column {
                visible: root.ready
                width: speechColumn.contentWidth
                spacing: Theme.spacingLg

                ThemedTextArea {
                    id: scriptField
                    width: parent.width
                    height: 140
                    enabled: !EditorState.tts.running
                    placeholderText: qsTr("Type what the voice should say")
                    placeholderTextColor: Theme.mutedForeground
                }

                Text {
                    width: parent.width
                    text: qsTr("%1 characters, about %2 s").arg(scriptField.text.length)
                          .arg(Math.ceil(scriptField.text.length / 14))
                    color: Theme.mutedForeground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeXs
                }

                ThemedLabel {
                    text: qsTr("Voice")
                    tone: "default"
                    size: "sm"
                }

                Row {
                    width: parent.width
                    spacing: Theme.spacingSm

                    ThemedComboBox {
                        id: voiceBox
                        width: parent.width - previewButton.width - moreButton.width - parent.spacing * 2
                        anchors.verticalCenter: parent.verticalCenter
                        enabled: !root.busy
                        model: EditorState.voices
                        textRole: "name"
                        valueRole: "id"
                        Component.onCompleted: root.selectVoice(EditorState.tts.lastVoice)
                    }

                    IconButton {
                        id: previewButton
                        anchors.verticalCenter: parent.verticalCenter
                        readonly property bool playing: voicePlayer.playbackState === MediaPlayer.PlayingState
                        glyph: playing ? Theme.icons.pause : Theme.icons.play
                        tooltip: playing ? qsTr("Stop") : qsTr("Play the voice sample")
                        onClicked: {
                            if (playing) {
                                voicePlayer.stop()
                                return
                            }
                            voicePlayer.source = EditorState.voices.referenceUrl(voiceBox.currentValue)
                            voicePlayer.play()
                        }
                    }

                    IconButton {
                        id: moreButton
                        anchors.verticalCenter: parent.verticalCenter
                        glyph: Theme.icons.ellipsis
                        tooltip: qsTr("Rename or delete this voice")
                        enabled: voiceBox.currentIndex > 0 && !root.busy
                        onClicked: voiceMenu.popup()

                        ThemedContextMenu {
                            id: voiceMenu
                            ThemedMenuItem {
                                text: qsTr("Rename…")
                                icon.name: Theme.icons.pencil
                                onTriggered: renameDialog.openWith(qsTr("Rename voice"), voiceBox.currentText)
                            }
                            ThemedMenuItem {
                                text: qsTr("Delete")
                                icon.name: Theme.icons.trash
                                onTriggered: {
                                    voicePlayer.stop()
                                    EditorState.voices.remove(voiceBox.currentValue)
                                    voiceBox.currentIndex = 0
                                }
                            }
                        }
                    }
                }

                ThemedButton {
                    id: newVoiceButton
                    width: parent.width
                    text: qsTr("New voice")
                    variant: "secondary"
                    glyph: Theme.icons.plus
                    enabled: !root.busy
                    tooltip: qsTr("Clone a voice from a recording")
                    onClicked: newVoiceMenu.popup()

                    ThemedContextMenu {
                        id: newVoiceMenu
                        ThemedMenuItem {
                            text: qsTr("From selected clip")
                            enabled: root.clipUsable
                            onTriggered: voiceDialog.openForClip()
                        }
                        ThemedMenuItem {
                            text: qsTr("Record…")
                            icon.name: Theme.icons.mic
                            onTriggered: voiceDialog.openForMic()
                        }
                        ThemedMenuItem {
                            text: qsTr("Import file…")
                            icon.name: Theme.icons.upload
                            onTriggered: voiceDialog.openForFile()
                        }
                    }
                }

                Text {
                    visible: EditorState.voices.encoding
                    width: parent.width
                    wrapMode: Text.WordWrap
                    text: EditorState.tts.loading
                          ? qsTr("Loading voice model (first use takes a while)…")
                          : qsTr("Learning the voice…")
                    color: Theme.mutedForeground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeXs
                }

                ThemedLabel {
                    text: qsTr("Language")
                    tone: "default"
                    size: "sm"
                }

                ThemedComboBox {
                    id: langBox
                    width: parent.width
                    enabled: !EditorState.tts.running
                    textRole: "label"
                    valueRole: "code"
                    model: EditorState.tts.languageOptions()
                    Component.onCompleted: currentIndex = Math.max(0, indexOfValue(EditorState.tts.lastLanguage))
                }

                ThemedLabel {
                    text: qsTr("Expressiveness")
                    tone: "default"
                    size: "sm"
                }

                ThemedSlider {
                    id: exaggerationSlider
                    width: parent.width
                    label: qsTr("Expressiveness")
                    from: 0.25
                    to: 2.0
                    stepSize: 0.05
                    value: EditorState.tts.lastExaggeration
                    enabled: !EditorState.tts.running
                }

                Text {
                    width: parent.width
                    wrapMode: Text.WordWrap
                    text: qsTr("Higher values sound more dramatic and speak faster")
                    color: Theme.mutedForeground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeXs
                }

                ThemedButton {
                    visible: !EditorState.tts.running
                    width: parent.width
                    text: qsTr("Generate speech")
                    variant: "primary"
                    glyph: Theme.icons.audioLines
                    enabled: root.canGenerate
                    tooltip: qsTr("Generate speech and add it at the playhead")
                    onClicked: {
                        root.resultText = ""
                        voicePlayer.stop()
                        EditorState.tts.generate(scriptField.text, voiceBox.currentValue,
                                                 langBox.currentValue, exaggerationSlider.value)
                    }
                }

                Row {
                    visible: EditorState.tts.loading && !EditorState.tts.running
                    width: parent.width
                    spacing: Theme.spacingMd

                    IconGlyph {
                        anchors.verticalCenter: parent.verticalCenter
                        glyph: Theme.icons.spinner
                        spinning: true
                    }

                    Text {
                        width: parent.width - Theme.iconSizeBase - parent.spacing
                        anchors.verticalCenter: parent.verticalCenter
                        wrapMode: Text.WordWrap
                        text: qsTr("Loading voice model (first use takes a while)…")
                        color: Theme.mutedForeground
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSizeXs
                    }
                }

                Column {
                    visible: EditorState.tts.running
                    width: parent.width
                    spacing: Theme.spacingMd

                    Text {
                        width: parent.width
                        text: EditorState.tts.loading
                              ? qsTr("Loading voice model (first use takes a while)…")
                              : EditorState.tts.status
                        color: Theme.panelForeground
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSizeXs
                        elide: Text.ElideRight
                    }

                    ThemedProgressBar {
                        width: parent.width
                        value: EditorState.tts.progress
                        opacity: EditorState.tts.loading ? 0.4 : 1
                    }

                    ThemedButton {
                        text: qsTr("Cancel")
                        variant: "destructive"
                        glyph: Theme.icons.x
                        tooltip: qsTr("Stop generating")
                        onClicked: EditorState.tts.cancel()
                    }
                }

                Text {
                    visible: root.resultText.length > 0 && !EditorState.tts.running
                    width: parent.width
                    wrapMode: Text.WordWrap
                    text: root.resultText
                    color: root.resultOk ? Theme.mutedForeground : Theme.destructive
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeXs
                }
            }
        }
    }

    NameDialog {
        id: renameDialog
        onSubmitted: name => EditorState.voices.rename(voiceBox.currentValue, name)
    }

    ThemedDialog {
        id: voiceDialog

        property string mode: "clip"
        property string sourcePath: ""
        property real sourceIn: 0
        property real sourceOut: 0
        property real offset: 0
        readonly property real range: Math.max(0, sourceOut - sourceIn)
        readonly property bool recording: EditorState.voices.recording
        readonly property bool saveReady: nameField.text.trim().length > 0 && rightsBox.checked
                                         && (mode !== "mic"
                                             || (recording && EditorState.voices.recordingSeconds >= 3))

        title: mode === "mic" ? qsTr("Record a voice")
                              : (mode === "file" ? qsTr("Import a voice") : qsTr("Voice from clip"))
        preferredWidth: Theme.dialogWidthMd
        acceptText: qsTr("Save")
        showAccept: saveReady

        function reset(defaultName) {
            nameField.text = defaultName
            rightsBox.checked = false
            offset = 0
            open()
            nameField.forceActiveFocus()
            nameField.selectAll()
        }

        function openForClip() {
            const data = EditorState.selectedClipData
            mode = "clip"
            sourcePath = data.path
            sourceIn = data.inPoint
            sourceOut = data.outPoint
            reset(data.name || qsTr("My voice"))
        }

        function openForMic() {
            mode = "mic"
            reset(qsTr("My voice"))
        }

        function openForFile() {
            const url = FileDialogs.openFile(qsTr("Import a voice sample"),
                                             [qsTr("Audio files (*.wav *.mp3 *.flac *.ogg *.m4a *.aac *.opus)"),
                                              qsTr("All files (*)")])
            if (url == "")
                return
            mode = "file"
            sourcePath = url.toString()
            reset(decodeURIComponent(sourcePath.split("/").pop().replace(/\.[^.]*$/, "")))
        }

        onAccepted: {
            const name = nameField.text.trim()
            if (mode === "mic")
                EditorState.voices.stopRecordingAndAdd(name)
            else if (mode === "file")
                EditorState.voices.addFromFile(sourcePath, name)
            else
                EditorState.voices.addFromMedia(sourcePath, sourceIn + offset,
                                                Math.min(sourceOut, sourceIn + offset + 20), name)
        }
        onClosed: EditorState.voices.cancelRecording()

        contentItem: Column {
            spacing: Theme.spacingLg
            width: parent ? parent.width : 360

            ThemedLabel {
                text: qsTr("Name")
            }

            ThemedTextField {
                id: nameField
                width: parent.width
                placeholderText: qsTr("My voice")
            }

            ThemedLabel {
                visible: voiceDialog.mode === "clip"
                width: parent.width
                text: voiceDialog.range > 20
                      ? qsTr("The clip is longer than 20 seconds, so 20 seconds are used. Starting at %1 s.")
                            .arg(voiceDialog.offset.toFixed(1))
                      : qsTr("Using %1 seconds of the selected clip.").arg(voiceDialog.range.toFixed(1))
            }

            ThemedSlider {
                visible: voiceDialog.mode === "clip" && voiceDialog.range > 20
                width: parent.width
                label: qsTr("Start offset")
                from: 0
                to: Math.max(0.1, voiceDialog.range - 20)
                value: voiceDialog.offset
                valueFormatter: v => qsTr("%1 s").arg(Number(v).toFixed(1))
                onMoved: voiceDialog.offset = value
            }

            Column {
                visible: voiceDialog.mode === "mic"
                width: parent.width
                spacing: Theme.spacingMd

                ThemedLabel {
                    width: parent.width
                    text: qsTr("Record 5 to 20 seconds in a quiet room. Read this aloud in your natural voice:")
                }

                Text {
                    width: parent.width
                    wrapMode: Text.WordWrap
                    text: qsTr("The quick brown fox jumps over the lazy dog, while a gentle breeze moves through the quiet garden and the evening light fades slowly.")
                    color: Theme.panelForeground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeSm
                }

                Rectangle {
                    width: parent.width
                    height: Theme.spacingMd
                    radius: height / 2
                    color: Theme.panelMuted

                    Rectangle {
                        width: parent.width * Math.max(0, Math.min(1, EditorState.voices.level))
                        height: parent.height
                        radius: parent.radius
                        color: Theme.primary
                    }
                }

                Row {
                    spacing: Theme.spacingLg

                    ThemedButton {
                        text: voiceDialog.recording ? qsTr("Recording…") : qsTr("Start recording")
                        variant: "secondary"
                        glyph: Theme.icons.mic
                        enabled: !voiceDialog.recording
                        onClicked: EditorState.voices.startRecording()
                    }

                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: qsTr("%1 s").arg(EditorState.voices.recordingSeconds.toFixed(1))
                        color: Theme.panelForeground
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSizeSm
                    }
                }
            }

            ThemedCheckBox {
                id: rightsBox
                width: parent.width
                text: qsTr("I have the right to use this person's voice")
            }
        }
    }
}
