import QtQuick
import QtQuick.Controls.Basic
import Drift
// Live-lyrics style subtitle editor. The list auto-scrolls and highlights the cue
// currently on screen as the playhead moves, keeping the neighbouring lines visible.
// Editing happens in the fixed panel below so the list always shows context.
// Owns the full panel content area (the list manages its own scrolling).
Item {
    id: root

    property var clipData: null
    property var formatSeconds: (function (v) { return Number(v || 0).toFixed(2) })
    property int trackIndex: EditorState.selectedTrack
    property int clipIndex: EditorState.selectedClip

    // The phone's properties sheet: a cue is edited in the keyboard composer instead of the
    // panel below the list, so a tap on a row asks the editor page to open it.
    property bool compactEdit: false
    signal cueEditRequested(int index)

    readonly property double defaultCueDuration: 3.0

    // The cue being edited, shared with the timeline cue lane. Follows the active cue
    // during playback; while paused it only changes on an explicit click / add.
    readonly property int selectedCueIndex: EditorState.selectedSubtitleCue

    readonly property double localPlayhead: {
        void EditorState.inspectorPlayheadSeconds
        void root.clipData
        return clipData ? EditorState.subtitleLocalPlayheadSeconds(trackIndex, clipIndex) : -1
    }
    readonly property double clipDuration: (clipData && clipData.duration) ? clipData.duration : 0
    readonly property var cues: (clipData && clipData.subtitleCues) ? clipData.subtitleCues : []
    readonly property int activeCueIndex: EditorState.subtitles.activeCue
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
            next.push({ start: cues[i].start, end: cues[i].end, text: cues[i].text, source: cues[i].source })
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
        if (!editorPanel.cueText.activeFocus)
            editorPanel.cueText.text = cue ? cue.text : ""
        if (!editorPanel.startField.activeFocus)
            editorPanel.startField.text = cue ? formatCueTime(cue.start) : ""
        if (!editorPanel.endField.activeFocus)
            editorPanel.endField.text = cue ? formatCueTime(cue.end) : ""
    }

    // A different cue was picked: its values replace the fields outright. Clicking a row does
    // not move focus out of the text box, so the guards in syncEditor would otherwise leave
    // the previous cue's text sitting there. Anything typed and not applied is dropped.
    function loadEditor() {
        const cue = currentCue()
        editorPanel.cueText.text = cue ? cue.text : ""
        editorPanel.startField.text = cue ? formatCueTime(cue.start) : ""
        editorPanel.endField.text = cue ? formatCueTime(cue.end) : ""
    }

    // While playing, the edited cue tracks the playhead — but not while the text box is being
    // typed into, since following would replace what is in it. When paused it only changes on
    // an explicit click (side list or timeline lane) or when adding a cue.
    onActiveCueIndexChanged: {
        if (EditorState.playing && !editorPanel.cueText.activeFocus)
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
    SubtitleEditorHeader {
        id: header
        editor: root
    }

    // ---- Lyrics list (compact, keeps neighbours in view) -----------------------
    ListView {
        id: listView
        reuseItems: true
        anchors.top: header.bottom
        anchors.topMargin: 8
        anchors.bottom: editorPanel.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: 6
        anchors.rightMargin: 6
        clip: true
        spacing: 2
        model: EditorState.subtitles.cueModel

        currentIndex: root.activeCueIndex
        highlightFollowsCurrentItem: true
        highlightRangeMode: ListView.ApplyRange
        preferredHighlightBegin: height * 0.4
        preferredHighlightEnd: height * 0.6
        highlightMoveDuration: 320
        highlightMoveVelocity: -1
        highlight: Item { }

        ScrollBar.vertical: AppScrollBar { }

        delegate: SubtitleCueCard {
            editor: root
        }

        // Empty state
        SubtitleCueEmptyState {
            anchors.centerIn: parent
            width: Math.min(220, parent.width - 24)
            editor: root
        }
    }

    SubtitleEditorPanel {
        id: editorPanel
        editor: root
        headerItem: header
        cueList: listView
    }
}
