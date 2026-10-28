pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import Drift

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
    property int hoverRow: -1
    readonly property alias rowsModel: rowsModelObject
    readonly property var rows: rowsModelObject.rows

    readonly property alias timeView: timeViewObject
    readonly property alias selection: selectionObject
    readonly property real treeColumn: Math.min(treeWidth, width * 0.45)
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
    property var curveSeen: ({})

    readonly property var targets: {
        void selectionObject.revision
        void rows
        void focusedProp
        return rowsModelObject.targetIndices(selectionObject.props(), focusedProp)
    }
    readonly property real halfFrame: {
        void EditorState.tracksRevision
        return 0.5 / Math.max(1, EditorState.projectFile.projectFps())
    }
    readonly property bool noKeys: {
        void rows
        return rowsModelObject.count > 0 && rowsModelObject.allVisibleKeyRefs().length === 0
    }
    readonly property bool allTargetsKeyed: {
        void playhead
        void rows
        return rowsModelObject.allTargetsKeyed(targets, playhead, halfFrame)
    }
    readonly property bool targetsHaveKeys: {
        void rows
        return rowsModelObject.targetsHaveKeys(targets)
    }

    function formatTimecode(seconds) {
        const fps = Math.max(1, EditorState.projectFile.projectFps())
        const total = Math.round(Math.max(0, seconds) * fps)
        const pad = (n) => (n < 10 ? "0" : "") + n
        const whole = Math.floor(total / fps)
        return pad(Math.floor(whole / 3600)) + ":" + pad(Math.floor(whole / 60) % 60) + ":" + pad(whole % 60)
               + ":" + pad(total - whole * fps)
    }

    function snapKeyTime(raw, excludeRefs, invert) {
        return rowsModelObject.snapKeyTime(raw, excludeRefs, !!invert)
    }

    function rebuild() {
        if (!shown || !hasClip) {
            rowsModelObject.clear()
            return
        }
        const keepY = treeList.contentY
        rowsModelObject.rebuild()
        const built = rows
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
        const t = forward ? rowsModelObject.nextKeyTime(targets, playhead)
                          : rowsModelObject.prevKeyTime(targets, playhead)
        if (t >= 0)
            EditorState.playheadSeconds = t
    }

    function toggleKeyAtPlayhead() {
        if (targets.length === 0)
            return
        const tracks = rowsModelObject.targetTracks(targets)
        if (allTargetsKeyed)
            EditorState.keyframes.removeKeysAt(track, clip, tracks, playhead)
        else
            EditorState.keyframes.addKeysAt(track, clip, tracks, playhead)
    }

    function clearTargetKeys() {
        if (targets.length === 0)
            return
        EditorState.keyframes.clearProperties(track, clip, rowsModelObject.targetTracks(targets))
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

    function nudgeSelection(frames) {
        const dt = frames / Math.max(1, EditorState.projectFile.projectFps())
        const moved = EditorState.keyframes.nudgeKeys(track, clip, selectionObject.refs(), dt)
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

    function handleAction(id, force) {
        if (!shown || !hasClip || textFieldFocused())
            return false
        const mine = !!force || activeFocus || hover.hovered
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
            selectionObject.replace(rowsModelObject.allVisibleKeyRefs())
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
        } else if (event.key === Qt.Key_F && event.modifiers === Qt.NoModifier && root.hasClip) {
            if (selectionObject.count > 0)
                root.fitSelection()
            else
                root.fitAll()
            event.accepted = true
        } else if (event.key === Qt.Key_Home && event.modifiers === Qt.NoModifier && root.hasClip) {
            root.fitAll()
            event.accepted = true
        } else if (event.key === Qt.Key_Left && (event.modifiers & Qt.AltModifier)) {
            root.jumpToKey(false)
            event.accepted = true
        } else if (event.key === Qt.Key_Right && (event.modifiers & Qt.AltModifier)) {
            root.jumpToKey(true)
            event.accepted = true
        }
    }

    KeyframeRowsModel {
        id: rowsModelObject
        controller: EditorState.keyframes
        timeView: timeViewObject
        track: root.track
        clip: root.clip
        revision: EditorState.tracksRevision
        sizeLinked: EditorState.sizeLinked
        animatedOnly: root.animatedOnly
        scaleLabel: qsTr("Scale")
        collapsed: root.collapsed
    }

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

    KeyframeTimeView {
        id: timeViewObject
        viewportWidth: rightAreaItem.width
    }

    KeyframeSelection {
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
        visible: root.hasClip && rowsModelObject.count > 0
        anchors.top: toolbar.bottom
        anchors.bottom: parent.bottom
        width: parent.width

        Rectangle {
            width: root.treeColumn
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
            width: root.treeColumn
            height: parent.height - y
            clip: true
            model: rowsModelObject
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: AppScrollBar { }

            delegate: KeyframesTreeRow {
                id: delegate
                required property int index
                required property var rowData
                width: treeList.width
                // Empty while the row outlives a shrinking list, until the ListView removes it.
                row: delegate.rowData ?? ({})
                rowsModel: rowsModelObject
                rowIndex: delegate.index
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
                onHoveredChanged: {
                    if (hovered)
                        root.hoverRow = delegate.index
                    else if (root.hoverRow === delegate.index)
                        root.hoverRow = -1
                }
                onKeysSelectRequested: (additive) => {
                    const refs = rowsModelObject.rowRefs(delegate.index)
                    if (additive)
                        selectionObject.add(refs)
                    else
                        selectionObject.replace(refs)
                }
                onGroupToggled: root.toggleCollapsed(delegate.row.group)
                onCurveShownToggled: (on) => root.setCurveShown(delegate.row.keyProp, on)
            }
        }

        Rectangle {
            id: splitter
            x: root.treeColumn
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
                    pressWidth = root.treeColumn
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
            x: root.treeColumn + splitter.width
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

                Text {
                    visible: root.noKeys
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: Theme.spacingXl * 2
                    width: parent.width - Theme.spacing3xl
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    text: qsTr("Press K to add a keyframe at the playhead, or double-click a lane to add one there")
                    color: Theme.c.keyframesTreeRow.foregroundMuted
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeXs
                }

                MouseArea {
                    z: 5
                    width: parent.width
                    height: Theme.spacingMd
                    cursorShape: Qt.SizeHorCursor
                    preventStealing: true
                    onPressed: (mouse) => ruler.scrubTo(mouse.x)
                    onPositionChanged: (mouse) => {
                        if (pressed)
                            ruler.scrubTo(mouse.x)
                    }
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
        visible: root.hasClip && rowsModelObject.count === 0
        anchors.centerIn: parent
        width: Math.min(parent.width - Theme.spacing3xl, 260)
        compact: true
        title: root.animatedOnly ? qsTr("No animated properties on this clip")
                                 : qsTr("This clip has no animatable properties")
    }
}
