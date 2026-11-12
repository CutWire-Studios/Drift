pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import Drift
import "../subtitles/SubtitleTime.js" as SubtitleTime

PanelFrame {
    id: root

    property bool shown: true
    property bool follow: true
    property bool timelineTimes: false
    property int editIndex: -1
    property string editColumn: ""
    property int cursorCue: -1

    readonly property real rowHeight: Theme.controlHeight
    readonly property real colNumberWidth: 44
    readonly property real colTimeWidth: 104
    readonly property real colDurationWidth: 76
    readonly property real fixedColumnsWidth: Theme.c.subtitleCueGrid.markerWidth + colNumberWidth
                                              + colTimeWidth * 2 + colDurationWidth

    readonly property int track: EditorState.selectedTrack
    readonly property int clipIndex: EditorState.selectedClip
    readonly property var clipData: EditorState.selectedClipData
    readonly property bool isSubtitle: (clipData?.kind ?? "") === "subtitle"
    readonly property real clipStart: clipData?.start || 0
    readonly property real clipDuration: clipData?.duration || 0
    readonly property real timeOffset: timelineTimes ? clipStart : 0
    readonly property bool captionTargetReady: {
        const kind = clipData?.kind ?? ""
        return kind === "video" || kind === "audio"
    }
    readonly property bool canGenerate: captionTargetReady && Addons.hasKind("whisper-model")
                                        && Addons.runtimeAvailable() && !EditorState.subtitleGenerating
    readonly property var filtered: EditorState.subtitles.filteredCueModel
    readonly property var selectedMap: {
        const map = ({})
        const sel = EditorState.subtitles.selectedCues
        for (let i = 0; i < sel.length; ++i)
            map[sel[i]] = true
        return map
    }
    readonly property int selectedCount: EditorState.subtitles.selectedCues.length
    readonly property int activeCue: EditorState.subtitles.activeCue

    function formatTime(seconds) {
        return SubtitleTime.format(seconds + timeOffset)
    }

    function parseTime(text) {
        return SubtitleTime.parse(text)
    }


    function commitList(list, tag) {
        const items = list.map((c, i) => ({ "c": c, "i": i }))
        items.sort((a, b) => (a.c.start - b.c.start) || (a.i - b.i))
        EditorState.setSubtitleCues(track, clipIndex, items.map(x => x.c))
        return items.findIndex(x => x.i === tag)
    }

    function scrollToCue(cueIndex) {
        const pos = filtered.rowForCue(cueIndex)
        if (pos >= 0)
            list.positionViewAtIndex(pos, ListView.Contain)
    }

    function selectOnly(cueIndex, seek) {
        cursorCue = cueIndex
        EditorState.subtitles.selectCue(cueIndex, 0)
        if (seek)
            EditorState.seekToSubtitleCue(track, clipIndex, cueIndex)
    }

    function clickCue(cueIndex, modifiers) {
        cursorCue = cueIndex
        if (modifiers & Qt.ControlModifier) {
            EditorState.subtitles.selectCue(cueIndex, 1)
        } else if (modifiers & Qt.ShiftModifier) {
            EditorState.subtitles.selectCue(cueIndex, 2)
        } else {
            EditorState.subtitles.selectCue(cueIndex, 0)
            EditorState.seekToSubtitleCue(track, clipIndex, cueIndex)
        }
    }

    function moveCursor(delta, extend) {
        const rows = filtered.count
        if (rows === 0)
            return
        const current = cursorCue >= 0 ? cursorCue : EditorState.subtitles.anchorCue
        let pos = filtered.rowForCue(current)
        if (pos < 0)
            pos = delta > 0 ? -1 : rows
        pos = Math.max(0, Math.min(rows - 1, pos + delta))
        const target = filtered.cueAt(pos)
        if (extend) {
            cursorCue = target
            EditorState.subtitles.selectCue(target, 2)
        } else {
            selectOnly(target, true)
        }
        list.positionViewAtIndex(pos, ListView.Contain)
    }

    function beginEdit(cueIndex, column) {
        if (cueIndex < 0 || cueIndex >= EditorState.subtitles.count)
            return
        cursorCue = cueIndex
        if (!selectedMap[cueIndex])
            EditorState.subtitles.selectCue(cueIndex, 0)
        editColumn = column
        editIndex = cueIndex
    }

    function beginEditAt(cueIndex, x) {
        const timeStart = Theme.c.subtitleCueGrid.markerWidth + colNumberWidth
        if (x >= timeStart && x < timeStart + colTimeWidth)
            beginEdit(cueIndex, "start")
        else if (x >= timeStart + colTimeWidth && x < timeStart + colTimeWidth * 2)
            beginEdit(cueIndex, "end")
        else if (x >= fixedColumnsWidth)
            beginEdit(cueIndex, "text")
    }

    function cancelEdit() {
        editIndex = -1
        editColumn = ""
        forceActiveFocus()
    }

    function commitTime(cueIndex, column, text) {
        if (editIndex !== cueIndex || editColumn !== column)
            return
        cancelEdit()
        const t = parseTime(text)
        if (isNaN(t) || cueIndex >= EditorState.subtitles.count)
            return
        const value = Math.max(0, t - timeOffset)
        const list = EditorState.subtitles.cues(track, clipIndex)
        const cue = list[cueIndex]
        const other = column === "start" ? cue.end : cue.start
        if (column === "start" ? value >= other : value <= other)
            return
        if (Math.abs(value - cue[column]) < 0.0005)
            return
        cue[column] = value
        const at = commitList(list, cueIndex)
        if (at >= 0)
            EditorState.subtitles.setSelectedCues([at])
        cursorCue = at
    }

    function commitText(cueIndex, text) {
        if (editIndex !== cueIndex || editColumn !== "text")
            return
        cancelEdit()
        if (cueIndex >= EditorState.subtitles.count)
            return
        const list = EditorState.subtitles.cues(track, clipIndex)
        if (list[cueIndex].text === text)
            return
        list[cueIndex].text = text
        EditorState.setSubtitleCues(track, clipIndex, list)
    }

    function insertCue(start, end) {
        toolbar.filterText = ""
        const list = EditorState.subtitles.cues(track, clipIndex)
        let at = 0
        while (at < list.length && list[at].start < start)
            ++at
        list.splice(at, 0, { "start": start, "end": end, "text": "", "source": "" })
        EditorState.setSubtitleCues(track, clipIndex, list)
        selectOnly(at, false)
        Qt.callLater(() => {
            scrollToCue(at)
            beginEdit(at, "text")
        })
    }

    function minCueSeconds() {
        return 1 / Math.max(1, EditorState.subtitles.projectFps())
    }

    function addCueAtPlayhead() {
        if (!isSubtitle)
            return
        const dur = clipDuration > 0 ? clipDuration : Infinity
        const cues = EditorState.subtitles.cues(track, clipIndex)
        let start = Math.max(0, Math.min(dur, EditorState.playheadSeconds - clipStart))
        for (let i = 0; i < cues.length; ++i) {
            if (start >= cues[i].start && start < cues[i].end)
                start = cues[i].end
        }
        let end = Math.min(start + 3, dur)
        for (let i = 0; i < cues.length; ++i) {
            if (cues[i].start >= start)
                end = Math.min(end, cues[i].start)
        }
        if (end - start >= minCueSeconds())
            insertCue(start, end)
    }

    function insertAfterSelected() {
        const sel = EditorState.subtitles.selectedCues
        if (!isSubtitle || sel.length === 0)
            return
        const base = Math.max.apply(null, sel)
        const cues = EditorState.subtitles.cues(track, clipIndex)
        if (base >= cues.length)
            return
        const dur = clipDuration > 0 ? clipDuration : Infinity
        const start = cues[base].end
        let end = Math.min(start + 2, dur)
        if (base + 1 < cues.length)
            end = Math.min(end, cues[base + 1].start)
        if (end - start >= minCueSeconds())
            insertCue(start, end)
    }

    function deleteSelected() {
        if (selectedCount === 0)
            return
        const list = EditorState.subtitles.cues(track, clipIndex).filter((c, i) => !selectedMap[i])
        EditorState.subtitles.clearSelection()
        cursorCue = -1
        EditorState.setSubtitleCues(track, clipIndex, list)
    }

    function onClipSwitched() {
        editIndex = -1
        editColumn = ""
        cursorCue = -1
    }

    readonly property var focusItem: Window.activeFocusItem
    readonly property bool wantsActions: shown && isSubtitle && (activeFocus || hover.hovered)

    function textFieldFocused() {
        const f = focusItem
        return !!f && f.cursorPosition !== undefined
    }

    function handleAction(id) {
        if (!shown || !isSubtitle || textFieldFocused())
            return false
        if (id === "selectAll") {
            EditorState.subtitles.selectAll(track, clipIndex)
            return true
        }
        if (id === "delete") {
            deleteSelected()
            return true
        }
        return false
    }

    onWantsActionsChanged: {
        if (wantsActions)
            ShellBridge.subtitleActionHandler = root
        else if (ShellBridge.subtitleActionHandler === root)
            ShellBridge.subtitleActionHandler = null
    }
    Component.onDestruction: {
        if (ShellBridge.subtitleActionHandler === root)
            ShellBridge.subtitleActionHandler = null
    }

    HoverHandler { id: hover }

    focus: true
    activeFocusOnTab: true

    onTrackChanged: onClipSwitched()
    onClipIndexChanged: onClipSwitched()
    onActiveCueChanged: {
        if (follow && shown && EditorState.playing && activeCue >= 0)
            scrollToCue(activeCue)
    }
    onFollowChanged: {
        if (follow && activeCue >= 0)
            scrollToCue(activeCue)
    }

    Keys.onPressed: (event) => {
        if (!root.isSubtitle)
            return
        const mods = event.modifiers
        if (event.key === Qt.Key_Delete) {
            root.deleteSelected()
        } else if (event.key === Qt.Key_A && (mods & Qt.ControlModifier)) {
            EditorState.subtitles.selectAll(root.track, root.clipIndex)
        } else if (event.key === Qt.Key_F && (mods & Qt.ControlModifier)) {
            toolbar.focusFilter()
        } else if (event.key === Qt.Key_Escape) {
            EditorState.subtitles.clearSelection()
            root.cursorCue = -1
        } else if (event.key === Qt.Key_Up || event.key === Qt.Key_Down) {
            root.moveCursor(event.key === Qt.Key_Up ? -1 : 1, !!(mods & Qt.ShiftModifier))
        } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
            const target = root.cursorCue >= 0 ? root.cursorCue : EditorState.subtitles.anchorCue
            root.beginEdit(target, "text")
        } else {
            return
        }
        event.accepted = true
    }

    Binding {
        target: root.filtered
        property: "filterText"
        value: toolbar.filterText
    }

    SubtitleCueGridToolbar {
        id: toolbar
        visible: root.isSubtitle
        width: parent.width
        follow: root.follow
        timelineTimes: root.timelineTimes
        canInsertAfter: root.selectedCount > 0
        canDelete: root.selectedCount > 0
        cueCount: EditorState.subtitles.count
        selectedCount: root.selectedCount
        onAddCue: root.addCueAtPlayhead()
        onInsertAfter: root.insertAfterSelected()
        onDeleteSelected: root.deleteSelected()
        onFollowToggled: root.follow = !root.follow
        onTimelineTimesToggled: root.timelineTimes = !root.timelineTimes
    }

    SubtitleCueGridHeader {
        id: columnHeader
        visible: root.isSubtitle
        anchors.top: toolbar.bottom
        width: parent.width
        panel: root
    }

    ListView {
        id: list
        visible: root.isSubtitle
        anchors.top: columnHeader.bottom
        anchors.bottom: parent.bottom
        width: parent.width
        clip: true
        reuseItems: true
        boundsBehavior: Flickable.StopAtBounds
        model: root.isSubtitle ? root.filtered : null
        delegate: SubtitleCueGridRow {
            panel: root
        }
        ScrollBar.vertical: AppScrollBar { }
    }

    EmptyState {
        anchors.top: columnHeader.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: Theme.spacingLg
        anchors.topMargin: Theme.spacing3xl
        visible: root.isSubtitle && root.filtered.count === 0
        compact: true
        title: EditorState.subtitles.count === 0 ? qsTr("No cues yet") : qsTr("No cues match the filter")
        hint: EditorState.subtitles.count === 0 ? qsTr("Add a cue at the playhead with the + button.") : ""
    }

    EmptyState {
        anchors.fill: parent
        anchors.margins: Theme.spacingLg
        visible: !root.isSubtitle
        glyph: Theme.icons.captions
        title: qsTr("Select a subtitle clip, or generate subtitles")
        actionText: root.canGenerate ? qsTr("Generate subtitles") : ""
        onActionTriggered: EditorState.generateSubtitlesForSelection()
    }
}
