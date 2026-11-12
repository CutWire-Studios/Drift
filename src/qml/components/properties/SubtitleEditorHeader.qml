import QtQuick
import Drift

Column {
    property var editor
    id: header
    anchors.top: parent.top
    anchors.left: parent.left
    anchors.right: parent.right
    anchors.margins: 12
    spacing: 4

    Item {
        width: parent.width
        height: titleText.implicitHeight

        Text {
            id: titleText
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            text: qsTr("Subtitles")
            color: Theme.c.subtitleEditorHeader.foreground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeSm
            font.weight: Font.Medium
        }

        Text {
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            text: qsTr("%1 captions").arg(editor.cues.length)
            color: Theme.c.subtitleEditorHeader.foregroundMuted
            font.family: Theme.monoFontFamily
            font.pixelSize: Theme.fontSizeXs
        }
    }

    Text {
        width: parent.width
        wrapMode: Text.WordWrap
        text: editor.compactEdit
              ? qsTr("Play the timeline — the line on screen lights up. Tap any line to edit it.")
              : qsTr("Play the timeline — the line on screen lights up. Click any line to jump to it and edit it below.")
        color: Theme.c.subtitleEditorHeader.foregroundMuted
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSizeXs
        opacity: 0.8
    }

    Row {
        width: parent.width
        spacing: 6

        ThemedButton {
            width: (parent.width - parent.spacing) / 2
            text: qsTr("Import")
            variant: "secondary"
            glyph: Theme.icons.upload
            tooltip: qsTr("Replace these captions from a .srt or .vtt file")
            onClicked: {
                const url = FileDialogs.openFile(
                    qsTr("Import Subtitles"),
                    [qsTr("Subtitles (*.srt *.vtt)"), qsTr("All files (*)")])
                if (url != "")
                    EditorState.subtitles.importFile(
                        editor.trackIndex, editor.clipIndex, url)
            }
        }

        ThemedButton {
            width: (parent.width - parent.spacing) / 2
            text: qsTr("Export")
            variant: "secondary"
            glyph: Theme.icons.save
            tooltip: qsTr("Save captions as a .srt or .vtt file")
            enabled: editor.cues.length > 0
            onClicked: {
                const url = FileDialogs.saveFile(
                    qsTr("Export Subtitles"),
                    [qsTr("SubRip subtitles (*.srt)"), qsTr("WebVTT subtitles (*.vtt)")],
                    (editor.clipData && editor.clipData.name) ? editor.clipData.name : EditorState.projectFile.projectName,
                    "srt")
                if (url != "")
                    EditorState.subtitles.exportFile(
                        editor.trackIndex, editor.clipIndex, url, timelineTimesBox.checked)
            }
        }
    }

    ThemedCheckBox {
        id: timelineTimesBox
        width: parent.width
        text: qsTr("Timestamps from the start of the video")
        tooltip: qsTr("Offset the exported captions by this clip's start so they match the exported video")
    }
}
