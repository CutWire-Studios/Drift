pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import Drift
import "../keyframes/KeyframesModel.js" as Model

PanelFrame {
    id: root

    property bool shown: false
    property real treeWidth: 300
    property string mode: "lanes"
    property bool normalize: false
    property bool animatedOnly: false
    property var collapsed: ({})
    property var curveShown: ({})
    property string focusedProp: ""
    property var rows: []
    readonly property alias rowKeys: rowKeysModel

    readonly property alias timeView: timeViewObject
    readonly property alias selection: selectionObject
    readonly property alias treeContentY: treeList.contentY
    readonly property alias rightArea: rightAreaItem
    readonly property alias treeList: treeList

    readonly property int track: EditorState.selectedTrack
    readonly property int clip: EditorState.selectedClip
    readonly property bool hasClip: track >= 0 && clip >= 0
                                    && Object.keys(EditorState.selectedClipData).length > 0
    readonly property real playhead: EditorState.playheadSeconds
    readonly property bool wantsActions: shown && (activeFocus || hover.hovered)

    readonly property var focusItem: Window.activeFocusItem
    property string lastRowKeys: ""
    property var entryCache: ({ "key": "", "list": [] })
    property var curveSeen: ({})

    readonly property var targets: {
        void selectionObject.revision
        void rows
        void focusedProp
        const props = selectionObject.props()
        const out = []
        for (let i = 0; i < rows.length; ++i) {
            const r = rows[i]
            if (r.type === "group")
                continue
            if (props.length > 0 ? props.some(p => r.tracks.indexOf(p) >= 0) : r.prop === focusedProp)
                out.push(r)
        }
        return out
    }
    readonly property real halfFrame: {
        void EditorState.tracksRevision
        return 0.5 / Math.max(1, EditorState.projectFile.projectFps())
    }
    readonly property bool allTargetsKeyed: {
        void playhead
        return targets.length > 0 && targets.every(r => Model.refsForRowAt(r, playhead, halfFrame).length > 0)
    }
    readonly property bool targetsHaveKeys: targets.some(r => r.keys.length > 0)

    function targetTracks(list) {
        const out = []
        for (let i = 0; i < list.length; ++i) {
            for (let j = 0; j < list[i].tracks.length; ++j) {
                if (out.indexOf(list[i].tracks[j]) < 0)
                    out.push(list[i].tracks[j])
            }
        }
        return out
    }

    function visibleDataRows() {
        return rows.filter(r => r.type !== "group")
    }

    function entries() {
        const key = track + ":" + clip + ":" + EditorState.tracksRevision
        if (entryCache.key !== key)
            entryCache = { "key": key, "list": EditorState.keyframes.animatableProperties(track, clip) }
        return entryCache.list
    }

    function snapKeyTime(raw, excludeRefs, invert) {
        const skip = {}
        for (let i = 0; i < excludeRefs.length; ++i)
            skip[Math.round(excludeRefs[i].seconds * 1e6)] = true
        const others = Model.allKeyTimes(rows).filter(t => !skip[Math.round(t * 1e6)])
        return EditorState.keyframes.snapKeyTime(raw, timeViewObject.pxPerSecond, others,
                                                 EditorState.snapEnabled !== !!invert)
    }

    function rowKey(r) {
        return r.type === "group" ? "g:" + r.group : "p:" + r.group + ":" + r.prop
    }

    function applyRows(built) {
        const keys = built.map(rowKey)
        const joined = keys.join("\n")
        if (joined === lastRowKeys && rowKeysModel.count === keys.length) {
            rows = built
            return
        }
        lastRowKeys = joined
        const wanted = {}
        for (let i = 0; i < keys.length; ++i)
            wanted[keys[i]] = true
        for (let i = rowKeysModel.count - 1; i >= 0; --i) {
            if (!wanted[rowKeysModel.get(i).key])
                rowKeysModel.remove(i)
        }
        let at = 0
        let ordered = true
        for (let i = 0; i < rowKeysModel.count && ordered; ++i) {
            while (at < keys.length && keys[at] !== rowKeysModel.get(i).key)
                ++at
            if (at++ >= keys.length)
                ordered = false
        }
        if (!ordered)
            rowKeysModel.clear()
        rows = built
        for (let i = 0; i < keys.length; ++i) {
            if (i >= rowKeysModel.count || rowKeysModel.get(i).key !== keys[i])
                rowKeysModel.insert(i, { "key": keys[i] })
        }
    }

    function rebuild() {
        if (!shown || !hasClip) {
            applyRows([])
            return
        }
        const built = Model.buildRows(entries(), {
            "sizeLinked": EditorState.sizeLinked,
            "animatedOnly": animatedOnly,
            "collapsed": collapsed,
            "clipStart": timeViewObject.clipStart,
            "scaleLabel": qsTr("Scale"),
            "keysFor": prop => EditorState.clipKeyframes(track, clip, prop)
        })
        const keepY = treeList.contentY
        applyRows(built)
        if (keepY > 0)
            Qt.callLater(() => { treeList.contentY = keepY })
        let shownNow = curveShown
        let changed = false
        for (let i = 0; i < built.length; ++i) {
            const r = built[i]
            if (r.type === "group" || !r.animated || curveSeen[r.keyProp])
                continue
            curveSeen[r.keyProp] = true
            if (!changed) {
                shownNow = Object.assign({}, curveShown)
                changed = true
            }
            shownNow[r.keyProp] = true
        }
        if (changed)
            curveShown = shownNow
    }

    function scheduleRebuild() {
        Qt.callLater(rebuild)
    }

    function updateClipSpan() {
        const data = EditorState.selectedClipData
        timeViewObject.clipStart = data.start || 0
        timeViewObject.clipDuration = Math.max(0.1, data.duration || 1)
    }

    function onClipSwitched() {
        selectionObject.clear()
        focusedProp = ""
        curveSeen = ({})
        curveShown = ({})
        updateClipSpan()
        timeViewObject.fitClip()
        rebuild()
    }

    function toggleCollapsed(group) {
        const next = Object.assign({}, collapsed)
        next[group] = !next[group]
        collapsed = next
    }

    function setCurveShown(prop, on) {
        const next = Object.assign({}, curveShown)
        next[prop] = on
        curveShown = next
    }

    function jumpToKey(forward) {
        const list = targets.length > 0 ? targets : visibleDataRows()
        const t = forward ? Model.nextKeyTime(list, playhead) : Model.prevKeyTime(list, playhead)
        if (t !== null)
            EditorState.playheadSeconds = t
    }

    function toggleKeyAtPlayhead() {
        if (targets.length === 0)
            return
        const tracks = targetTracks(targets)
        if (allTargetsKeyed)
            EditorState.keyframes.removeKeysAt(track, clip, tracks, playhead)
        else
            EditorState.keyframes.addKeysAt(track, clip, tracks, playhead)
    }

    function clearTargetKeys() {
        if (targets.length === 0)
            return
        EditorState.keyframes.clearProperties(track, clip, targetTracks(targets))
        selectionObject.clear()
    }

    function fitAll() {
        timeViewObject.fitClip()
        if (mode === "curves")
            curvesView.fitValues(false)
    }

    function fitSelection() {
        const span = selectionObject.span()
        if (span)
            timeViewObject.fitRange(span.start, span.end)
        if (mode === "curves")
            curvesView.fitValues(true)
    }

    function allVisibleKeyRefs() {
        const refs = []
        const list = visibleDataRows()
        for (let i = 0; i < list.length; ++i) {
            for (let j = 0; j < list[i].tracks.length; ++j) {
                const keys = list[i].trackKeys[list[i].tracks[j]] || []
                for (let k = 0; k < keys.length; ++k)
                    refs.push({ "prop": list[i].tracks[j], "seconds": keys[k].seconds })
            }
        }
        return refs
    }

    function nudgeSelection(frames) {
        const dt = frames / Math.max(1, EditorState.projectFile.projectFps())
        const moved = EditorState.keyframes.moveKeys(track, clip, selectionObject.refs(), dt)
        if (moved && moved.length > 0)
            selectionObject.replace(moved)
    }

    function cancelGesture() {
        if (!surface.busy && !curvesView.busy && !stretchBox.dragging)
            return false
        surface.cancelDrag()
        curvesView.cancelDrag()
        stretchBox.cancel()
        return true
    }

    function textFieldFocused() {
        const f = focusItem
        return !!f && f.cursorPosition !== undefined
    }

    function handleAction(id) {
        if (!shown || !hasClip || textFieldFocused())
            return false
        const mine = activeFocus || hover.hovered
        if (id === "paste") {
            if (selectionObject.count === 0 && !(EditorState.keyframes.clipboardCount > 0 && mine))
                return false
            const pasted = EditorState.keyframes.pasteKeys(track, clip, playhead)
            if (pasted && pasted.length > 0)
                selectionObject.replace(pasted)
            return true
        }
        if (id === "selectAll") {
            if (selectionObject.count === 0 && !mine)
                return false
            selectionObject.replace(allVisibleKeyRefs())
            return true
        }
        if (selectionObject.count === 0)
            return false
        if (id === "delete") {
            EditorState.keyframes.removeKeys(track, clip, selectionObject.refs())
            selectionObject.clear()
            return true
        }
        if (id === "copy") {
            EditorState.keyframes.copyKeys(track, clip, selectionObject.refs())
            return true
        }
        return false
    }

    focus: true
    activeFocusOnTab: true

    onWantsActionsChanged: {
        if (wantsActions)
            ShellBridge.keyframesActionHandler = root
        else if (ShellBridge.keyframesActionHandler === root)
            ShellBridge.keyframesActionHandler = null
    }
    Component.onDestruction: {
        if (ShellBridge.keyframesActionHandler === root)
            ShellBridge.keyframesActionHandler = null
    }

    onShownChanged: scheduleRebuild()
    onTrackChanged: Qt.callLater(onClipSwitched)
    onClipChanged: Qt.callLater(onClipSwitched)
    onAnimatedOnlyChanged: scheduleRebuild()
    onCollapsedChanged: scheduleRebuild()
    Component.onCompleted: onClipSwitched()

    Keys.onPressed: (event) => {
        if (event.key === Qt.Key_K && event.modifiers === Qt.NoModifier) {
            root.toggleKeyAtPlayhead()
            event.accepted = true
        } else if (event.key === Qt.Key_Escape && root.cancelGesture()) {
            event.accepted = true
        } else if ((event.key === Qt.Key_Left || event.key === Qt.Key_Right)
                   && !(event.modifiers & (Qt.AltModifier | Qt.ControlModifier | Qt.MetaModifier))
                   && selectionObject.count > 0 && root.hasClip) {
            root.nudgeSelection((event.key === Qt.Key_Left ? -1 : 1) * (event.modifiers & Qt.ShiftModifier ? 10 : 1))
            event.accepted = true
        } else if (event.key === Qt.Key_Left && (event.modifiers & Qt.AltModifier)) {
            root.jumpToKey(false)
            event.accepted = true
        } else if (event.key === Qt.Key_Right && (event.modifiers & Qt.AltModifier)) {
            root.jumpToKey(true)
            event.accepted = true
        }
    }

    ListModel { id: rowKeysModel }

    HoverHandler { id: hover }

    TapHandler {
        onPressedChanged: if (pressed) root.forceActiveFocus()
    }

    Connections {
        target: EditorState
        function onTracksRevisionChanged() { root.scheduleRebuild() }
        function onSizeLinkedChanged() { root.scheduleRebuild() }
        function onClipPropertiesPreviewed(t, c, keys) {
            if (t === root.track && c === root.clip)
                root.scheduleRebuild()
        }
        function onSelectedClipDataChanged() {
            root.updateClipSpan()
            root.scheduleRebuild()
        }
    }

    KeyframesTimeView {
        id: timeViewObject
        viewportWidth: rightAreaItem.width
    }

    KeyframesSelection {
        id: selectionObject
    }

    KeyframesToolbar {
        id: toolbar
        visible: root.hasClip
        width: parent.width
        mode: root.mode
        normalize: root.normalize
        animatedOnly: root.animatedOnly
        hasTargets: root.targets.length > 0
        allTargetsKeyed: root.allTargetsKeyed
        canClear: root.targetsHaveKeys
        canFitSelection: selectionObject.count > 0
        onPrevKey: root.jumpToKey(false)
        onNextKey: root.jumpToKey(true)
        onToggleKey: root.toggleKeyAtPlayhead()
        onClearKeys: root.clearTargetKeys()
        onModeRequested: (m) => root.mode = m
        onNormalizeToggled: root.normalize = !root.normalize
        onFitAll: root.fitAll()
        onFitSelection: root.fitSelection()
        onAnimatedOnlyToggled: root.animatedOnly = !root.animatedOnly
    }

    Item {
        id: body
        visible: root.hasClip && root.rows.length > 0
        anchors.top: toolbar.bottom
        anchors.bottom: parent.bottom
        width: parent.width

        Rectangle {
            width: root.treeWidth
            height: ruler.height
            color: Theme.c.keyframesRuler.background

            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: Theme.c.keyframesRuler.borderWidth
                color: Theme.c.keyframesRuler.border
            }
        }

        ListView {
            id: treeList
            y: ruler.height
            width: root.treeWidth
            height: parent.height - y
            clip: true
            model: root.rowKeys
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: AppScrollBar { }

            delegate: KeyframesTreeRow {
                id: delegate
                required property int index
                width: treeList.width
                // Empty while the row outlives a shrinking list, until the ListView removes it.
                row: root.rows[index] ?? ({})
                track: root.track
                clip: root.clip
                playhead: root.playhead
                displayPlayhead: EditorState.inspectorPlayheadSeconds
                curvesMode: root.mode === "curves"
                curveShown: !!root.curveShown[delegate.row.keyProp]
                focused: delegate.row.type !== "group" && root.focusedProp === delegate.row.prop
                onFocusRequested: {
                    root.focusedProp = delegate.row.prop
                    root.forceActiveFocus()
                }
                onGroupToggled: root.toggleCollapsed(delegate.row.group)
                onCurveShownToggled: (on) => root.setCurveShown(delegate.row.keyProp, on)
            }
        }

        Rectangle {
            id: splitter
            x: root.treeWidth
            width: Theme.spacingXs
            height: parent.height
            color: splitArea.containsMouse || splitArea.pressed ? Theme.c.keyframesPanel.splitterHover
                                                                 : Theme.c.keyframesPanel.splitter

            MouseArea {
                id: splitArea
                anchors.fill: parent
                anchors.leftMargin: -Theme.spacingXs
                anchors.rightMargin: -Theme.spacingXs
                hoverEnabled: true
                cursorShape: Qt.SplitHCursor
                preventStealing: true
                property real pressX: 0
                property real pressWidth: 0
                onPressed: (mouse) => {
                    pressX = mapToItem(body, mouse.x, 0).x
                    pressWidth = root.treeWidth
                }
                onPositionChanged: (mouse) => {
                    if (!pressed)
                        return
                    const dx = mapToItem(body, mouse.x, 0).x - pressX
                    root.treeWidth = Math.max(180, Math.min(600, pressWidth + dx))
                }
            }
        }

        Item {
            id: rightAreaItem
            x: root.treeWidth + splitter.width
            width: parent.width - x
            height: parent.height
            clip: true

            KeyframesRuler {
                id: ruler
                width: parent.width
                timeView: timeViewObject
            }

            Item {
                id: lanesArea
                y: ruler.height
                width: parent.width
                height: parent.height - y
                clip: true

                KeyframesLanesView {
                    id: lanesView
                    anchors.fill: parent
                    visible: root.mode === "lanes"
                    panel: root
                }

                KeyframesCurvesView {
                    id: curvesView
                    anchors.fill: parent
                    visible: root.mode === "curves"
                    panel: root
                    onContextRequested: (x, y, hit) => contextMenu.openAt(x, y, hit)
                }

                KeyframesInteractionSurface {
                    id: surface
                    anchors.fill: parent
                    anchors.bottomMargin: lanesView.scrollBar.height
                    visible: root.mode === "lanes"
                    panel: root
                    lanes: lanesView
                    onContextRequested: (x, y, hit) => contextMenu.openAt(x, y, hit)
                }

                KeyframesStretchBox {
                    id: stretchBox
                    anchors.fill: parent
                    visible: active && root.mode === "lanes"
                    panel: root
                }

                KeyframesContextMenu {
                    id: contextMenu
                    panel: root
                    anchorItem: surface.anchorProxy
                }
            }

            KeyframesPlayheadLine {
                y: ruler.height * 0.55
                height: parent.height - y
                timeView: timeViewObject
            }
        }
    }

    EmptyState {
        visible: !root.hasClip
        anchors.centerIn: parent
        width: Math.min(parent.width - Theme.spacing3xl, 260)
        compact: true
        title: qsTr("Select a clip to edit its keyframes")
    }

    EmptyState {
        visible: root.hasClip && root.rows.length === 0
        anchors.centerIn: parent
        width: Math.min(parent.width - Theme.spacing3xl, 260)
        compact: true
        title: root.animatedOnly ? qsTr("No animated properties on this clip")
                                 : qsTr("This clip has no animatable properties")
    }
}
