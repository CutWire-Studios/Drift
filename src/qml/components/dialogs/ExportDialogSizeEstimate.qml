import QtQuick
import Drift

Column {
    id: root
    required property var dialog
    spacing: Theme.spacingXs

    readonly property var estimate: {
        void EditorState.tracksRevision
        return root.dialog.isGifExport ? ({ bytes: 0, approximate: false })
                                       : EditorState.estimateExportSize(root.dialog.buildSettings())
    }
    visible: estimate.bytes > 0

    function formatBytes(bytes) {
        if (bytes < 1e6)
            return qsTr("%1 KB").arg(Math.max(1, Math.round(bytes / 1e3)))
        if (bytes < 1e9) {
            const mb = bytes / 1e6
            return qsTr("%1 MB").arg(mb < 10 ? mb.toFixed(1) : Math.round(mb))
        }
        return qsTr("%1 GB").arg((bytes / 1e9).toFixed(2))
    }

    Row {
        width: parent.width

        ThemedLabel {
            width: parent.width / 2
            text: qsTr("Estimated size")
        }

        ThemedLabel {
            width: parent.width / 2
            horizontalAlignment: Text.AlignRight
            tone: "default"
            text: qsTr("≈ %1").arg(root.formatBytes(root.estimate.bytes))
        }
    }

    ThemedLabel {
        width: parent.width
        size: "sm"
        visible: root.estimate.approximate
        text: qsTr("Estimate — varies with content")
    }
}
