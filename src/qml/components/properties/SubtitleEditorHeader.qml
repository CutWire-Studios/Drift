import QtQuick
import Drift

Column {
    property var editor
    id: header
    readonly property bool phone: Theme.touchInput && Theme.compact
    property bool timelineTimes: false

    function importCaptions() {
        const url = FileDialogs.openFile(
            qsTr("Import Subtitles"),
            [qsTr("Subtitles (*.srt *.vtt)"), qsTr("All files (*)")])
        if (url != "")
            EditorState.subtitles.importFile(editor.trackIndex, editor.clipIndex, url)
    }

    function exportCaptions() {
        const url = FileDialogs.saveFile(
            qsTr("Export Subtitles"),
            [qsTr("SubRip subtitles (*.srt)"), qsTr("WebVTT subtitles (*.vtt)")],
            (editor.clipData && editor.clipData.name) ? editor.clipData.name : EditorState.projectFile.projectName,
            "srt")
        if (url != "")
            EditorState.subtitles.exportFile(editor.trackIndex, editor.clipIndex, url, header.timelineTimes)
    }
    anchors.top: parent.top
    anchors.left: parent.left
    anchors.right: parent.right
    anchors.margins: 12
    spacing: 4

    Item {
        width: parent.width
        height: Math.max(titleText.implicitHeight, phone ? Theme.androidRowHeight : Theme.controlHeightSm)

        Text {
            id: titleText
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            text: qsTr("Subtitles")
            color: Theme.c.subtitleEditorHeader.foreground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.touchInput && Theme.compact ? Theme.fontSizeBase : Theme.fontSizeSm
            font.weight: Font.Medium
        }

        IconButton {
            anchors.left: titleText.right
            anchors.leftMargin: Theme.spacingSm
            anchors.verticalCenter: parent.verticalCenter
            buttonSize: Theme.controlHeightSm
            iconSize: Theme.iconSizeSm
            variant: "ghost"
            glyph: Theme.icons.info
            tooltip: editor.compactEdit
                     ? qsTr("Play the timeline — the line on screen lights up. Tap any line to edit it.")
                     : qsTr("Play the timeline — the line on screen lights up. Click any line to jump to it and edit it below.")
        }

        IconButton {
            id: moreButton
            visible: header.phone
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            buttonSize: Theme.androidMinTouchTarget
            iconSize: Theme.iconSizeMd
            variant: "ghost"
            glyph: Theme.icons.ellipsis
            tooltip: qsTr("Import and export")
            onClicked: moreMenu.popup(moreButton, 0, moreButton.height)

            ThemedContextMenu {
                id: moreMenu

                ThemedMenuItem {
                    text: qsTr("Import subtitles…")
                    icon.name: Theme.icons.upload
                    onTriggered: header.importCaptions()
                }
                ThemedMenuItem {
                    text: qsTr("Export subtitles…")
                    icon.name: Theme.icons.save
                    enabled: editor.cues.length > 0
                    onTriggered: header.exportCaptions()
                }
                ThemedMenuItem {
                    text: qsTr("Timestamps from the start of the video")
                    icon.name: header.timelineTimes ? Theme.icons.check : ""
                    onTriggered: header.timelineTimes = !header.timelineTimes
                }
            }
        }

        Text {
            anchors.right: moreButton.visible ? moreButton.left : parent.right
            anchors.verticalCenter: parent.verticalCenter
            text: qsTr("%1 captions").arg(editor.cues.length)
            color: Theme.c.subtitleEditorHeader.foregroundMuted
            font.family: Theme.touchInput && Theme.compact ? Theme.fontFamily : Theme.monoFontFamily
            font.pixelSize: Theme.touchInput && Theme.compact ? Theme.fontSizeSm : Theme.fontSizeXs
        }
    }

    Row {
        visible: !header.phone
        width: parent.width
        height: visible ? implicitHeight : 0
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
                        editor.trackIndex, editor.clipIndex, url, header.timelineTimes)
            }
        }
    }

    ThemedCheckBox {
        id: timelineTimesBox
        visible: !header.phone
        height: visible ? implicitHeight : 0
        checked: header.timelineTimes
        onToggled: header.timelineTimes = checked
        width: parent.width
        text: qsTr("Timestamps from the start of the video")
        tooltip: qsTr("Offset the exported captions by this clip's start so they match the exported video")
    }
}
