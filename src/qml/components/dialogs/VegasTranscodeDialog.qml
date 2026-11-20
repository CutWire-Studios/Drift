import QtQuick
import QtQuick.Controls.Basic
import Drift

// Shown when opening a Sony / MAGIX VEGAS Pro project with camera raw / AVCHD (MTS) media
// offering one-click batch parallel transcode to Apple ProRes 422.
ThemedDialog {
    id: root

    property var assetIds: []
    property var projectUrl: null

    title: qsTr("Convert Media to Apple ProRes 422")
    acceptText: qsTr("Convert to ProRes 422")
    rejectText: qsTr("Keep Original Media")
    acceptVariant: "primary"
    rejectVariant: "secondary"
    preferredWidth: Theme.dialogWidthMd

    function openFor(ids, url) {
        if (!ids || ids.length === 0)
            return
        assetIds = ids
        projectUrl = url
        open()
    }

    onAccepted: {
        if (assetIds && assetIds.length > 0) {
            EditorState.bin.convertToProRes(assetIds, projectUrl)
        }
    }

    contentItem: Column {
        spacing: Theme.spacingLg
        width: parent ? parent.width : 400

        ThemedLabel {
            width: parent.width
            wrapMode: Text.WordWrap
            text: qsTr("This VEGAS Pro timeline contains %n unconverted AVCHD / camera raw clip(s).", "", root.assetIds.length)
            tone: "default"
            size: "base"
            font.weight: Font.DemiBold
            font.pixelSize: Theme.fontSizeLg
        }

        ThemedLabel {
            width: parent.width
            wrapMode: Text.WordWrap
            text: qsTr("Drift works best with edit-friendly intraframe codecs. Converting these clips to Apple ProRes 422 now ensures smooth timeline playback, responsive frame-accurate scrubbing, and prevents audio sync drift.\n\nTranscoding runs in the background using multi-threaded FFmpeg without modifying your original media files.")
            tone: "muted"
            size: "sm"
            lineHeight: 1.35
        }
    }
}
