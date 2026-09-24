import QtQuick
import Drift

// One track's clips on the scene-graph renderer: TimelineTrackClips draws every clip in the
// viewport and owns the body gestures, and a TimelineClipItem overlay exists only for the clips
// that need trim handles, fade dots or the context menu right now. The panel's policy for where a
// moved clip lands stays here, in QML, shared by the desktop and Android panels.
Item {
    id: area

    // Owning panel (TimelinePanel or AndroidTimeline) and its track column, as TimelineClipItem
    // takes them.
    property var panel
    property var timelineColumn
    property int trackIndex: -1
    property var viewState
    property bool touchMode: false
    readonly property bool dragging: renderer.dragging

    readonly property var trackData: panel && panel.tracks && trackIndex >= 0
                                     && trackIndex < panel.tracks.length
                                     ? panel.tracks[trackIndex] : ({})

    property real lastPreviewDesired: -1
    property int lastPreviewTrack: -1

    TimelineTrackClips {
        id: renderer
        anchors.fill: parent
        viewState: area.viewState
        clipsModel: EditorState.clipsModel(area.trackIndex)
        trackIndex: area.trackIndex
        trackType: area.trackData.type || ""
        clipDisplay: area.trackData.clipDisplay !== undefined ? area.trackData.clipDisplay : 1
        showChannelWaveforms: area.trackData.showChannelWaveforms === true
        editor: EditorState
        assets: AssetLibrary
        haptics: Haptics

        onMoveStarted: (clipIndex) => {
            area.lastPreviewDesired = -1
            area.lastPreviewTrack = -1
            area.panel.beginMoveFollow(area.trackIndex, clipIndex)
        }
        onMoveUpdated: (clipIndex, rect, dx, dy) => {
            area.panel.updateMoveFollow(dx, dy)
            const info = renderer.clipInfo(clipIndex)
            const desired = Math.max(0, (rect.x - Theme.clipSelectionRingWidth) / area.panel.pxPerSecond)
            const pos = renderer.mapToItem(area.timelineColumn, rect.x + rect.width / 2,
                                             rect.y + rect.height / 2)
            const targetTrack = area.panel.trackIndexAtY(pos.y)
            const effTrack = targetTrack >= 0 ? targetTrack : area.trackIndex
            if (Math.abs(desired - area.lastPreviewDesired) * area.panel.pxPerSecond >= 1
                    || effTrack !== area.lastPreviewTrack) {
                area.lastPreviewDesired = desired
                area.lastPreviewTrack = effTrack
                area.panel.showLandingPreview(effTrack, desired, info.duration, info.id)
            }
        }
        onMoveCanceled: {
            area.panel.clearMoveFollow()
            area.panel.clearLandingPreview()
            area.lastPreviewDesired = -1
            area.lastPreviewTrack = -1
        }
        onMoveFinished: (clipIndex, rect) => area.finishMove(clipIndex, rect)
        onContextMenuRequested: (clipIndex) => area.openContextMenu(clipIndex)
        onToggleSelectionRequested: (clipIndex) => area.panel.toggleInSelection(area.trackIndex, clipIndex)
        onLiftedChanged: {
            if (typeof area.panel.setScrollLocked === "function")
                area.panel.setScrollLocked(renderer.lifted)
        }
    }

    // Same decision tree the per-clip delegate ran on release, with the indices captured at press.
    function finishMove(originClip, rect) {
        const originTrack = area.trackIndex
        panel.clearMoveFollow()
        panel.clearLandingPreview()
        lastPreviewDesired = -1
        lastPreviewTrack = -1

        const info = renderer.clipInfo(originClip)
        // Snapped here, not only in the preview, so the clip lands on the outline it was showing.
        const rawStart = Math.max(0, (rect.x - Theme.clipSelectionRingWidth) / panel.pxPerSecond)
        const newStart = panel.snapClipStart(rawStart, info.duration, info.id).start
        const pos = renderer.mapToItem(timelineColumn, rect.x + rect.width / 2,
                                         rect.y + rect.height / 2)
        const target = typeof panel.dropTargetAtY === "function"
                     ? panel.dropTargetAtY(pos.y)
                     : { "track": panel.trackIndexAtY(pos.y), "lane": -1 }
        const isAdjustment = info.kind === "adjustment"
        const wasInLane = panel.tracks[originTrack].isAdjustmentLane === true
        if (isAdjustment && target.lane >= 0) {
            if (target.lane !== originTrack)
                EditorState.moveClipToTrack(originTrack, originClip, target.lane, newStart)
            else
                EditorState.moveClip(originTrack, originClip, newStart)
        } else if (isAdjustment && target.track >= 0
                   && panel.tracks[target.track].type !== "adjustment") {
            EditorState.moveAdjustmentToLane(originTrack, originClip, target.track, newStart)
        } else if (isAdjustment && wasInLane && target.track < 0) {
            EditorState.moveAdjustmentToOwnTrack(originTrack, originClip, newStart)
        } else if (target.track >= 0 && target.track !== originTrack) {
            EditorState.moveClipToTrack(originTrack, originClip, target.track, newStart)
        } else {
            EditorState.moveClip(originTrack, originClip, newStart)
        }
    }

    // The clip was selected by the press that asked for this, which already gave it an overlay.
    function openContextMenu(clipIndex) {
        for (let i = 0; i < overlays.count; i++) {
            const item = overlays.itemAt(i)
            if (item && item.clipIndex === clipIndex) {
                item.openContextMenu()
                return
            }
        }
    }

    Repeater {
        id: overlays
        model: renderer.activeClips
        delegate: TimelineClipItem {
            overlayOnly: true
            trackClips: renderer
            panel: area.panel
            timelineColumn: area.timelineColumn
            trackIndex: area.trackIndex
            clipIndex: model.sourceIndex
            touchMode: area.touchMode
        }
    }

    // Pills and the VFR badge are drawn by the renderer; their tooltips live here, one per track.
    Loader {
        active: renderer.hoverToolTip.length > 0
        x: renderer.hoverToolTipRect.x
        y: renderer.hoverToolTipRect.y
        width: renderer.hoverToolTipRect.width
        height: renderer.hoverToolTipRect.height
        sourceComponent: ThemedToolTip {
            visible: true
            text: renderer.hoverToolTip
        }
    }

    // Touch: scroll the timeline while a picked-up clip is held near an edge.
    Timer {
        interval: 16
        repeat: true
        running: renderer.touchDragging && typeof area.panel.dragEdgeScroll === "function"

        function step(depth) { return Math.min(24, 6 + depth * 0.4) }

        onTriggered: {
            const panel = area.panel
            const px = renderer.dragPointer.x - panel.timelineViewX
            const py = renderer.mapToItem(area.timelineColumn, 0, renderer.dragPointer.y).y
                       + panel.seekHeaderHeight - panel.timelineViewY
            const edgeX = Math.min(48, panel.timelineViewW * 0.25)
            const usableTop = panel.seekHeaderHeight
            const usableH = Math.max(0, panel.timelineViewH - usableTop)
            const edgeY = Math.min(48, usableH * 0.25)
            let dx = 0
            let dy = 0
            if (px < edgeX)
                dx = -step(edgeX - px)
            else if (px > panel.timelineViewW - edgeX)
                dx = step(px - (panel.timelineViewW - edgeX))
            if (edgeY > 0) {
                if (py < usableTop + edgeY)
                    dy = -step(usableTop + edgeY - py)
                else if (py > panel.timelineViewH - edgeY)
                    dy = step(py - (panel.timelineViewH - edgeY))
            }
            if (dx === 0 && dy === 0)
                return
            panel.dragEdgeScroll(dx, dy)
            renderer.refreshDrag()
        }
    }
}
