import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Window
import Drift
import ".."

// Subtitles tab: timed caption clips, .srt import, and auto-caption from speech.
Item {
    id: root

    // A clip landed on the timeline. The phone shell closes the sheet on this —
    // the thing you came for is behind it. Auto-caption deliberately does not
    // emit it: its progress and cancel button live in this sheet.
    signal added()

    Flickable {
        anchors.fill: parent
        contentWidth: width
        contentHeight: subtitleColumn.height + Theme.spacing3xl
        clip: true
        ScrollBar.vertical: AppScrollBar { }

        Column {
            id: subtitleColumn
            x: Theme.pagePadding
            width: parent.width - Theme.pagePadding * 2
            spacing: Theme.spacingLg
            topPadding: Theme.pagePadding

            readonly property real contentWidth: width

            Text {
                width: subtitleColumn.contentWidth
                wrapMode: Text.WordWrap
                text: qsTr("Subtitle track — one clip holds many timed captions. Place it on the timeline, trim its length, then add caption lines at each moment in the clip panel.")
                color: Theme.mutedForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
            }

            ThemedButton {
                text: qsTr("Add subtitle clip")
                variant: "primary"
                glyph: Theme.icons.captions
                onClicked: {
                    EditorState.addSubtitleClip(-1)
                    root.added()
                }
            }

            ThemedButton {
                text: qsTr("Import subtitle file")
                variant: "secondary"
                glyph: Theme.icons.upload
                tooltip: qsTr("Import a .srt file as a subtitle clip")
                onClicked: {
                    const url = FileDialogs.openFile(
                        qsTr("Import Subtitles"),
                        [qsTr("SubRip subtitles (*.srt)"), qsTr("All files (*)")])
                    if (url != "") {
                        EditorState.importSubtitleFile(url, -1)
                        root.added()
                    }
                }
            }

            Rectangle {
                width: subtitleColumn.contentWidth
                height: Theme.borderWidth
                color: Theme.panelBorder
            }

            // Same transcriber as the clip inspector's Audio tab, surfaced here so
            // auto captions sit next to the manual subtitle route. It transcribes the
            // selected clip, so it stays disabled until a video or audio clip is picked.
            Text {
                width: subtitleColumn.contentWidth
                text: qsTr("Add auto caption")
                color: Theme.foreground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeSm
            }

            Text {
                width: subtitleColumn.contentWidth
                wrapMode: Text.WordWrap
                text: root.captionTargetReady
                      ? qsTr("Creates captions from the speech in the selected clip.")
                      : qsTr("Select a video or audio clip on the timeline first.")
                color: Theme.mutedForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
            }

            // The transcriber ships as an addon, and so does the runtime it needs; without
            // both there are no languages to list and nothing to run, so offer the download
            // in place of the controls.
            ThemedComboBox {
                id: captionLanguageBox
                visible: root.whisperReady
                width: subtitleColumn.contentWidth
                enabled: root.captionTargetReady && !EditorState.subtitleGenerating
                textRole: "label"
                valueRole: "code"
                model: EditorState.whisperLanguages()
                Component.onCompleted: currentIndex = 0
            }

            ThemedComboBox {
                id: captionWordsBox
                visible: root.whisperReady
                width: subtitleColumn.contentWidth
                enabled: root.captionTargetReady && !EditorState.subtitleGenerating
                textRole: "label"
                valueRole: "words"
                model: root.captionLengthOptions
                Component.onCompleted: currentIndex = 0
            }

            Text {
                visible: root.whisperReady && captionWordsBox.currentValue > 0
                width: subtitleColumn.contentWidth
                wrapMode: Text.WordWrap
                text: qsTr("Shorter captions are timed by splitting each phrase evenly, so they can drift slightly out of sync with the speech.")
                color: Theme.mutedForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
            }

            ThemedButton {
                visible: root.whisperReady && !EditorState.subtitleGenerating
                width: subtitleColumn.contentWidth
                text: qsTr("Add auto caption")
                variant: "secondary"
                glyph: Theme.icons.captions
                tooltip: root.captionTargetReady
                         ? qsTr("Create captions from the selected clip's speech")
                         : qsTr("Select a video or audio clip first")
                enabled: root.captionTargetReady
                onClicked: {
                    const lang = captionLanguageBox.currentValue !== undefined
                                 ? captionLanguageBox.currentValue
                                 : ""
                    EditorState.generateSubtitlesForClip(
                        EditorState.selectedTrack, EditorState.selectedClip, lang,
                        captionWordsBox.currentValue)
                }
            }

            Column {
                visible: root.whisperReady && EditorState.subtitleGenerating
                width: subtitleColumn.contentWidth
                spacing: Theme.spacingMd

                Text {
                    width: parent.width
                    text: EditorState.subtitleGenStatus.length > 0
                          ? EditorState.subtitleGenStatus
                          : qsTr("Creating captions… %1%").arg(Math.round(EditorState.subtitleGenProgress * 100))
                    color: Theme.panelForeground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeXs
                    elide: Text.ElideRight
                }

                Rectangle {
                    width: parent.width
                    height: Theme.spacingMd
                    radius: height / 2
                    color: Theme.panelMuted

                    Rectangle {
                        width: parent.width * Math.max(0, Math.min(1, EditorState.subtitleGenProgress))
                        height: parent.height
                        radius: parent.radius
                        color: Theme.primary

                        Behavior on width {
                            NumberAnimation { duration: Theme.durationBase; easing.type: Theme.easing }
                        }
                    }
                }

                ThemedButton {
                    text: qsTr("Cancel")
                    variant: "destructive"
                    glyph: Theme.icons.x
                    tooltip: qsTr("Stop creating captions")
                    onClicked: EditorState.cancelSubtitleGeneration()
                }
            }

            ThemedButton {
                visible: !root.whisperReady
                width: subtitleColumn.contentWidth
                text: root.runtimeReady
                      ? qsTr("Download speech recognition (about 670 MB)")
                      : qsTr("Install AI engine first")
                variant: "primary"
                glyph: Theme.icons.download
                tooltip: qsTr("Needed for auto captions from speech")
                onClicked: root.Window.window.openAddonManager(
                    root.runtimeReady ? "whisper-model" : "onnxruntime")
            }
        }
    }

    // "Recommended" packs by display width like openai-whisper does; the numbered entries cap
    // words per caption on top of that.
    readonly property var captionLengthOptions: {
        const options = [{ label: qsTr("Recommended caption length"), words: 0 }]
        options.push({ label: qsTr("1 word per caption"), words: 1 })
        for (let n = 2; n <= 8; ++n)
            options.push({ label: qsTr("%1 words per caption").arg(n), words: n })
        return options
    }

    property bool whisperReady: Addons.hasKind("whisper-model")
                                && Addons.runtimeAvailable()
    property bool runtimeReady: Addons.runtimeAvailable()

    readonly property string captionClipKind: {
        const data = EditorState.selectedClipData
        return (data && data.kind) ? data.kind : ""
    }
    readonly property bool captionTargetReady: captionClipKind === "video"
                                               || captionClipKind === "audio"

    Connections {
        target: Addons
        function onKindChanged(kind) {
            if (kind !== "whisper-model" && kind !== "onnxruntime")
                return
            root.runtimeReady = Addons.runtimeAvailable()
            root.whisperReady = Addons.hasKind("whisper-model")
                                && root.runtimeReady
        }
    }
}
