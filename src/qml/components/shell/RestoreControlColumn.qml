import QtQuick
import Drift

Item {
    id: sidebar
    required property var tool
    property Item pickerPanel
    property alias decompressBox: decompressBox
    width: 300
    height: parent.height

    Column {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: footer.top
        anchors.bottomMargin: Theme.spacingLg
        spacing: Theme.spacingLg

        ThemedLabel {
            width: parent.width
            wrapMode: Text.WordWrap
            text: tool.forAsset
                  ? qsTr("Preview one frame, then enhance the whole video. The result is added to the media bin. Enhancing is slow — minutes per second of video without a GPU.")
                  : qsTr("Preview one frame, then enhance the part of the clip used on the timeline. The result is added to the media bin. Enhancing is slow — minutes per second of video without a GPU.")
        }

        Column {
            width: parent.width
            spacing: Theme.spacingSm

            ThemedLabel { text: qsTr("Remove compression") }

            ThemedComboBox {
                id: decompressBox
                width: parent.width
                enabled: !tool.running
                textRole: "label"
                valueRole: "value"
                model: tool.decompressModel
            }
        }

        Column {
            width: parent.width
            spacing: Theme.spacingSm

            ThemedLabel { text: qsTr("Upscale") }

            UpscaleModelCard {
                width: parent.width
                model: tool.upscaleChoice
                speedLevel: tool.speedLevel(tool.upscaleChoice.spm || 0)
                perFrameText: tool.upscaleChoice.spm > 0
                              ? tool.formatPerFrame(tool.secondsPerFrame(tool.upscaleChoice.spm)) : ""
                selected: pickerPanel.visible
                trailingGlyph: Theme.icons.chevronDown
                enabled: !tool.running
                onClicked: pickerPanel.visible = !pickerPanel.visible
            }
        }

        ThemedLabel {
            width: parent.width
            visible: tool.sourceWidth > 0
            tone: "default"
            wrapMode: Text.WordWrap
            text: qsTr("Output: %1 × %2").arg(tool.outputWidth).arg(tool.outputHeight)
                  + (tool.outputWidth * tool.outputHeight > 3840 * 2160
                     ? qsTr(" — larger than 4K, which is slow to edit and export") : "")
        }

        ThemedLabel {
            width: parent.width
            visible: tool.maxSeconds > 0
            wrapMode: Text.WordWrap
            text: (tool.speed.calibrated
                   ? qsTr("Up to about %1 on this computer's CPU.")
                   : qsTr("Up to about %1 on a typical laptop CPU. Preview a frame for an estimate for this computer."))
                  .arg(tool.formatDuration(tool.maxSeconds))
        }

        ThemedButton {
            width: parent.width
            variant: "secondary"
            text: qsTr("Preview this frame")
            enabled: !tool.running && tool.preview.width > 0 && !tool.preview.loading
                     && tool.hasModelChoice
            onClicked: {
                if (EditorState.restore.preview(tool.targetId,
                                               decompressBox.currentValue || "",
                                               tool.upscaleId) !== "")
                    tool.previewedWith = tool.chosenModels
            }
        }

        Column {
            width: parent.width
            spacing: Theme.spacingSm
            visible: tool.running || !!tool.job.error

            LabelledProgressRing {
                anchors.horizontalCenter: parent.horizontalCenter
                visible: tool.running
                ringSize: 56
                value: tool.job.progress || 0
                indeterminate: !(tool.job.progress > 0)
            }

            ThemedLabel {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                text: tool.running ? (tool.job.status || "") : (tool.job.error || "")
            }
        }
    }

    Row {
        id: footer
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        spacing: Theme.spacingMd

        WorkspaceLink {
            active: tool.hasEnhanced && tool.labAssetId.length > 0
            rowHeight: Theme.controlHeight
            target: ({ panel: "sourceLab.viewer", assetId: tool.labAssetId })
            onTriggered: tool.close()
        }

        ThemedButton {
            variant: tool.running ? "destructive" : "ghost"
            text: tool.running ? qsTr("Stop") : qsTr("Close")
            onClicked: {
                if (tool.running)
                    EditorState.restore.cancel(tool.targetId)
                else
                    tool.close()
            }
        }

        ThemedButton {
            variant: "primary"
            text: qsTr("Enhance clip")
            enabled: !tool.running && tool.hasModelChoice
            onClicked: EditorState.restore.run(tool.targetId,
                                                decompressBox.currentValue || "",
                                                tool.upscaleId)
        }
    }
}
