import QtQuick
import Drift

SettingsPage {
    id: page

    pageId: "audio"
    title: qsTr("Audio")

    SettingsGroupHeader {
        text: qsTr("Devices")
        query: page.query
    }

    SettingsRow {
        title: qsTr("Audio output")
        searchId: "audio-output"
        description: qsTr("Where playback is heard. “System default” follows whatever your computer is set to, including when that changes.")
        keywords: "speakers headphones playback sound device"
        query: page.query

        ThemedComboBox {
            width: Theme.c.settingsRow.controlWidth
            textRole: "label"
            valueRole: "id"
            model: EditorState.audioOutputDevices
            currentIndex: {
                for (var i = 0; i < model.length; ++i) {
                    if (model[i].id === EditorState.audioOutputDeviceId)
                        return i
                }
                return 0
            }
            onActivated: EditorState.audioOutputDeviceId = model[currentIndex].id
        }
    }

    SettingsRow {
        title: qsTr("Microphone input")
        searchId: "audio-microphone-input"
        description: qsTr("Audio device used for recording voiceovers onto audio tracks.")
        keywords: "recording voiceover mic input"
        query: page.query

        ThemedComboBox {
            width: Theme.c.settingsRow.controlWidth
            textRole: "name"
            valueRole: "id"
            model: EditorState.availableMicrophones
            currentIndex: {
                for (var i = 0; i < model.length; ++i) {
                    if (model[i].name === EditorState.currentMicrophoneName)
                        return i
                }
                return 0
            }
            onActivated: EditorState.selectMicrophone(model[currentIndex].id)
        }
    }
}
