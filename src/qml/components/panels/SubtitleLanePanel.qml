pragma ComponentBehavior: Bound
import QtQuick
import Drift

PanelFrame {
    id: root

    property bool shown: false
    property bool follow: true
    property bool gestureActive: false
    property int editingIndex: -1
    property real cueAreaHeight: 0
    property real laneWidth: 0
    property var liveCues: []
    property int cuesRevision: 0

    readonly property alias timeView: timeViewObject
    readonly property int track: EditorState.selectedTrack
    readonly property int clip: EditorState.selectedClip
    readonly property var clipData: EditorState.selectedClipData
    readonly property bool hasClip: track >= 0 && clip >= 0 && Object.keys(clipData).length > 0
    readonly property bool isSubtitle: hasClip && clipData.kind === "subtitle"
    readonly property real clipStart: clipData.start || 0
    readonly property real clipDuration: clipData.duration || 0
    readonly property var committedCues: {
        void EditorState.tracksRevision
        void cuesRevision
        return isSubtitle && clipData.subtitleCues ? copyCues(clipData.subtitleCues) : []
    }
    readonly property var mediaRange: {
        void EditorState.tracksRevision
        return shown && isSubtitle ? EditorState.mediaExtentSeconds() : ({ "start": 0, "duration": 0 })
    }
    readonly property var selectedMap: {
        const map = ({})
        const sel = EditorState.subtitles.selectedCues
        for (let i = 0; i < sel.length; ++i)
            map[sel[i]] = true
        return map
    }
    readonly property int selectedCount: EditorState.subtitles.selectedCues.length
    readonly property int activeCue: EditorState.subtitles.activeCue

    function copyCues(list) {
        return list.map(c => ({ "start": c.start, "end": c.end, "text": c.text, "source": c.source }))
    }

    function isSelected(index) {
        return !!selectedMap[index]
    }

    function selectedIndices() {
        return Object.keys(selectedMap).map(Number).sort((a, b) => a - b)
    }

    function updateClipSpan() {
        timeViewObject.clipStart = clipStart
        timeViewObject.clipDuration = Math.max(0.1, clipDuration || 1)
    }

    function onClipSwitched() {
        editingIndex = -1
        liveCues = committedCues
        updateClipSpan()
        timeViewObject.fitClip()
    }

    function beginEdit(index) {
        if (index >= 0 && index < liveCues.length)
            editingIndex = index
    }

    function finishEdit() {
        editingIndex = -1
        forceActiveFocus()
    }

    function cancelEdit() {
        finishEdit()
    }

    function commitEdit(text) {
        const i = editingIndex
        finishEdit()
        if (i < 0 || i >= liveCues.length || liveCues[i].text === text)
            return
        const list = copyCues(liveCues)
        list[i].text = text
        EditorState.setSubtitleCues(track, clip, list)
    }

    function insertCue(start, end) {
        const list = copyCues(liveCues)
        let at = 0
        while (at < list.length && list[at].start < start)
            ++at
        list.splice(at, 0, { "start": start, "end": end, "text": "", "source": "" })
        liveCues = list
        EditorState.setSubtitleCues(track, clip, list)
        EditorState.subtitles.setSelectedCues([at])
        beginEdit(at)
    }

    function addCueAtPlayhead() {
        if (!isSubtitle)
            return
        const dur = clipDuration > 0 ? clipDuration : Infinity
        let start = Math.max(0, Math.min(dur, EditorState.playheadSeconds - clipStart))
        for (let i = 0; i < liveCues.length; ++i) {
            if (start >= liveCues[i].start && start < liveCues[i].end)
                start = liveCues[i].end
        }
        let end = Math.min(start + 3, dur)
        for (let i = 0; i < liveCues.length; ++i) {
            if (liveCues[i].start >= start)
                end = Math.min(end, liveCues[i].start)
        }
        if (end - start < 1 / Math.max(1, EditorState.subtitles.projectFps()))
            return
        insertCue(start, end)
    }

    function deleteSelected() {
        const sel = selectedIndices()
        if (sel.length === 0)
            return
        const list = copyCues(liveCues).filter((c, i) => !selectedMap[i])
        EditorState.subtitles.clearSelection()
        EditorState.setSubtitleCues(track, clip, list)
    }

    function nudgeSelected(seconds) {
        if (selectedCount > 0)
            EditorState.subtitles.applyShift(track, clip, 1, seconds, 0)
    }

    function zoomBy(factor) {
        timeViewObject.zoomAt(laneWidth / 2, factor)
    }

    function ensurePlayheadVisible() {
        const x = timeViewObject.xFor(EditorState.playheadSeconds)
        if (x < 0 || x > laneWidth * 0.92)
            timeViewObject.contentX = EditorState.playheadSeconds * timeViewObject.pxPerSecond - laneWidth * 0.08
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
            EditorState.subtitles.selectAll(track, clip)
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

    onCommittedCuesChanged: {
        if (!gestureActive)
            liveCues = committedCues
    }
    onTrackChanged: Qt.callLater(onClipSwitched)
    onClipChanged: Qt.callLater(onClipSwitched)
    onShownChanged: {
        if (shown)
            Qt.callLater(onClipSwitched)
    }
    onClipDataChanged: updateClipSpan()
    Component.onCompleted: onClipSwitched()

    Keys.onPressed: (event) => {
        if (!root.isSubtitle)
            return
        const mods = event.modifiers
        if (event.key === Qt.Key_Delete || event.key === Qt.Key_Backspace) {
            root.deleteSelected()
            event.accepted = true
        } else if (event.key === Qt.Key_A && (mods & Qt.ControlModifier)) {
            EditorState.subtitles.selectAll(root.track, root.clip)
            event.accepted = true
        } else if (event.key === Qt.Key_Escape) {
            if (!view.surface.busy)
                EditorState.subtitles.clearSelection()
            else
                view.surface.cancelGesture()
            event.accepted = true
        } else if ((event.key === Qt.Key_Left || event.key === Qt.Key_Right) && (mods & Qt.AltModifier)) {
            const step = (mods & Qt.ShiftModifier) ? 0.1
                                                   : 1 / Math.max(1, EditorState.subtitles.projectFps())
            root.nudgeSelected((event.key === Qt.Key_Left ? -1 : 1) * step)
            event.accepted = true
        }
    }

    TapHandler {
        onPressedChanged: if (pressed) root.forceActiveFocus()
    }

    Connections {
        target: EditorState.subtitles

        function onCuesChanged() { root.cuesRevision++ }
    }

    Connections {
        target: EditorState

        function onPlayheadSecondsChanged() {
            if (root.shown && root.follow && EditorState.playing && root.isSubtitle)
                root.ensurePlayheadVisible()
        }
    }

    KeyframeTimeView {
        id: timeViewObject
        viewportWidth: view.width
    }

    SubtitleLaneToolbar {
        id: toolbar
        visible: root.isSubtitle
        width: parent.width
        follow: root.follow
        canDelete: root.selectedCount > 0
        cueCount: root.liveCues.length
        onZoomOut: root.zoomBy(0.8)
        onZoomIn: root.zoomBy(1.25)
        onFitAll: timeViewObject.fitClip()
        onFollowToggled: root.follow = !root.follow
        onAddCue: root.addCueAtPlayhead()
        onDeleteSelected: root.deleteSelected()
    }

    SubtitleLaneView {
        id: view
        visible: root.isSubtitle
        anchors.top: toolbar.bottom
        anchors.bottom: parent.bottom
        width: parent.width
        panel: root
    }

    EmptyState {
        visible: !root.isSubtitle
        anchors.centerIn: parent
        width: Math.min(parent.width - Theme.spacing3xl, 280)
        compact: true
        title: qsTr("Select a subtitle clip")
        hint: qsTr("Pick a subtitle clip on the timeline to edit its cues here.")
    }
}
