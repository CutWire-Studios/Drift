import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Window
import Drift
Item {
    id: root

    property int clipDataRevision: 0
    readonly property var clipData: {
        void clipDataRevision
        return EditorState.selectedClipData
    }
    readonly property bool hasSelection: !!clipData && Object.keys(clipData).length > 0
    readonly property string clipKind: hasSelection ? (clipData.kind || "") : ""
    // Read straight from a media file: stream choice, denoise and transcription need one.
    readonly property bool fileAudio: (clipKind === "audio" || clipKind === "video") && !clipData.sequenceId
    readonly property var propVolume: { "key": "volume", "label": qsTr("Volume"), "def": 1.0, "decimals": 2 }

    // "Recommended" packs by display width like openai-whisper does; the numbered entries cap
    // words per caption on top of that.
    readonly property var captionLengthOptions: {
        const options = [{ label: qsTr("Recommended caption length"), words: 0 }]
        options.push({ label: qsTr("1 word per caption"), words: 1 })
        for (let n = 2; n <= 8; ++n)
            options.push({ label: qsTr("%1 words per caption").arg(n), words: n })
        return options
    }

    height: audioTabColumn.height
    implicitHeight: audioTabColumn.height

    function refreshFields() {}

    Connections {
        target: EditorState
        function onSelectionChanged() { root.clipDataRevision++ }
        function onSelectedClipDataChanged() { root.clipDataRevision++ }
        function onTracksChanged() { root.clipDataRevision++ }
    }

    Column {
        id: audioTabColumn
        width: root.width
        spacing: Theme.spacingXl

        SearchAnchor {
            searchId: "audio-volume"
            title: qsTr("Volume")
            keywords: "loud gain level mute"
            target: audioVolumeRow
        }
        PropertyKeyframeRow {
            id: audioVolumeRow
            width: root.width
            propDef: root.propVolume
            keyframeList: (root.clipData.keyframes && root.clipData.keyframes.volume && root.clipData.keyframes.volume.points) || []
            useSlider: true
            editableWhilePlaying: true
            sliderFrom: 0
            sliderTo: 2
            percent: true
            decibels: true
        }

        // ----- Pan (stereo balance) ----------------------------------------
        Column {
            id: panSection
            width: parent.width
            spacing: Theme.spacingSm

            readonly property real panValue: {
                void root.clipDataRevision
                return (root.clipData && root.clipData.pan !== undefined)
                       ? root.clipData.pan : 0.0
            }

            Row {
                width: parent.width

                Text {
                    width: parent.width / 2
                    text: qsTr("Pan")
                    SearchAnchor {
                        searchId: "audio-pan"
                        title: qsTr("Pan")
                        keywords: "balance left right stereo"
                    }
                    color: Theme.c.audioInspector.foregroundMuted
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeXs
                }

                Text {
                    width: parent.width / 2
                    horizontalAlignment: Text.AlignRight
                    // "L 50" / "C" / "R 50" reads faster than a signed fraction, and the
                    // sign convention for pan is not something a user should have to recall.
                    text: {
                        const v = panSection.panValue
                        if (Math.abs(v) < 0.005)
                            return qsTr("C")
                        return (v < 0 ? qsTr("L %1") : qsTr("R %1"))
                                   .arg(Math.round(Math.abs(v) * 100))
                    }
                    color: Theme.c.audioInspector.foreground
                    font.family: Theme.monoFontFamily
                    font.pixelSize: Theme.fontSizeSm
                }
            }

            ThemedSlider {
                id: panSlider
                label: qsTr("Pan")
                width: parent.width
                from: -1
                to: 1
                Binding on value {
                    when: !panSlider.pressed
                    value: panSection.panValue
                }
                onMoved: EditorState.previewSetClipPan(
                             EditorState.selectedTrack, EditorState.selectedClip, value)
                onPressedChanged: {
                    if (pressed)
                        EditorState.beginPreviewDrag(qsTr("Pan changed"))
                    else
                        EditorState.commitPreviewDrag()
                }
            }

            ThemedButton {
                text: qsTr("Centre")
                enabled: Math.abs(panSection.panValue) >= 0.005
                onClicked: EditorState.setClipPan(
                               EditorState.selectedTrack, EditorState.selectedClip, 0)
            }
        }

        // ----- Audio Track Selection (Multi-Track) -------------------------
        Column {
            id: audioTrackSection
            width: parent.width
            spacing: Theme.spacingSm
            visible: root.fileAudio && audioTrackModel.length > 1

            property var audioTrackModel: {
                void root.clipDataRevision
                if (EditorState.selectedTrack < 0 || EditorState.selectedClip < 0)
                    return []
                return EditorState.clipAudioStreams(EditorState.selectedTrack, EditorState.selectedClip)
            }

            Text {
                width: parent.width
                text: qsTr("Audio track")
                SearchAnchor {
                    searchId: "audio-track"
                    title: qsTr("Audio track")
                    keywords: "extract stream"
                }
                color: Theme.c.audioInspector.foregroundMuted
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
            }

            ThemedComboBox {
                id: audioStreamSelector
                width: parent.width
                textRole: "label"
                valueRole: "index"
                model: audioTrackSection.audioTrackModel
                currentIndex: {
                    const currentIdx = (root.clipData && root.clipData.audioStreamIndex !== undefined)
                                       ? root.clipData.audioStreamIndex : 0
                    for (let i = 0; i < audioTrackSection.audioTrackModel.length; ++i) {
                        if (audioTrackSection.audioTrackModel[i].index === currentIdx)
                            return i
                    }
                    return 0
                }
                onActivated: {
                    EditorState.setClipAudioStreamIndex(
                        EditorState.selectedTrack, EditorState.selectedClip, currentValue)
                }
            }

            ThemedButton {
                visible: (root.clipKind === "video" || root.clipKind === "composite")
                         && EditorState.separateAudioAvailable
                width: parent.width
                text: qsTr("Extract all audio tracks")
                onClicked: {
                    EditorState.separateAllAudioTracks(EditorState.selectedTrack, EditorState.selectedClip)
                }
            }
        }

        Rectangle {
            visible: root.fileAudio
            width: parent.width
            height: Theme.c.audioInspector.ruleWidth
            color: Theme.c.audioInspector.rule
            opacity: Theme.c.audioInspector.ruleOpacity
        }

        // ----- Noise removal ---------------------------------------------
        Column {
            id: denoiseSection
            width: parent.width
            spacing: Theme.spacingSm
            visible: root.fileAudio

            // Whether the model is on disk is a one-shot filesystem answer, not a
            // binding, hence the reset below when an addon of this kind appears.
            // The runtime that runs it is a second, separate addon.
            property bool denoiseReady: EditorState.denoise.available()
            property bool runtimeReady: Addons.runtimeAvailable()

            Connections {
                target: Addons
                function onKindChanged(kind) {
                    if (kind === "denoise-model")
                        denoiseSection.denoiseReady = EditorState.denoise.available()
                    else if (kind === "onnxruntime")
                        denoiseSection.runtimeReady = Addons.runtimeAvailable()
                }
            }

            Text {
                width: parent.width
                text: qsTr("Noise")
                color: Theme.c.audioInspector.foregroundMuted
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
            }

            SearchAnchor {
                searchId: "audio-remove-noise"
                title: qsTr("Remove noise…")
                keywords: "denoise hiss background clean"
                target: audioRemoveNoiseButton
            }
            ThemedButton {
                id: audioRemoveNoiseButton
                visible: denoiseSection.denoiseReady && denoiseSection.runtimeReady
                width: parent.width
                text: qsTr("Remove noise…")
                enabled: !EditorState.denoise.running
                onClicked: {
                    const data = EditorState.selectedClipData
                    ShellBridge.main.openDenoise(
                        EditorState.selectedTrack, EditorState.selectedClip,
                        data.duration !== undefined ? data.duration : 0)
                }
            }

            ThemedButton {
                visible: !denoiseSection.denoiseReady || !denoiseSection.runtimeReady
                width: parent.width
                text: denoiseSection.runtimeReady
                      ? qsTr("Download noise removal (about 9 MB)")
                      : qsTr("Install AI engine first")
                variant: "primary"
                onClicked: ShellBridge.main.openAddonManager(
                    denoiseSection.runtimeReady ? "denoise-model" : "onnxruntime")
            }
        }

        Rectangle {
            visible: root.fileAudio
            width: parent.width
            height: Theme.c.audioInspector.ruleWidth
            color: Theme.c.audioInspector.rule
            opacity: Theme.c.audioInspector.ruleOpacity
        }

        Text {
            visible: root.fileAudio
            text: qsTr("Auto subtitles")
            SearchAnchor {
                searchId: "audio-auto-subtitles"
                title: qsTr("Auto subtitles")
                keywords: "captions transcribe speech whisper"
            }
            color: Theme.c.audioInspector.foregroundMuted
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeXs
        }

        // The transcriber is an addon, and so is the runtime it needs; without
        // both there are no languages to list and nothing to run, so offer the
        // download in place of the controls.
        property bool whisperReady: Addons.hasKind("whisper-model")
                                    && Addons.runtimeAvailable()
        property bool runtimeReady: Addons.runtimeAvailable()

        Connections {
            target: Addons
            function onKindChanged(kind) {
                if (kind !== "whisper-model" && kind !== "onnxruntime")
                    return
                const section = subtitleLanguageBox.parent
                section.runtimeReady = Addons.runtimeAvailable()
                section.whisperReady = Addons.hasKind("whisper-model")
                                       && section.runtimeReady
            }
        }

        ThemedComboBox {
            id: subtitleLanguageBox
            visible: parent.whisperReady
                     && root.fileAudio
            width: parent.width
            enabled: !EditorState.subtitleGenerating
            textRole: "label"
            valueRole: "code"
            model: EditorState.whisperLanguages()
            Component.onCompleted: currentIndex = 0
        }

        ThemedComboBox {
            id: subtitleWordsBox
            visible: parent.whisperReady
                     && root.fileAudio
            width: parent.width
            enabled: !EditorState.subtitleGenerating
            textRole: "label"
            valueRole: "words"
            model: root.captionLengthOptions
            Component.onCompleted: currentIndex = 0
        }

        Text {
            visible: subtitleWordsBox.visible && subtitleWordsBox.currentValue > 0
            width: parent.width
            wrapMode: Text.WordWrap
            text: qsTr("Shorter captions are timed by splitting each phrase evenly, so they can drift slightly out of sync with the speech.")
            color: Theme.c.audioInspector.foregroundMuted
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeXs
        }

        ThemedButton {
            visible: parent.whisperReady
                     && root.fileAudio
            width: parent.width
            text: EditorState.subtitleGenerating
                  ? qsTr("Creating captions… %1%").arg(Math.round(EditorState.subtitleGenProgress * 100))
                  : qsTr("Create captions from speech")
            enabled: !EditorState.subtitleGenerating
            tooltip: qsTr("Several selected clips become one caption clip")
            onClicked: {
                const lang = subtitleLanguageBox.currentValue !== undefined
                             ? subtitleLanguageBox.currentValue
                             : ""
                EditorState.generateSubtitlesForSelection(lang, subtitleWordsBox.currentValue)
            }
        }

        ThemedButton {
            visible: !parent.whisperReady
                     && root.fileAudio
            width: parent.width
            text: parent.runtimeReady
                  ? qsTr("Download speech recognition (about 670 MB)")
                  : qsTr("Install AI engine first")
            variant: "primary"
            onClicked: ShellBridge.main.openAddonManager(
                parent.runtimeReady ? "whisper-model" : "onnxruntime")
        }
    }
}
