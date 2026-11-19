import QtQuick
import QtQuick.Controls.Basic
import Drift
// Sound effect from a text prompt through the user's ElevenLabs account; billed by the provider.
ThemedDialog {
    id: root

    readonly property bool configured: {
        void EditorState.cloudProviders.revision
        return EditorState.cloudProviders.configured("elevenlabs")
    }
    readonly property bool consented: {
        void EditorState.cloudProviders.revision
        return EditorState.cloudProviders.consent("elevenlabs")
    }
    readonly property bool canGenerate: configured && consented && !EditorState.tts.sfxRunning
                                        && promptField.text.trim().length > 0

    title: qsTr("Sound effect")
    preferredWidth: Theme.dialogWidthMd
    showFooter: false
    closePolicy: EditorState.tts.sfxRunning ? Popup.NoAutoClose : (Popup.CloseOnEscape | Popup.CloseOnPressOutside)

    Connections {
        target: EditorState.tts
        function onSfxFinished(ok, message) {
            if (!root.visible)
                return
            if (ok)
                root.close()
            AppController.setLastMessage(message, ok ? "success" : "error")
        }
    }

    contentItem: Column {
        spacing: Theme.spacingLg
        width: parent ? parent.width : 360

        ThemedLabel {
            visible: !root.configured
            width: parent.width
            wrapMode: Text.WordWrap
            size: "sm"
            text: qsTr("Sound effects come from ElevenLabs. Add your API key in Settings to use them.")
        }

        ThemedLabel {
            visible: root.configured && !root.consented
            width: parent.width
            wrapMode: Text.WordWrap
            size: "sm"
            text: qsTr("Allow sending prompts to ElevenLabs in Settings to use sound effects.")
        }

        ThemedButton {
            visible: !root.configured || !root.consented
            text: qsTr("Open settings")
            variant: "secondary"
            glyph: Theme.icons.settings
            onClicked: {
                root.close()
                Qt.callLater(() => ShellBridge.main.openSettings("ai"))
            }
        }

        ThemedLabel {
            text: qsTr("Describe the sound")
        }

        ThemedTextArea {
            id: promptField
            width: parent.width
            height: 90
            enabled: !EditorState.tts.sfxRunning
            placeholderText: qsTr("Heavy wooden door creaking open")
        }

        ThemedCheckBox {
            id: autoLength
            width: parent.width
            checked: true
            enabled: !EditorState.tts.sfxRunning
            text: qsTr("Let the model pick the length")
        }

        ThemedNumberField {
            id: lengthField
            visible: !autoLength.checked
            width: parent.width
            from: 0.5
            to: 30
            decimals: 1
            step: 0.5
            value: 3
            unit: "s"
            enabled: !EditorState.tts.sfxRunning
        }

        ThemedLabel {
            text: qsTr("Prompt influence")
        }

        ThemedSlider {
            id: influenceSlider
            width: parent.width
            label: qsTr("Prompt influence")
            from: 0
            to: 1
            stepSize: 0.05
            value: 0.3
            enabled: !EditorState.tts.sfxRunning
        }

        ThemedLabel {
            width: parent.width
            wrapMode: Text.WordWrap
            size: "sm"
            text: qsTr("Higher values follow the prompt more literally and vary less")
        }

        ThemedCheckBox {
            id: loopBox
            width: parent.width
            enabled: !EditorState.tts.sfxRunning
            text: qsTr("Make it loop seamlessly")
        }

        ThemedCheckBox {
            id: placeBox
            width: parent.width
            checked: true
            enabled: !EditorState.tts.sfxRunning
            text: qsTr("Place at the playhead")
        }

        Column {
            visible: EditorState.tts.sfxRunning
            width: parent.width
            spacing: Theme.spacingMd

            ThemedLabel {
                width: parent.width
                tone: "default"
                elide: Text.ElideRight
                text: EditorState.tts.sfxStatus
            }

            ThemedProgressBar {
                width: parent.width
                value: EditorState.tts.sfxProgress
            }
        }

        Row {
            spacing: Theme.spacingMd
            layoutDirection: Qt.RightToLeft
            width: parent.width

            ThemedButton {
                visible: !EditorState.tts.sfxRunning
                text: qsTr("Generate")
                variant: "primary"
                glyph: Theme.icons.audioLines
                enabled: root.canGenerate
                tooltip: qsTr("Billed by ElevenLabs to your account")
                onClicked: EditorState.tts.generateSfx(promptField.text, autoLength.checked ? 0 : lengthField.value,
                                                       influenceSlider.value, loopBox.checked, placeBox.checked)
            }

            ThemedButton {
                visible: EditorState.tts.sfxRunning
                text: qsTr("Cancel")
                variant: "destructive"
                glyph: Theme.icons.x
                onClicked: EditorState.tts.cancelSfx()
            }

            ThemedButton {
                visible: !EditorState.tts.sfxRunning
                text: qsTr("Close")
                variant: "secondary"
                onClicked: root.close()
            }
        }
    }
}
