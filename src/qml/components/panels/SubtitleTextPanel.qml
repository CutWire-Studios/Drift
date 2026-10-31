pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import Drift

PanelFrame {
    id: root
    SearchAnchor {
        searchId: "subtitle-text-editor"
        title: qsTr("Subtitle text editor")
        keywords: "captions srt vtt paste edit import export source"
    }

    property bool shown: true
    property string format: "srt"
    property bool timelineTimes: false
    property string loadedText: ""
    property bool dirty: false
    property bool loading: false
    property bool externalChange: false
    property int loadedTrack: -1
    property int loadedClip: -1
    property var pending: null
    property var parseResult: ({ "count": 0, "errorCount": 0, "errors": [] })

    readonly property int track: EditorState.selectedTrack
    readonly property int clip: EditorState.selectedClip
    readonly property var clipData: EditorState.selectedClipData
    readonly property bool isSubtitle: (clipData?.kind ?? "") === "subtitle"
    readonly property bool clipMismatch: externalChange && (loadedTrack !== track || loadedClip !== clip)
    readonly property bool canApply: dirty && parseResult.count > 0 && !clipMismatch && isSubtitle
    readonly property var errorLines: {
        const map = ({})
        for (const e of parseResult.errors)
            map[e.line] = true
        return map
    }

    function syncCombo() {
        formatBox.currentIndex = format === "vtt" ? 1 : 0
    }

    function load() {
        if (!isSubtitle)
            return
        loading = true
        editor.text = EditorState.subtitles.cuesText(track, clip, format, timelineTimes)
        loadedText = editor.text
        loading = false
        loadedTrack = track
        loadedClip = clip
        dirty = false
        externalChange = false
        pending = null
        reparse()
    }

    function reparse() {
        parseTimer.stop()
        parseResult = EditorState.subtitles.parseText(editor.text, format)
    }

    function requestChange(change) {
        if (dirty) {
            pending = change
            syncCombo()
            return
        }
        applyChange(change)
    }

    function applyChange(change) {
        if (change.format !== undefined)
            format = change.format
        if (change.timelineTimes !== undefined)
            timelineTimes = change.timelineTimes
        syncCombo()
        load()
    }

    function applyEdits() {
        if (!canApply)
            return
        if (EditorState.subtitles.applyText(track, clip, editor.text, format, timelineTimes))
            load()
    }

    function lineStart(line) {
        const lines = editor.text.split("\n")
        let pos = 0
        for (let i = 0; i < Math.min(line - 1, lines.length); ++i)
            pos += lines[i].length + 1
        return pos
    }

    function jumpToFirstError() {
        if (parseResult.errors.length === 0)
            return
        editor.forceActiveFocus()
        editor.cursorPosition = lineStart(parseResult.errors[0].line)
    }

    function importFile() {
        const url = FileDialogs.openFile(
            qsTr("Import Subtitles"),
            [qsTr("Subtitles (*.srt *.vtt)"), qsTr("All files (*)")])
        if (url == "")
            return
        if (EditorState.subtitles.importFile(track, clip, url)) {
            format = String(url).toLowerCase().endsWith(".vtt") ? "vtt" : "srt"
            syncCombo()
            load()
        }
    }

    function exportFile() {
        const url = FileDialogs.saveFile(
            qsTr("Export Subtitles"),
            [qsTr("SubRip subtitles (*.srt)"), qsTr("WebVTT subtitles (*.vtt)")],
            clipData.name || EditorState.projectFile.projectName,
            format)
        if (url != "")
            EditorState.subtitles.exportFile(track, clip, url, timelineTimes)
    }

    function copyAll() {
        editor.selectAll()
        editor.copy()
        editor.deselect()
    }

    function onTextEdited() {
        if (loading)
            return
        dirty = editor.text !== loadedText
        const detected = EditorState.subtitles.detectFormat(editor.text)
        if (detected !== format && (detected === "vtt" || /^\s*\d+\s*\r?\n\s*[\d:,.]+\s*-->/.test(editor.text))) {
            format = detected
            syncCombo()
        }
        parseTimer.restart()
    }

    focus: true
    onTrackChanged: Qt.callLater(onClipSwitched)
    onClipChanged: Qt.callLater(onClipSwitched)
    onIsSubtitleChanged: Qt.callLater(onClipSwitched)
    Component.onCompleted: load()

    function onClipSwitched() {
        if (!isSubtitle)
            return
        if (dirty)
            externalChange = true
        else
            load()
    }

    Connections {
        target: EditorState.subtitles

        function onCuesChanged() {
            if (!root.isSubtitle)
                return
            if (root.dirty)
                root.externalChange = true
            else
                root.load()
        }
    }

    Timer {
        id: parseTimer
        interval: 300
        onTriggered: root.reparse()
    }

    Item {
        id: toolbar
        visible: root.isSubtitle
        width: parent.width
        implicitHeight: toolbarFlow.height + Theme.spacingMd * 2
        height: implicitHeight

        Rectangle {
            anchors.bottom: parent.bottom
            width: parent.width
            height: Theme.c.subtitleTextPanel.borderWidth
            color: Theme.c.subtitleTextPanel.border
        }

        Flow {
            id: toolbarFlow
            x: Theme.spacingMd
            y: Theme.spacingMd
            width: parent.width - Theme.spacingMd * 2
            spacing: Theme.spacingXs

            ThemedComboBox {
                id: formatBox
                width: widestContentWidth + Theme.spacingLg
                model: [qsTr("SRT"), qsTr("WebVTT")]
                currentIndex: 0
                onActivated: root.requestChange({ "format": currentIndex === 1 ? "vtt" : "srt" })
            }

            ThemedToggleButton {
                text: qsTr("Timeline")
                checked: root.timelineTimes
                tooltip: qsTr("Show times from the start of the timeline instead of the start of the clip")
                onClicked: root.requestChange({ "timelineTimes": !root.timelineTimes })
            }

            ThemedButton {
                text: qsTr("Apply")
                variant: "primary"
                enabled: root.canApply
                tooltip: qsTr("Replace the cues with this text (Ctrl+S)")
                onClicked: root.applyEdits()
            }

            ThemedButton {
                text: qsTr("Revert")
                enabled: root.dirty
                tooltip: qsTr("Discard edits and reload the text from the cues")
                onClicked: root.load()
            }

            ThemedButton {
                text: qsTr("Import")
                glyph: Theme.icons.upload
                tooltip: qsTr("Replace the cues from a .srt or .vtt file")
                onClicked: root.importFile()
            }

            ThemedButton {
                text: qsTr("Export")
                glyph: Theme.icons.save
                tooltip: qsTr("Save the applied cues as a .srt or .vtt file")
                onClicked: root.exportFile()
            }

            ThemedButton {
                text: qsTr("Copy all")
                glyph: Theme.icons.copy
                onClicked: root.copyAll()
            }
        }
    }

    Rectangle {
        id: banner
        readonly property bool confirming: root.pending !== null
        visible: root.isSubtitle && (confirming || root.externalChange)
        anchors.top: toolbar.bottom
        width: parent.width
        height: visible ? bannerRow.height + Theme.spacingMd * 2 : 0
        color: Theme.c.subtitleTextPanel.bannerBackground

        Flow {
            id: bannerRow
            x: Theme.spacingMd
            y: Theme.spacingMd
            width: parent.width - Theme.spacingMd * 2
            spacing: Theme.spacingMd

            Text {
                height: Theme.controlHeightSm
                verticalAlignment: Text.AlignVCenter
                text: banner.confirming ? qsTr("Discard edits?")
                                        : (root.clipMismatch ? qsTr("A different clip is selected")
                                                             : qsTr("Subtitles changed outside this editor"))
                color: Theme.c.subtitleTextPanel.bannerForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeSm
            }

            ThemedButton {
                visible: banner.confirming
                text: qsTr("Discard")
                onClicked: root.applyChange(root.pending)
            }
            ThemedButton {
                visible: banner.confirming
                text: qsTr("Cancel")
                onClicked: root.pending = null
            }
            ThemedButton {
                visible: !banner.confirming
                text: qsTr("Reload")
                onClicked: root.load()
            }
            ThemedButton {
                visible: !banner.confirming && !root.clipMismatch
                text: qsTr("Keep mine")
                onClicked: root.externalChange = false
            }
        }
    }

    Item {
        id: body
        visible: root.isSubtitle
        anchors.top: banner.bottom
        anchors.bottom: status.top
        width: parent.width

        FontMetrics {
            id: metrics
            font: editor.font
        }

        readonly property real lineHeight: editor.lineCount > 0 ? Math.max(1, editor.contentHeight / editor.lineCount)
                                                                 : metrics.height

        Rectangle {
            id: gutterBox
            width: Math.max(String(editor.lineCount).length, 2) * metrics.averageCharacterWidth + Theme.spacingLg * 2
            height: parent.height
            color: Theme.c.subtitleTextPanel.gutterBackground
            clip: true

            ListView {
                id: gutter
                anchors.fill: parent
                interactive: false
                model: editor.lineCount
                delegate: Text {
                    required property int index
                    width: gutter.width
                    height: body.lineHeight
                    rightPadding: Theme.spacingLg
                    horizontalAlignment: Text.AlignRight
                    verticalAlignment: Text.AlignVCenter
                    text: index + 1
                    color: root.errorLines[index + 1] ? Theme.c.subtitleTextPanel.gutterForegroundError
                                                      : Theme.c.subtitleTextPanel.gutterForeground
                    font: editor.font
                }
            }

            Binding {
                target: gutter
                property: "contentY"
                value: flick.contentY - editor.topPadding
            }
        }

        Flickable {
            id: flick
            anchors.left: gutterBox.right
            anchors.right: parent.right
            height: parent.height
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: AppScrollBar { }
            ScrollBar.horizontal: AppScrollBar { }

            TextArea.flickable: TextArea {
                id: editor
                wrapMode: TextArea.NoWrap
                color: Theme.c.subtitleTextPanel.foreground
                selectionColor: Theme.c.subtitleTextPanel.selection
                selectedTextColor: Theme.c.subtitleTextPanel.selectionForeground
                font.family: Theme.monoFontFamily
                font.pixelSize: Theme.fontSizeSm
                selectByMouse: true
                persistentSelection: true
                background: Rectangle {
                    color: Theme.c.subtitleTextPanel.background
                }
                onTextChanged: root.onTextEdited()
                Keys.onPressed: (event) => {
                    if (event.key === Qt.Key_S && (event.modifiers & Qt.ControlModifier)) {
                        root.applyEdits()
                        event.accepted = true
                    }
                }
            }
        }
    }

    Rectangle {
        id: status
        visible: root.isSubtitle
        anchors.bottom: parent.bottom
        width: parent.width
        height: Theme.controlHeightSm + Theme.spacingXs
        color: Theme.c.subtitleTextPanel.bannerBackground

        Row {
            anchors.left: parent.left
            anchors.leftMargin: Theme.spacingMd
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.spacingSm

            Text {
                text: qsTr("%n cue(s)", "", root.parseResult.count)
                color: Theme.c.subtitleTextPanel.foreground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
            }

            Text {
                id: errorSummary
                visible: root.parseResult.errorCount > 0
                text: "· " + qsTr("%n error(s)", "", root.parseResult.errorCount)
                color: Theme.c.subtitleTextPanel.foregroundError
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs

                HoverHandler {
                    id: errorHover
                    cursorShape: Qt.PointingHandCursor
                }
                TapHandler {
                    onTapped: root.jumpToFirstError()
                }
                ThemedToolTip {
                    visible: errorHover.hovered
                    text: root.parseResult.errors.slice(0, 5)
                              .map(e => qsTr("Line %1: %2").arg(e.line).arg(e.message)).join("\n")
                }
            }
        }
    }

    EmptyState {
        anchors.centerIn: parent
        width: Math.min(parent.width - Theme.spacing3xl, 280)
        visible: !root.isSubtitle
        compact: true
        title: qsTr("Select a subtitle clip")
        hint: qsTr("Pick a subtitle clip on the timeline to edit its text here.")
    }
}
