import QtQuick
import QtQuick.Controls.Basic
import Drift
import "."

// Live-lyrics style subtitle editor. The list auto-scrolls and highlights the cue
// currently on screen as the playhead moves, keeping the neighbouring lines visible.
// Editing happens in the fixed panel below so the list always shows context.
// Owns the full panel content area (the list manages its own scrolling).
Item {
    id: root

    property var clip: null
    property var formatSeconds: (function (v) { return Number(v || 0).toFixed(2) })
    property int trackIndex: EditorState.selectedTrack
    property int clipIndex: EditorState.selectedClip

    readonly property double defaultCueDuration: 3.0

    // The cue being edited, shared with the timeline cue lane. Follows the active cue
    // during playback; while paused it only changes on an explicit click / add.
    readonly property int selectedCueIndex: EditorState.selectedSubtitleCue

    readonly property double localPlayhead: {
        void EditorState.playheadSeconds
        void root.clip
        return clip ? EditorState.subtitleLocalPlayheadSeconds(trackIndex, clipIndex) : -1
    }
    readonly property double clipDuration: (clip && clip.duration) ? clip.duration : 0
    readonly property var cues: (clip && clip.subtitleCues) ? clip.subtitleCues : []
    readonly property int activeCueIndex: {
        const t = localPlayhead
        if (t < 0)
            return -1
        for (let i = 0; i < cues.length; i++) {
            if (t >= cues[i].start && t < cues[i].end)
                return i
        }
        return -1
    }
    readonly property var selectedCue: (selectedCueIndex >= 0 && selectedCueIndex < cues.length)
                                       ? cues[selectedCueIndex] : null

    function formatCueTime(seconds) {
        const clamped = Math.max(0, seconds)
        const m = Math.floor(clamped / 60)
        const s = clamped - m * 60
        const whole = Math.floor(s)
        const frac = Math.floor((s - whole) * 1000)
        return String(m).padStart(2, "0") + ":" + String(whole).padStart(2, "0") + "."
               + String(frac).padStart(3, "0")
    }

    function parseCueTime(str) {
        const t = String(str).trim()
        if (t.indexOf(":") >= 0) {
            const parts = t.split(":")
            const m = parseFloat(parts[0])
            const s = parseFloat(parts[1])
            if (isNaN(m) || isNaN(s))
                return NaN
            return m * 60 + s
        }
        return parseFloat(t)
    }

    function replaceCues(newCues) {
        EditorState.setSubtitleCues(trackIndex, clipIndex, newCues)
    }

    function cloneCues() {
        const next = []
        for (let i = 0; i < cues.length; i++)
            next.push({ start: cues[i].start, end: cues[i].end, text: cues[i].text })
        return next
    }

    function updateCue(index, patch) {
        const next = cloneCues()
        if (index >= 0 && index < next.length)
            Object.assign(next[index], patch)
        replaceCues(next)
    }

    function removeCue(index) {
        const next = []
        for (let i = 0; i < cues.length; i++) {
            if (i !== index)
                next.push(cues[i])
        }
        replaceCues(next)
    }

    // Insert an empty cue directly after `index`. It prefers the gap that follows the cue,
    // and when the cues are back to back it takes over the second half of `index` instead —
    // that way only the cue you clicked (+) on is ever re-timed, and everything later stays
    // in sync with the audio.
    function insertCueAfter(index) {
        if (index < 0 || index >= root.cues.length)
            return

        const list = cloneCues()
        const cur = list[index]
        const next = (index + 1 < list.length) ? list[index + 1] : null

        let newStart = cur.end
        let newEnd = next ? Math.min(next.start, cur.end + root.defaultCueDuration)
                          : cur.end + root.defaultCueDuration
        if (!next && root.clipDuration > 0)
            newEnd = Math.min(newEnd, root.clipDuration)

        if (newEnd - newStart < 0.2) {
            const mid = (cur.start + cur.end) / 2
            newStart = mid
            newEnd = cur.end
            cur.end = mid
        }

        list.splice(index + 1, 0, { start: newStart, end: newEnd, text: "" })
        replaceCues(list)
        EditorState.selectedSubtitleCue = index + 1
    }

    function selectCue(index) {
        EditorState.selectedSubtitleCue = index
        EditorState.seekToSubtitleCue(root.trackIndex, root.clipIndex, index)
    }

    function setCueEdgeToPlayhead(index, edge) {
        const t = root.localPlayhead
        if (t < 0 || index < 0 || index >= root.cues.length)
            return
        const cue = root.cues[index]
        if (edge === "start" && t >= cue.end)
            return
        if (edge === "end" && t <= cue.start)
            return
        const patch = {}
        patch[edge] = t
        root.updateCue(index, patch)
    }

    // Add a new cue at the playhead, chaining it to its neighbours: the preceding cue's
    // end is trimmed back to the new start if it would otherwise overlap (never extended),
    // and the new cue runs until the next cue's start (or a default length when there is
    // none). This lets you add subtitles back to back.
    function addCueAtPlayhead() {
        const t = root.localPlayhead
        if (t < 0)
            return

        const list = cloneCues()

        // If a cue already begins here, just select it instead of stacking a duplicate.
        for (let i = 0; i < list.length; i++) {
            if (Math.abs(list[i].start - t) < 0.05) {
                EditorState.selectedSubtitleCue = i
                return
            }
        }

        let prevIdx = -1
        let nextStart = -1
        for (let i = 0; i < list.length; i++) {
            if (list[i].start < t && (prevIdx < 0 || list[i].start > list[prevIdx].start))
                prevIdx = i
            if (list[i].start > t && (nextStart < 0 || list[i].start < nextStart))
                nextStart = list[i].start
        }
        if (prevIdx >= 0 && list[prevIdx].end > t)
            list[prevIdx].end = t

        let newEnd
        if (nextStart >= 0)
            newEnd = nextStart
        else if (root.clipDuration > 0)
            newEnd = Math.min(t + root.defaultCueDuration, root.clipDuration)
        else
            newEnd = t + root.defaultCueDuration
        if (newEnd <= t)
            newEnd = t + 0.5

        list.push({ start: t, end: newEnd, text: "" })
        list.sort(function (a, b) { return a.start - b.start })
        replaceCues(list)

        Qt.callLater(function () {
            for (let i = 0; i < root.cues.length; i++) {
                if (Math.abs(root.cues[i].start - t) < 0.001) {
                    EditorState.selectedSubtitleCue = i
                    break
                }
            }
        })
    }

    // Both editor refreshes run from change handlers on what `selectedCue` is derived from, and a
    // derived binding is not guaranteed to have been re-evaluated by then — reading it there hands
    // back the previously selected cue. Resolve the cue from the source of truth instead.
    function currentCue() {
        const i = EditorState.selectedSubtitleCue
        return (i >= 0 && i < cues.length) ? cues[i] : null
    }

    // Refresh the fields from the model after the cue list was rebuilt elsewhere (a start/end
    // commit, a timeline-lane drag) without fighting an edit in progress.
    function syncEditor() {
        const cue = currentCue()
        if (!cueText.activeFocus)
            cueText.text = cue ? cue.text : ""
        if (!startField.activeFocus)
            startField.text = cue ? formatCueTime(cue.start) : ""
        if (!endField.activeFocus)
            endField.text = cue ? formatCueTime(cue.end) : ""
    }

    // A different cue was picked: its values replace the fields outright. Clicking a row does
    // not move focus out of the text box, so the guards in syncEditor would otherwise leave
    // the previous cue's text sitting there. Anything typed and not applied is dropped.
    function loadEditor() {
        const cue = currentCue()
        cueText.text = cue ? cue.text : ""
        startField.text = cue ? formatCueTime(cue.start) : ""
        endField.text = cue ? formatCueTime(cue.end) : ""
    }

    // While playing, the edited cue tracks the playhead — but not while the text box is being
    // typed into, since following would replace what is in it. When paused it only changes on
    // an explicit click (side list or timeline lane) or when adding a cue.
    onActiveCueIndexChanged: {
        if (EditorState.playing && !cueText.activeFocus)
            EditorState.selectedSubtitleCue = root.activeCueIndex
    }
    onCuesChanged: {
        if (root.selectedCueIndex >= root.cues.length)
            EditorState.selectedSubtitleCue = root.cues.length - 1
        syncEditor()
    }
    onSelectedCueIndexChanged: loadEditor()

    // Reset the editing selection only when a different clip is selected — not on every
    // cue edit (selectedClipData hands back a fresh object each time).
    function resetSelectionForClip() {
        Qt.callLater(function () {
            // loadEditor explicitly: a new clip whose active cue lands on the same index
            // leaves selectedCueIndex unchanged, so nothing else would refresh the fields.
            EditorState.selectedSubtitleCue = root.activeCueIndex
            loadEditor()
        })
    }
    onTrackIndexChanged: resetSelectionForClip()
    onClipIndexChanged: resetSelectionForClip()
    Component.onCompleted: {
        EditorState.selectedSubtitleCue = root.activeCueIndex
        loadEditor()
    }

    Connections {
        target: EditorState
        function onPlayingChanged() {
            if (EditorState.playing)
                EditorState.selectedSubtitleCue = root.activeCueIndex
        }
    }

    // ---- Header ----------------------------------------------------------------
    Column {
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
                color: Theme.panelForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeSm
                font.weight: Font.Medium
            }

            Text {
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("%1 captions").arg(root.cues.length)
                color: Theme.mutedForeground
                font.family: Theme.monoFontFamily
                font.pixelSize: Theme.fontSizeXs
            }
        }

        Text {
            width: parent.width
            wrapMode: Text.WordWrap
            text: qsTr("Play the timeline — the line on screen lights up. Click any line to jump to it and edit it below.")
            color: Theme.mutedForeground
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
                tooltip: qsTr("Replace these captions from a .srt file")
                onClicked: {
                    const url = FileDialogs.openFile(
                        qsTr("Import Subtitles"),
                        [qsTr("SubRip subtitles (*.srt)"), qsTr("All files (*)")])
                    if (url != "")
                        EditorState.importSubtitleFileIntoClip(
                            root.trackIndex, root.clipIndex, url)
                }
            }

            ThemedButton {
                width: (parent.width - parent.spacing) / 2
                text: qsTr("Export")
                variant: "secondary"
                glyph: Theme.icons.save
                tooltip: qsTr("Save captions as a .srt file")
                enabled: root.cues.length > 0
                onClicked: {
                    const url = FileDialogs.saveFile(
                        qsTr("Export Subtitles"),
                        [qsTr("SubRip subtitles (*.srt)")],
                        (root.clip && root.clip.name) ? root.clip.name : EditorState.projectName,
                        "srt")
                    if (url != "")
                        EditorState.exportSubtitleFile(
                            root.trackIndex, root.clipIndex, url)
                }
            }
        }
    }

    // ---- Lyrics list (compact, keeps neighbours in view) -----------------------
    ListView {
        id: listView
        anchors.top: header.bottom
        anchors.topMargin: 8
        anchors.bottom: editorPanel.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: 6
        anchors.rightMargin: 6
        clip: true
        spacing: 2
        model: root.cues

        currentIndex: root.activeCueIndex
        highlightFollowsCurrentItem: true
        highlightRangeMode: ListView.ApplyRange
        preferredHighlightBegin: height * 0.4
        preferredHighlightEnd: height * 0.6
        highlightMoveDuration: 320
        highlightMoveVelocity: -1
        highlight: Item { }

        ScrollBar.vertical: AppScrollBar { }

        delegate: Item {
            id: cueDelegate
            required property int index
            required property var modelData
            readonly property bool isActive: index === root.activeCueIndex
            readonly property bool isSelected: index === root.selectedCueIndex

            width: ListView.view ? ListView.view.width : 0
            height: card.height + (isSelected ? addRow.height : 0)

            Rectangle {
                id: card
                width: parent.width
                height: lineCol.implicitHeight + 14
                radius: Theme.radiusMd
                color: cueDelegate.isSelected ? Theme.panelAccent : (rowHover.containsMouse ? Theme.panelAccent : "transparent")
                border.width: (cueDelegate.isSelected || cueDelegate.isActive || rowHover.containsMouse) ? 1 : 0
                border.color: cueDelegate.isActive ? Theme.clipSubtitle : (cueDelegate.isSelected ? Theme.panelBorder : (rowHover.containsMouse ? Theme.panelBorder : "transparent"))

                Behavior on color { ColorAnimation { duration: Theme.durationFast } }
                Behavior on border.color { ColorAnimation { duration: Theme.durationFast } }

                MouseArea {
                    id: rowHover
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.selectCue(cueDelegate.index)
                }

                Column {
                    id: lineCol
                    x: 12
                    y: 7
                    // The bin's slot is reserved even while it is hidden, so the wrapped
                    // caption text doesn't reflow as the pointer moves down the list.
                    width: parent.width - 24 - deleteButton.width
                    spacing: 2

                    Text {
                        text: root.formatCueTime(cueDelegate.modelData.start) + "  →  "
                              + root.formatCueTime(cueDelegate.modelData.end)
                        color: Theme.mutedForeground
                        font.family: Theme.monoFontFamily
                        font.pixelSize: Theme.fontSizeXs
                        opacity: (cueDelegate.isActive || cueDelegate.isSelected) ? 0.95 : (rowHover.containsMouse ? 0.85 : 0.65)
                        Behavior on opacity { NumberAnimation { duration: 150 } }
                    }

                    Text {
                        width: parent.width
                        wrapMode: Text.WordWrap
                        text: (cueDelegate.modelData.text && cueDelegate.modelData.text.length)
                              ? cueDelegate.modelData.text : qsTr("(empty)")
                        font.family: Theme.fontFamily
                        font.pixelSize: cueDelegate.isActive ? Theme.fontSizeBase : Theme.fontSizeSm
                        font.weight: cueDelegate.isActive ? Font.DemiBold : Font.Normal
                        font.italic: !(cueDelegate.modelData.text && cueDelegate.modelData.text.length)
                        color: cueDelegate.isActive ? Theme.clipSubtitle
                               : (cueDelegate.isSelected || rowHover.containsMouse ? Theme.panelForeground : Theme.foreground)
                        opacity: (cueDelegate.isActive || cueDelegate.isSelected) ? 1.0 : (rowHover.containsMouse ? 0.95 : 0.82)
                        Behavior on opacity { NumberAnimation { duration: 200 } }
                        Behavior on color { ColorAnimation { duration: 200 } }
                    }
                }

                IconButton {
                    id: deleteButton
                    anchors.right: parent.right
                    anchors.rightMargin: 6
                    anchors.verticalCenter: parent.verticalCenter
                    glyph: Theme.icons.trash
                    variant: "ghost"
                    iconSize: Theme.iconSizeSm
                    tooltip: qsTr("Delete this subtitle")
                    opacity: (rowHover.containsMouse || cueDelegate.isSelected || visualFocus) ? 1 : 0
                    enabled: opacity > 0
                    onClicked: root.removeCue(cueDelegate.index)

                    Behavior on opacity { NumberAnimation { duration: Theme.durationFast } }
                }
            }

            // Insertion point for the cue that follows the one being edited.
            Item {
                id: addRow
                anchors.top: card.bottom
                width: parent.width
                height: 30
                visible: cueDelegate.isSelected

                IconButton {
                    anchors.centerIn: parent
                    glyph: Theme.icons.plus
                    variant: "ghost"
                    iconSize: Theme.iconSizeSm
                    tooltip: qsTr("Add a subtitle after this one")
                    onClicked: root.insertCueAfter(cueDelegate.index)
                }
            }
        }

        // Empty state
        Column {
            anchors.centerIn: parent
            width: Math.min(220, parent.width - 24)
            spacing: 10
            visible: root.cues.length === 0

            IconGlyph {
                anchors.horizontalCenter: parent.horizontalCenter
                glyph: Theme.icons.captions
                iconSize: 24
                iconColor: Theme.mutedForeground
            }
            Text {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                text: qsTr("No subtitles yet. Move to a time inside this clip and add one below.")
                color: Theme.mutedForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
            }
        }
    }

    // ---- Editor + add panel (capped so the cue list keeps space; scrolls when short)
    Rectangle {
        id: editorPanel
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        readonly property real naturalHeight: panelCol.implicitHeight + 24
        readonly property real minListHeight: 80
        readonly property real minEditorHeight: 72
        readonly property real headerBlock: header.height + header.anchors.topMargin
                                           + listView.anchors.topMargin
        height: Math.min(naturalHeight,
                         Math.max(minEditorHeight,
                                  parent.height - headerBlock - minListHeight))
        color: Theme.panelBackground

        Rectangle {
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            height: 1
            color: Theme.panelBorder
        }

        Flickable {
            id: editorFlick
            anchors.fill: parent
            anchors.margins: 12
            contentWidth: width
            contentHeight: panelCol.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            interactive: contentHeight > height
            ScrollBar.vertical: AppScrollBar {
                policy: editorFlick.contentHeight > editorFlick.height
                        ? ScrollBar.AlwaysOn : ScrollBar.AsNeeded
            }

            Column {
                id: panelCol
                width: parent.width
                spacing: 8

                // Editor for the selected cue.
                Column {
                    width: parent.width
                    spacing: 8
                    visible: root.selectedCue !== null

                    Row {
                        width: parent.width
                        spacing: 6

                        ThemedTextArea {
                            id: cueText
                            width: parent.width - applyButton.width - parent.spacing
                            height: 56
                            placeholderText: qsTr("Type subtitle…")
                            onEditingFinished: {
                                if (root.selectedCueIndex >= 0)
                                    root.updateCue(root.selectedCueIndex, { text: text })
                            }
                        }

                        // Explicit "apply text" affordance (separate from adding a cue).
                        Rectangle {
                            id: applyButton
                            width: 40
                            height: 56
                            radius: Theme.radiusSm
                            readonly property bool dirty: root.selectedCue !== null
                                                          && cueText.text !== (root.selectedCue ? root.selectedCue.text : "")
                            color: applyMouse.containsMouse ? Qt.lighter(Theme.primary, 1.08) : Theme.primary
                            opacity: (dirty || applyMouse.containsMouse) ? 1 : 0.5

                            // Was a raw "✓" glyph while every other button in the app
                            // uses IconGlyph + Theme.icons.
                            IconGlyph {
                                anchors.centerIn: parent
                                glyph: Theme.icons.check
                                iconSize: Theme.iconSizeBase
                                iconColor: Theme.primaryForeground
                            }

                            MouseArea {
                                id: applyMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    if (root.selectedCueIndex >= 0)
                                        root.updateCue(root.selectedCueIndex, { text: cueText.text })
                                }
                            }

                            ThemedToolTip {
                                visible: applyMouse.containsMouse
                                text: qsTr("Apply text to this subtitle")
                            }
                        }
                    }

                    Row {
                        width: parent.width
                        spacing: 8

                        // Start
                        Column {
                            width: (parent.width - parent.spacing) / 2
                            spacing: 4
                            Text {
                                text: qsTr("Start")
                                color: Theme.mutedForeground
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontSizeXs
                            }
                            Row {
                                width: parent.width
                                spacing: 4
                                ThemedTextField {
                                    id: startField
                                    width: parent.width - startPh.width - parent.spacing
                                    onEditingFinished: {
                                        const v = root.parseCueTime(text)
                                        if (!isNaN(v) && root.selectedCueIndex >= 0)
                                            root.updateCue(root.selectedCueIndex, { start: v })
                                    }
                                }
                                IconButton {
                                    id: startPh
                                    glyph: Theme.icons.setStart
                                    variant: "ghost"
                                    enabled: root.localPlayhead >= 0
                                    tooltip: qsTr("Set start to current time")
                                    onClicked: root.setCueEdgeToPlayhead(root.selectedCueIndex, "start")
                                }
                            }
                        }

                        // End
                        Column {
                            width: (parent.width - parent.spacing) / 2
                            spacing: 4
                            Text {
                                text: qsTr("End")
                                color: Theme.mutedForeground
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontSizeXs
                            }
                            Row {
                                width: parent.width
                                spacing: 4
                                ThemedTextField {
                                    id: endField
                                    width: parent.width - endPh.width - parent.spacing
                                    onEditingFinished: {
                                        const v = root.parseCueTime(text)
                                        if (!isNaN(v) && root.selectedCueIndex >= 0)
                                            root.updateCue(root.selectedCueIndex, { end: v })
                                    }
                                }
                                IconButton {
                                    id: endPh
                                    glyph: Theme.icons.setEnd
                                    variant: "ghost"
                                    enabled: root.localPlayhead >= 0
                                    tooltip: qsTr("Set end to current time")
                                    onClicked: root.setCueEdgeToPlayhead(root.selectedCueIndex, "end")
                                }
                            }
                        }
                    }

                    ThemedButton {
                        text: qsTr("Delete caption")
                        variant: "destructive"
                        glyph: Theme.icons.trash
                        onClicked: {
                            if (root.selectedCueIndex >= 0)
                                root.removeCue(root.selectedCueIndex)
                        }
                    }

                    Rectangle {
                        width: parent.width
                        height: 1
                        color: Theme.panelBorder
                        opacity: 0.6
                    }
                }

                Text {
                    width: parent.width
                    text: root.localPlayhead >= 0
                          ? qsTr("At %1").arg(root.formatCueTime(root.localPlayhead))
                          : qsTr("Move to a time inside this clip to add a subtitle")
                    color: Theme.mutedForeground
                    font.family: Theme.monoFontFamily
                    font.pixelSize: Theme.fontSizeXs
                }

                ThemedButton {
                    width: parent.width
                    variant: "primary"
                    glyph: Theme.icons.plus
                    enabled: root.localPlayhead >= 0
                    text: qsTr("Add subtitle at current time")
                    onClicked: root.addCueAtPlayhead()
                }
            }
        }
    }
}
