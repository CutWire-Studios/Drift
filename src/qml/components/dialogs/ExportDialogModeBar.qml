import QtQuick
import Drift

Row {
    id: bar
    width: parent.width
    required property var dialog
    spacing: Theme.spacingMd
    readonly property real chipWidth: Theme.touchInput && Theme.compact ? Math.floor((width - 2 * spacing) / 3) : -1

    ThemedToggleButton {
        width: bar.chipWidth > 0 ? bar.chipWidth : implicitWidth
        text: qsTr("Video")
        checked: dialog.exportMode === "video"
        onClicked: {
            if (dialog.exportMode === "video")
                return
            dialog.exportMode = "video"
            dialog.advancedOpen = false
            dialog.syncComboIndices()
        }
    }

    ThemedToggleButton {
        width: bar.chipWidth > 0 ? bar.chipWidth : implicitWidth
        text: qsTr("Audio")
        checked: dialog.exportMode === "audio"
        onClicked: {
            if (dialog.exportMode === "audio")
                return
            dialog.exportMode = "audio"
            dialog.advancedOpen = false
            dialog.syncComboIndices()
        }
    }

    ThemedToggleButton {
        width: bar.chipWidth > 0 ? bar.chipWidth : implicitWidth
        text: qsTr("GIF")
        enabled: EditorState.exportGifAvailable()
        tooltip: enabled ? "" : qsTr("GIF encoder is not available in this build")
        checked: dialog.exportMode === "gif"
        onClicked: {
            if (dialog.exportMode === "gif")
                return
            dialog.exportMode = "gif"
            dialog.advancedOpen = false
            dialog.applyGifDefaults()
            dialog.syncComboIndices()
        }
    }
    }
