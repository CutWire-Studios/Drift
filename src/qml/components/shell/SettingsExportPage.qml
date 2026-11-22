import QtQuick
import Drift

SettingsPage {
    id: page

    pageId: "export"
    title: qsTr("Export")

    SettingsGroupHeader {
        text: qsTr("Encoding")
        query: page.query
    }

    SettingsRow {
        title: qsTr("Prefer hardware encoder for new exports")
        searchId: "export-hardware-encoder"
        description: qsTr("Start new exports on a GPU H.264 encoder when one is available. Faster, but quality per bitrate is usually lower than the default software encoder.")
        keywords: "nvenc qsv amf vaapi videotoolbox mediacodec gpu encode h264 x264"
        query: page.query

        ThemedSwitch {
            checked: EditorState.preferences.preferHardwareExport
            onToggled: EditorState.preferences.preferHardwareExport = checked
        }
    }
}
