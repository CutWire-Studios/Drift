import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Window
import Drift
import ".."

Item {
    id: root

    property int clipDataRevision: 0
    readonly property var clipData: {
        void clipDataRevision
        return EditorState.selectedClipData
    }
    readonly property bool hasSelection: !!clipData && Object.keys(clipData).length > 0
    readonly property string clipKind: hasSelection ? (clipData.kind || "") : ""
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

        EmptyState {
            visible: root.clipKind !== "audio" && root.clipKind !== "video"
            width: parent.width
            compact: true
            glyph: Theme.icons.volumeOff
            title: qsTr("No audio")
            hint: qsTr("This clip has no audio track.")
        }

        PropertyKeyframeRow {
            width: root.width
            visible: root.clipKind === "audio" || root.clipKind === "video"
            propDef: root.propVolume
            keyframeList: (root.clipData.keyframes && root.clipData.keyframes.volume && root.clipData.keyframes.volume.points) || []
            useSlider: true
            sliderFrom: 0
            sliderTo: 2
            percent: true
        }

        // ----- Audio Track Selection (Multi-Track) -------------------------
        Column {
            id: audioTrackSection
            width: parent.width
            spacing: Theme.spacingSm
            visible: (root.clipKind === "audio" || root.clipKind === "video") && audioTrackModel.length > 1

            property var audioTrackModel: {
                void root.clipDataRevision
                if (EditorState.selectedTrack < 0 || EditorState.selectedClip < 0)
                    return []
                return EditorState.clipAudioStreams(EditorState.selectedTrack, EditorState.selectedClip)
            }

            Text {
                width: parent.width
                text: qsTr("Audio track")
                color: Theme.mutedForeground
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
                visible: root.clipKind === "video" && EditorState.separateAudioAvailable
                width: parent.width
                text: qsTr("Extract all audio tracks")
                onClicked: {
                    EditorState.separateAllAudioTracks(EditorState.selectedTrack, EditorState.selectedClip)
                }
            }
        }

        Rectangle {
            visible: root.clipKind === "audio" || root.clipKind === "video"
            width: parent.width
            height: 1
            color: Theme.panelBorder
            opacity: 0.5
        }

        // ----- Noise removal ---------------------------------------------
        Column {
            id: denoiseSection
            width: parent.width
            spacing: Theme.spacingSm
            visible: root.clipKind === "audio" || root.clipKind === "video"

            // Whether the model is on disk is a one-shot filesystem answer, not a
            // binding, hence the reset below when an addon of this kind appears.
            // The runtime that runs it is a second, separate addon.
            property bool denoiseReady: EditorState.denoiseAvailable()
            property bool runtimeReady: Addons.runtimeAvailable()

            Connections {
                target: Addons
                function onKindChanged(kind) {
                    if (kind === "denoise-model")
                        denoiseSection.denoiseReady = EditorState.denoiseAvailable()
                    else if (kind === "onnxruntime")
                        denoiseSection.runtimeReady = Addons.runtimeAvailable()
                }
            }

            Text {
                width: parent.width
                text: qsTr("Noise")
                color: Theme.mutedForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
            }

            ThemedButton {
                visible: denoiseSection.denoiseReady && denoiseSection.runtimeReady
                width: parent.width
                text: qsTr("Remove noise…")
                enabled: !EditorState.denoising
                onClicked: {
                    const data = EditorState.selectedClipData
                    root.Window.window.openDenoise(
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
                onClicked: root.Window.window.openAddonManager(
                    denoiseSection.runtimeReady ? "denoise-model" : "onnxruntime")
            }
        }

        Rectangle {
            visible: root.clipKind === "audio" || root.clipKind === "video"
            width: parent.width
            height: 1
            color: Theme.panelBorder
            opacity: 0.5
        }

        Text {
            visible: root.clipKind === "audio" || root.clipKind === "video"
            text: qsTr("Auto subtitles")
            color: Theme.mutedForeground
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
                     && (root.clipKind === "audio" || root.clipKind === "video")
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
                     && (root.clipKind === "audio" || root.clipKind === "video")
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
            color: Theme.mutedForeground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeXs
        }

        ThemedButton {
            visible: parent.whisperReady
                     && (root.clipKind === "audio" || root.clipKind === "video")
            width: parent.width
            text: EditorState.subtitleGenerating
                  ? qsTr("Creating captions… %1%").arg(Math.round(EditorState.subtitleGenProgress * 100))
                  : qsTr("Create captions from speech")
            enabled: !EditorState.subtitleGenerating
            onClicked: {
                const lang = subtitleLanguageBox.currentValue !== undefined
                             ? subtitleLanguageBox.currentValue
                             : ""
                EditorState.generateSubtitlesForClip(
                    EditorState.selectedTrack, EditorState.selectedClip, lang,
                    subtitleWordsBox.currentValue)
            }
        }

        ThemedButton {
            visible: !parent.whisperReady
                     && (root.clipKind === "audio" || root.clipKind === "video")
            width: parent.width
            text: parent.runtimeReady
                  ? qsTr("Download speech recognition (about 670 MB)")
                  : qsTr("Install AI engine first")
            variant: "primary"
            onClicked: root.Window.window.openAddonManager(
                parent.runtimeReady ? "whisper-model" : "onnxruntime")
        }
    }
}
