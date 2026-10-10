import QtQuick
import QtQuick.Controls.Basic
import Drift
import ".."

// Speaks every caption of the selected subtitle clip and lays the lines out on new audio tracks.
ThemedDialog {
    id: root

    property int trackIndex: -1
    property int clipIndex: -1

    title: qsTr("Generate voiceover")
    preferredWidth: Theme.dialogWidthMd
    showFooter: false
    closePolicy: EditorState.tts.running ? Popup.NoAutoClose : (Popup.CloseOnEscape | Popup.CloseOnPressOutside)

    function openFor(track, clip) {
        trackIndex = track
        clipIndex = clip
        voiceBox.currentIndex = Math.max(0, EditorState.voices.indexOfId(EditorState.tts.lastVoice))
        langBox.currentIndex = Math.max(0, langBox.indexOfValue(EditorState.tts.lastLanguage))
        exaggerationSlider.value = EditorState.tts.lastExaggeration
        open()
    }

    Connections {
        target: EditorState.tts
        function onFinished(ok, message) {
            if (!root.visible)
                return
            root.close()
            AppController.setLastMessage(message, ok ? "success" : "error")
        }
    }

    contentItem: Column {
        spacing: Theme.spacingLg
        width: parent ? parent.width : 360

        ThemedLabel {
            text: qsTr("Voice")
        }

        ThemedComboBox {
            id: voiceBox
            width: parent.width
            enabled: !EditorState.tts.running
            model: EditorState.voices
            textRole: "name"
            valueRole: "id"
        }

        ThemedLabel {
            text: qsTr("Language")
        }

        ThemedComboBox {
            id: langBox
            width: parent.width
            enabled: !EditorState.tts.running
            textRole: "label"
            valueRole: "code"
            model: EditorState.tts.languageOptions()
        }

        ThemedLabel {
            text: qsTr("Expressiveness")
        }

        ThemedSlider {
            id: exaggerationSlider
            width: parent.width
            label: qsTr("Expressiveness")
            from: 0.25
            to: 2.0
            stepSize: 0.05
            enabled: !EditorState.tts.running
        }

        ThemedLabel {
            width: parent.width
            text: qsTr("Higher values sound more dramatic and speak faster")
        }

        Column {
            visible: EditorState.tts.running
            width: parent.width
            spacing: Theme.spacingMd

            ThemedLabel {
                width: parent.width
                tone: "default"
                elide: Text.ElideRight
                text: EditorState.tts.loading
                      ? qsTr("Loading voice model (first use takes a while)…")
                      : EditorState.tts.status
            }

            ThemedProgressBar {
                width: parent.width
                value: EditorState.tts.progress
                opacity: EditorState.tts.loading ? 0.4 : 1
            }
        }

        Row {
            spacing: Theme.spacingMd
            layoutDirection: Qt.RightToLeft
            width: parent.width

            ThemedButton {
                visible: !EditorState.tts.running
                text: qsTr("Generate")
                variant: "primary"
                glyph: Theme.icons.audioLines
                enabled: EditorState.tts.available
                onClicked: EditorState.tts.voiceoverFromSubtitles(
                               root.trackIndex, root.clipIndex, voiceBox.currentValue,
                               langBox.currentValue, exaggerationSlider.value)
            }

            ThemedButton {
                visible: EditorState.tts.running
                text: qsTr("Cancel")
                variant: "destructive"
                glyph: Theme.icons.x
                onClicked: EditorState.tts.cancel()
            }

            ThemedButton {
                visible: !EditorState.tts.running
                text: qsTr("Close")
                variant: "secondary"
                onClicked: root.close()
            }
        }
    }
}
