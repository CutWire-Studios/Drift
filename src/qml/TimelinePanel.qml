import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Window
import Drift
import "components"
import "components/timeline"

PanelFrame {
    id: root

    property real zoom: 1.0
    property string propertiesTab: ""

    // Timeline tool mode (CapCut-style exclusive modes). One of:
    //   ""          Select — normal editing (toolbar pointer / V)
    //   "split"     Blade — click a clip to split it (toolbar scissors / B)
    //   "trimStart"  click a clip to drop everything left of the cut line
    //   "trimEnd"    click a clip to drop everything right of the cut line
    // While a cut tool is active, hovering the timeline shows a red dashed
    // virtual playhead; trim tools also tint the doomed side of the clip.
    property string timelineTool: ""
    // Snapped position (seconds) of the virtual cut playhead; < 0 when not hovering.
    property real cutHoverSeconds: -1
    // Clip physically under the pointer while a tool is active (-1 when none).
    property int cutHoverTrack: -1
    property int cutHoverClip: -1
    onTimelineToolChanged: if (timelineTool === "") {
        cutHoverSeconds = -1
        cutHoverTrack = -1
        cutHoverClip = -1
    }

    // CapCut: V = Select, B = Blade. These are registered actions ("selectTool" /
    // "bladeTool") so they appear in the shortcut list and can be rebound; the tool
    // state lives here rather than in the backend, so Main.qml dispatches them
    // alongside Escape, which exits the tool without clearing the selection.

    // Y offset of a track row within the track column (excludes ruler/bookmark).
    function trackOffsetY(index) {
        var cursor = 0
        for (var i = 0; i < index && i < tracks.length; i++)
            cursor += trackHeight(i) + Theme.trackGap
        return cursor
    }
    // Trailing empty runway after the last clip — constant pixel length at any
    // zoom (viewport-relative), not a fixed number of seconds.
    readonly property real timelineEndPadPx: Math.max(
        Theme.timelineEndPadMinPx, flick.width * Theme.timelineEndPadFraction)

    // Fixed floor for wheel/slider zoom-out. Not tied to project duration —
    // use fitZoom() when you want the timeline fitted to the viewport.
    // Low enough for multi-hour projects without live recalibration.
    readonly property real minZoom: 0.0001
    readonly property real maxZoom: 40.0
    readonly property real pxPerSecond: Theme.pixelsPerSecondBase * zoom
    // Exposed for clip filmstrip viewport culling (Flickable ids are local).
    readonly property real timelineViewX: flick.contentX
    readonly property real timelineViewW: flick.width

    // Fit the whole project (plus end pad) into the timeline viewport.
    function fitZoom() {
        if (!(EditorState.durationSeconds > 0)) {
            zoom = 1.0
            flick.contentX = 0
            return
        }
        const viewportW = Math.max(flick.width, 1)
        const usable = Math.max(viewportW - timelineEndPadPx, 1)
        const fit = usable / (EditorState.durationSeconds * Theme.pixelsPerSecondBase)
        zoom = Math.max(minZoom, Math.min(maxZoom, fit))
        flick.contentX = 0
    }

    // Keep `anchorSeconds` glued to the same viewport X after the scale change.
    // Without this, zooming expands from the content origin and the playhead
    // (or the point under the cursor) drifts off to the right.
    function setZoomAround(newZoom, anchorSeconds, viewportX) {
        const z = Math.max(minZoom, Math.min(maxZoom, newZoom))
        if (z === zoom)
            return
        contentXAnimation.stop()
        zoom = z
        const newMaxX = Math.max(0, flick.contentWidth - flick.width)
        flick.contentX = Math.max(0, Math.min(newMaxX,
                                              anchorSeconds * pxPerSecond - viewportX))
    }

    // Toolbar / slider zoom: hold the playhead steady in the viewport.
    function setZoom(newZoom) {
        const anchorSeconds = Math.max(0, EditorState.playheadSeconds)
        const viewportX = anchorSeconds * pxPerSecond - flick.contentX
        setZoomAround(newZoom, anchorSeconds, viewportX)
    }

    // Ruler tick interval (seconds): the smallest "nice" step whose labels still
    // have room to breathe at the current zoom, so timestamps never squash.
    readonly property real tickStepSeconds: {
        const minLabelPx = 66
        const needed = minLabelPx / pxPerSecond
        const steps = [0.01, 0.02, 0.05, 0.1, 0.2, 0.5, 1, 2, 5, 10, 15, 30,
                       60, 120, 300, 600, 900, 1800, 3600,
                       7200, 10800, 14400, 21600, 43200]
        for (var i = 0; i < steps.length; i++)
            if (steps[i] >= needed)
                return steps[i]
        return steps[steps.length - 1]
    }

    // HH:MM:SS, plus .CC hundredths once zoomed into sub-second ticks.
    function formatTick(seconds) {
        const cc = Math.round(Math.max(0, seconds) * 100)
        const pad = (n) => (n < 10 ? "0" : "") + n
        let out = pad(Math.floor(cc / 360000)) + ":"
                + pad(Math.floor((cc % 360000) / 6000)) + ":"
                + pad(Math.floor((cc % 6000) / 100))
        if (tickStepSeconds < 1)
            out += "." + pad(cc % 100)
        return out
    }
    readonly property var tracks: EditorState.tracks
    readonly property real playheadSeconds: EditorState.playheadSeconds
    readonly property int selectedTrack: EditorState.selectedTrack
    readonly property int selectedClip: EditorState.selectedClip

    // True while a library asset is being dragged in from the media panel.
    readonly property bool assetDragActive: EditorState.draggingAssetIndex >= 0

    // Nothing here may depend on the drag: a reserved lane that appeared when a
    // drag started shifted every track row, the Flickable's contentHeight and
    // the drop overlay at the exact moment QDrag::exec() entered its nested
    // event loop, so Mutter hit-tested against geometry that had just moved out
    // from under it — the target vanished mid-drag and only the track already
    // under the pointer would accept. A new track is signalled by an insertion
    // line drawn over the tracks instead, which costs no layout.

    // Live snap guide (seconds; < 0 when hidden) shown while dragging a clip.
    property real snapGuideSeconds: -1
    // Landing preview outline: where a dragged asset (from the library) or an
    // existing clip being moved would come to rest, snapped.
    property int dropTrackIndex: -1
    property real dropStartSeconds: 0
    property real dropDurationSeconds: 0
    // True while a library asset would land on a brand-new track, and the index
    // that track would be inserted at (0 = above everything, tracks.length =
    // below everything).
    property bool dropCreatesNewTrack: false
    property int dropNewTrackIndex: 0
    // Clip under an in-progress effect drag (for drop highlight).
    property int effectDropTrackIndex: -1
    property int effectDropClipIndex: -1
    // CapCut-style marquee (drag on empty track space to box-select clips).
    property bool marqueeActive: false
    property bool marqueeAdditive: false
    property real marqueeOriginX: 0
    property real marqueeOriginY: 0
    property real marqueeCurrentX: 0
    property real marqueeCurrentY: 0
    readonly property real marqueeLeft: Math.min(marqueeOriginX, marqueeCurrentX)
    readonly property real marqueeTop: Math.min(marqueeOriginY, marqueeCurrentY)
    readonly property real marqueeWidth: Math.abs(marqueeCurrentX - marqueeOriginX)
    readonly property real marqueeHeight: Math.abs(marqueeCurrentY - marqueeOriginY)

    // Clips whose timeline rect intersects the marquee (track-column coords).
    function clipsInMarquee(left, top, right, bottom) {
        const pairs = []
        for (let t = 0; t < tracks.length; t++) {
            const trackTop = trackOffsetY(t)
            const trackBottom = trackTop + trackHeight(t)
            if (trackBottom < top || trackTop > bottom)
                continue
            const clips = tracks[t].clips
            for (let c = 0; c < clips.length; c++) {
                const clipLeft = clips[c].start * pxPerSecond
                const clipRight = (clips[c].start + clips[c].duration) * pxPerSecond
                if (clipRight < left || clipLeft > right)
                    continue
                pairs.push({ "track": t, "clip": c })
            }
        }
        return pairs
    }

    function applyMarqueeSelection() {
        const hits = clipsInMarquee(marqueeLeft, marqueeTop,
                                    marqueeLeft + marqueeWidth,
                                    marqueeTop + marqueeHeight)
        if (marqueeAdditive) {
            const merged = []
            const seen = {}
            const existing = EditorState.selection
            for (let i = 0; i < existing.length; i++) {
                const key = existing[i].track + ":" + existing[i].clip
                seen[key] = true
                merged.push({ "track": existing[i].track, "clip": existing[i].clip })
            }
            for (let i = 0; i < hits.length; i++) {
                const key = hits[i].track + ":" + hits[i].clip
                if (seen[key])
                    continue
                seen[key] = true
                merged.push(hits[i])
            }
            EditorState.setSelection(merged)
        } else {
            EditorState.setSelection(hits)
        }
    }

    function endMarquee(apply) {
        if (!marqueeActive)
            return
        if (apply && (marqueeWidth > 2 || marqueeHeight > 2))
            applyMarqueeSelection()
        else if (apply && !marqueeAdditive)
            EditorState.clearSelection()
        marqueeActive = false
    }

    // While dragging a clip, other selected clips (linked A/V partners included)
    // ride along on the X axis so they don't sit still until drop. CapCut-style.
    property bool moveFollowActive: false
    property int moveLeaderTrack: -1
    property int moveLeaderClip: -1
    property real moveFollowDeltaX: 0

    function beginMoveFollow(trackIndex, clipIndex) {
        moveLeaderTrack = trackIndex
        moveLeaderClip = clipIndex
        moveFollowDeltaX = 0
        moveFollowActive = true
    }

    function updateMoveFollow(deltaX) {
        if (!moveFollowActive)
            return
        moveFollowDeltaX = deltaX
    }

    function clearMoveFollow() {
        moveFollowActive = false
        moveLeaderTrack = -1
        moveLeaderClip = -1
        moveFollowDeltaX = 0
    }

    // Shared by library drops and in-timeline clip moves so both snap and show
    // the same outline the same way.
    function showLandingPreview(trackIndex, desiredStart, duration) {
        const snapped = snapClipStart(desiredStart, duration)
        dropTrackIndex = trackIndex
        dropStartSeconds = snapped.start
        dropDurationSeconds = duration
        snapGuideSeconds = snapped.guide
    }

    // Drops only the landing outline. dropCreatesNewTrack belongs to the
    // panel-level drop area: a track row leaving the drag must not clear it,
    // since reaching the new-track slop always fires a leave on row 0.
    function clearLandingOutline() {
        dropTrackIndex = -1
        snapGuideSeconds = -1
    }

    function clearLandingPreview() {
        clearLandingOutline()
        dropCreatesNewTrack = false
    }

    function clearEffectDropHighlight() {
        effectDropTrackIndex = -1
        effectDropClipIndex = -1
    }

    function updateEffectDropHighlight(trackIndex, xPixels) {
        const clipIndex = clipIndexAtPosition(trackIndex, xPixels)
        effectDropTrackIndex = clipIndex >= 0 ? trackIndex : -1
        effectDropClipIndex = clipIndex
    }

    // Find the outgoing (earlier) clip index for a transition drop at timeline x.
    function transitionLeftClipAtPosition(trackIndex, xPixels) {
        if (trackIndex < 0 || trackIndex >= tracks.length)
            return -1
        const track = tracks[trackIndex]
        if (track.type !== "video" && track.type !== "shape" && track.type !== "text")
            return -1
        const seconds = xPixels / pxPerSecond
        const clips = track.clips
        let best = -1
        let bestDist = 1e9
        for (let i = 0; i < clips.length; i++) {
            const left = clips[i]
            for (let j = 0; j < clips.length; j++) {
                if (i === j)
                    continue
                const right = clips[j]
                if (right.start < left.start)
                    continue
                const leftEnd = left.start + left.duration
                const gap = right.start - leftEnd
                if (gap > 0.001)
                    continue
                let regionStart
                let regionEnd
                if (right.start < leftEnd) {
                    regionStart = right.start
                    regionEnd = leftEnd
                } else {
                    regionStart = leftEnd - 0.25
                    regionEnd = leftEnd + 0.25
                }
                if (seconds >= regionStart && seconds <= regionEnd) {
                    const mid = (regionStart + regionEnd) / 2
                    const dist = Math.abs(seconds - mid)
                    if (dist < bestDist) {
                        bestDist = dist
                        best = i
                    }
                }
            }
        }
        return best
    }

    function applyTransitionDrop(trackIndex, xPixels, kind) {
        const leftClip = transitionLeftClipAtPosition(trackIndex, xPixels)
        if (leftClip < 0 || !kind || kind.length === 0)
            return
        EditorState.addTransition(trackIndex, leftClip, kind, 0.5)
    }

    function assetDurationSeconds(assetIndex) {
        const asset = AssetLibrary.assetAt(assetIndex)
        if (!asset)
            return 5.0
        if (asset.kind === "image" || !(asset.durationSeconds > 0))
            return 5.0
        return asset.durationSeconds
    }

    // Snap a clip's desired start against timeline targets, testing both edges.
    // Returns {start, guide}; guide < 0 means no snap occurred.
    function snapClipStart(desiredStart, duration) {
        const l = EditorState.snapTime(desiredStart)
        const rEdge = EditorState.snapTime(desiredStart + duration)
        const lSnapped = Math.abs(l - desiredStart) > 0.0005
        const rSnapped = Math.abs(rEdge - (desiredStart + duration)) > 0.0005
        if (lSnapped && (!rSnapped || Math.abs(l - desiredStart) <= Math.abs(rEdge - duration - desiredStart)))
            return { "start": l, "guide": l }
        if (rSnapped)
            return { "start": rEdge - duration, "guide": rEdge }
        return { "start": desiredStart, "guide": -1 }
    }

    // Default row height for a track type, before the per-track scale.
    function trackBaseHeight(type) {
        if (type === "video") return Theme.trackHeightVideo;
        if (type === "audio") return Theme.trackHeightAudio;
        if (type === "shape") return Theme.trackHeightShape;
        if (type === "subtitle") return Theme.trackHeightSubtitle;
        return Theme.trackHeightText;
    }

    // Actual row height, including the lane's DAW-style vertical zoom.
    function trackHeight(index) {
        if (index < 0 || index >= tracks.length)
            return Theme.trackHeightVideo
        const track = tracks[index]
        const scale = track.heightScale > 0 ? track.heightScale : 1
        return Math.round(Math.max(20, trackBaseHeight(track.type) * scale))
               + maskLaneBlockHeight(index)
    }

    // Height of one row on a track's cutout lane.
    readonly property real maskLaneRowHeight: 18

    // The cutout lane's total height on a track, or 0 when the track has no cutouts — a track
    // that will never carry one should not pay vertical space for a lane it does not show.
    function maskLaneBlockHeight(index) {
        void tracks
        const rows = EditorState.trackMaskLaneCount(index)
        return rows > 0 ? rows * maskLaneRowHeight + Theme.borderWidth : 0
    }

    function trackMasksFor(index) {
        void tracks
        return EditorState.trackMasks(index)
    }

    function clipColor(type) {
        if (type === "text") return Theme.clipText;
        if (type === "subtitle") return Theme.clipSubtitle;
        if (type === "audio") return Theme.clipAudio;
        if (type === "graphic") return Theme.clipGraphic;
        if (type === "effect") return Theme.clipEffect;
        return Theme.clipVideoPlaceholder; // video: no flat fill, thumbnails would go here
    }

    function totalTracksHeight() {
        var h = 0;
        for (var i = 0; i < tracks.length; i++) {
            h += trackHeight(i);
            if (i > 0) h += Theme.trackGap;
        }
        return h;
    }

    // Finds the clip (if any) under a given x position (px) on a track, for
    // dropping an effect card directly onto a clip.
    function clipIndexAtPosition(trackIndex, xPixels) {
        if (trackIndex < 0 || trackIndex >= tracks.length)
            return -1
        const seconds = xPixels / pxPerSecond
        const clips = tracks[trackIndex].clips
        for (var i = 0; i < clips.length; i++) {
            if (seconds >= clips[i].start && seconds < clips[i].start + clips[i].duration)
                return i
        }
        return -1
    }

    function timelineHasClips() {
        for (var i = 0; i < tracks.length; i++) {
            if (tracks[i].clips.length > 0)
                return true
        }
        return false
    }

    function firstCompatibleTrackIndex(assetIndex) {
        for (var i = 0; i < tracks.length; i++) {
            if (EditorState.trackAcceptsAsset(i, assetIndex))
                return i
        }
        return -1
    }

    function trackIndexAtY(y) {
        var cursor = 0;
        for (var i = 0; i < tracks.length; i++) {
            const th = trackHeight(i);
            if (y >= cursor && y < cursor + th)
                return i;
            cursor += th + Theme.trackGap;
        }
        return -1;
    }

    // Depth of the "insert a new track here" band on a track row.
    function newTrackEdge(index) {
        return Math.min(Theme.newTrackHitSlop, trackHeight(index) / 4)
    }

    // Resolve a track-column y into a drop target, in the coordinates the track
    // rows actually live in (no reserved lane, so column y == track y).
    // Returns { newTrack: false, track: i } to land on an existing track, or
    // { newTrack: true, insertIndex: n } to create one at that index.
    //
    // Landing on the top/bottom edge of a row means "new track above/below it",
    // which is what makes a lane reachable underneath the last track — dropping
    // in the empty space below the tracks appends one.
    function assetDropTargetAtY(assetIndex, y) {
        const count = tracks.length
        if (count === 0)
            return { "newTrack": true, "insertIndex": 0 }

        var cursor = 0
        for (var i = 0; i < count; i++) {
            const h = trackHeight(i)
            const rowEnd = cursor + h
            // Claim the trailing gap too, so the 6px between rows resolves to a
            // boundary rather than to nothing.
            if (y < rowEnd + Theme.trackGap / 2) {
                if (!EditorState.trackAcceptsAsset(i, assetIndex))
                    return { "newTrack": true,
                             "insertIndex": y < cursor + h / 2 ? i : i + 1 }
                const edge = newTrackEdge(i)
                // Only the topmost row needs a leading band: on every other row
                // that boundary is already the previous row's trailing band, and
                // claiming it twice just ate the middle of the track.
                if (i === 0 && y < cursor + edge)
                    return { "newTrack": true, "insertIndex": 0 }
                if (y >= rowEnd - edge)
                    return { "newTrack": true, "insertIndex": i + 1 }
                return { "newTrack": false, "track": i }
            }
            cursor = rowEnd + Theme.trackGap
        }
        return { "newTrack": true, "insertIndex": count }
    }

    // Y of the boundary a new track would be inserted at, in track-column
    // coordinates — where the insertion line is drawn.
    function newTrackBoundaryY(insertIndex) {
        var cursor = 0
        for (var i = 0; i < insertIndex && i < tracks.length; i++)
            cursor += trackHeight(i) + Theme.trackGap
        return Math.max(0, cursor - Theme.trackGap / 2)
    }

    function updateAssetDropPreview(assetIndex, dropX, dropY) {
        const duration = assetDurationSeconds(assetIndex)
        const desired = Math.max(0, dropX / pxPerSecond)

        // Mirrors performAssetDrop: an empty project fills its existing track
        // wherever you aim, rather than stacking a second one on top.
        if (!timelineHasClips()) {
            const firstIdx = firstCompatibleTrackIndex(assetIndex)
            if (firstIdx >= 0) {
                dropCreatesNewTrack = false
                showLandingPreview(firstIdx, desired, duration)
                return
            }
        }

        const target = assetDropTargetAtY(assetIndex, dropY)

        if (!target.newTrack) {
            dropCreatesNewTrack = false
            showLandingPreview(target.track, desired, duration)
            return
        }

        const snapped = snapClipStart(desired, duration)
        dropCreatesNewTrack = true
        dropNewTrackIndex = target.insertIndex
        dropTrackIndex = -1
        dropStartSeconds = snapped.start
        dropDurationSeconds = duration
        snapGuideSeconds = snapped.guide
    }

    function performAssetDrop(assetIndex, dropX, dropY) {
        const atSeconds = Math.max(0, dropX / pxPerSecond)
        if (assetIndex < 0)
            return

        function runAdd() {
            // An empty project should fill its existing track rather than stack
            // a second one on top of it.
            if (!timelineHasClips()) {
                const firstIdx = firstCompatibleTrackIndex(assetIndex)
                if (firstIdx >= 0) {
                    EditorState.addClipFromAssetAt(assetIndex, firstIdx, atSeconds)
                    return
                }
            }

            const target = assetDropTargetAtY(assetIndex, dropY)
            if (target.newTrack)
                EditorState.addClipFromAssetOnNewTrackAt(assetIndex, target.insertIndex, atSeconds)
            else
                EditorState.addClipFromAssetAt(assetIndex, target.track, atSeconds)
        }

        if (typeof Window !== "undefined" && Window.window && Window.window.configureAndAddAsset)
            Window.window.configureAndAddAsset(assetIndex, runAdd)
        else
            runAdd()
    }

    function handleTimelineWheel(wheel) {
        const maxX = Math.max(0, flick.contentWidth - flick.width)
        const maxY = Math.max(0, flick.contentHeight - flick.height)
        if (wheel.modifiers & Qt.ControlModifier) {
            // Wheel MouseAreas live in content space, so wheel.x is already a
            // content X — keep that time fixed under the cursor.
            const anchorSeconds = Math.max(0, wheel.x / pxPerSecond)
            const viewportX = wheel.x - flick.contentX
            const factor = wheel.angleDelta.y > 0 ? 1.15 : 1.0 / 1.15
            setZoomAround(zoom * factor, anchorSeconds, viewportX)
            return
        }

        const dy = wheel.angleDelta.y
        const dx = wheel.angleDelta.x

        // Shift forces horizontal scrolling regardless of overflow.
        if (wheel.modifiers & Qt.ShiftModifier) {
            const delta = dy !== 0 ? dy : dx
            flick.contentX = Math.max(0, Math.min(maxX, flick.contentX - delta))
            return
        }

        // Trackpad horizontal component always scrolls horizontally.
        if (dx !== 0)
            flick.contentX = Math.max(0, Math.min(maxX, flick.contentX - dx))

        // Vertical wheel scrolls the tracks when they overflow the viewport;
        // otherwise it falls back to horizontal so short timelines keep the
        // previous wheel-to-pan behaviour.
        if (dy !== 0) {
            if (maxY > 0)
                flick.contentY = Math.max(0, Math.min(maxY, flick.contentY - dy))
            else
                flick.contentX = Math.max(0, Math.min(maxX, flick.contentX - dy))
        }
    }

    function formatTime(seconds) {
        const total = Math.max(0, Math.round(seconds));
        const m = Math.floor(total / 60);
        const s = total % 60;
        return m.toString().padStart(2, "0") + ":" + s.toString().padStart(2, "0");
    }

    property int renameClipTrack: -1
    property int renameClipIndex: -1

    function requestRenameClip(trackIndex, clipIndex) {
        if (trackIndex < 0 || clipIndex < 0)
            return
        if (trackIndex >= root.tracks.length)
            return
        const clips = root.tracks[trackIndex].clips || []
        if (clipIndex >= clips.length)
            return
        root.renameClipTrack = trackIndex
        root.renameClipIndex = clipIndex
        clipRenameField.text = clips[clipIndex].name || ""
        clipRenameDialog.open()
    }

    function ensurePlayheadVisible() {
        const playheadX = EditorState.playheadSeconds * pxPerSecond;
        const margin = 64;
        var target = -1
        if (playheadX < flick.contentX + margin)
            target = Math.max(0, playheadX - margin)
        else if (playheadX > flick.contentX + flick.width - margin)
            target = Math.min(Math.max(0, flick.contentWidth - flick.width),
                              playheadX - flick.width + margin)
        if (target < 0)
            return
        // During play, assign contentX directly — restarting a NumberAnimation
        // every ~16ms on a multi-hour Flickable freezes the UI.
        if (EditorState.playing || flick.dragging || flick.flicking) {
            flick.contentX = target
            return
        }
        scrollToX(target)
    }

    // Smooth horizontal scroll helper. Skipped while the user is dragging the
    // view, so it never fights a flick in progress.
    function scrollToX(target) {
        if (flick.dragging || flick.flicking) {
            flick.contentX = target
            return
        }
        contentXAnimation.stop()
        contentXAnimation.from = flick.contentX
        contentXAnimation.to = target
        contentXAnimation.start()
    }

    NumberAnimation {
        id: contentXAnimation
        target: flick
        property: "contentX"
        duration: Theme.durationBase
        easing.type: Theme.easingInOut
    }

    Column {
        anchors.fill: parent

        // === toolbar =================================================================
        TimelineToolbar {
            id: toolbar
            width: parent.width
            panel: root
        }

        // === ruler + track labels + tracks ================================================
        Column {
            width: parent.width
            height: parent.height - toolbar.height

            KeyframeGraph {
                id: keyframesBar
                width: parent.width
                pxPerSecond: root.pxPerSecond
                labelsWidth: Theme.trackLabelsWidth
                propertiesTab: root.propertiesTab
                // Keep keys/playhead lined up with the track scroll view below.
                contentX: flick.contentX
                contentWidth: flick.contentWidth
            }

            SubtitleCueLane {
                id: subtitleLane
                width: parent.width
                pxPerSecond: root.pxPerSecond
                labelsWidth: Theme.trackLabelsWidth
                contentX: flick.contentX
                contentWidth: flick.contentWidth
            }

            Row {
                width: parent.width
                height: parent.height - keyframesBar.height - subtitleLane.height

            // --- fixed left label column --------------------------------------------
            Column {
                width: Theme.trackLabelsWidth
                height: parent.height

                // CapCut-style: add-track sits at the timeline origin, above the
                // track headers, not buried in the top toolbar.
                Item {
                    width: parent.width
                    height: Theme.timelineRulerHeight + Theme.timelineBookmarkRowHeight

                    IconButton {
                        id: addTrackButton
                        anchors.centerIn: parent
                        glyph: Theme.icons.plus
                        variant: "text"
                        tooltip: qsTr("Add new track")
                        onClicked: addTrackMenu.open()

                        NewTrackMenu {
                            id: addTrackMenu
                            x: Math.max(0, (addTrackButton.width - width) / 2)
                            y: addTrackButton.height + 4
                        }
                    }

                    Rectangle {
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        width: 1
                        height: parent.height
                        color: Theme.panelBorder
                    }
                    Rectangle {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        height: 1
                        color: Theme.panelBorder
                    }
                }

                TrackHeaderColumn {
                    id: trackLabelsArea
                    width: parent.width
                    // Clamped: went negative at small panel heights, which spilled
                    // the absolutely-positioned label rows out of the column.
                    height: Math.max(0, parent.height - Theme.timelineRulerHeight
                                        - Theme.timelineBookmarkRowHeight)
                    tracks: root.tracks
                    contentY: flick.contentY
                }
            }

            // --- scrollable ruler + tracks --------------------------------------------
            Flickable {
                id: flick
                width: parent.width - Theme.trackLabelsWidth
                height: parent.height

                // Height of the pinned ruler + bookmark strip at the top.
                readonly property real headerHeight: Theme.timelineRulerHeight
                                                     + Theme.timelineBookmarkRowHeight

                contentWidth: Math.max(width,
                    EditorState.durationSeconds * root.pxPerSecond + root.timelineEndPadPx)
                // Was `height`, so once the tracks were taller than the panel the
                // lower ones were silently truncated and could not be reached at
                // all. totalTracksHeight() was already computed but never used.
                contentHeight: Math.max(height,
                                        headerHeight + root.totalTracksHeight()
                                        + Theme.trackGap)
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.horizontal: AppScrollBar { policy: ScrollBar.AlwaysOn }
                ScrollBar.vertical: AppScrollBar { }

                Item {
                    id: timelineContent
                    width: flick.contentWidth
                    height: flick.contentHeight

                    // Wheel handling: scoped to the ruler so it does not block timeline drops.
                    MouseArea {
                        id: rulerWheelArea
                        width: parent.width
                        y: flick.contentY
                        height: Theme.timelineRulerHeight + Theme.timelineBookmarkRowHeight
                        z: 1
                        acceptedButtons: Qt.NoButton
                        onWheel: (wheel) => root.handleTimelineWheel(wheel)
                    }

                    // Horizontal scroll / zoom wheel over track rows (below the drop overlay).
                    MouseArea {
                        x: 0
                        y: Theme.timelineRulerHeight + Theme.timelineBookmarkRowHeight
                        width: parent.width
                        height: Math.max(root.totalTracksHeight(), Theme.trackHeightVideo)
                        z: 150
                        acceptedButtons: Qt.NoButton
                        onWheel: (wheel) => root.handleTimelineWheel(wheel)
                    }

                    // seek strip (ruler + bookmark lane) ------------------------------------
                    // CapCut/Premiere-style: the whole pinned header is one scrub area so
                    // the playhead is easy to move without hunting for a tiny hit target.
                    Item {
                        id: seekStrip
                        width: parent.width
                        y: flick.contentY
                        z: 2
                        height: Theme.timelineRulerHeight + Theme.timelineBookmarkRowHeight

                        Rectangle {
                            anchors.fill: parent
                            color: Theme.panelBackground
                        }

                        // Subtle bottom edge so the seek strip reads as a distinct bar.
                        Rectangle {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.bottom: parent.bottom
                            height: 1
                            color: Theme.panelBorder
                        }

                        MouseArea {
                            id: rulerScrub
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            preventStealing: true
                            // Leave room for the playhead head so its drag can start first.
                            z: 1

                            function scrubTo(x) {
                                EditorState.playheadSeconds =
                                    EditorState.snapTime(Math.max(0, x) / root.pxPerSecond)
                            }

                            // Seeking is not a selection change: scrubbing to look at a
                            // different point used to drop the clip you were editing.
                            onPressed: (mouse) => scrubTo(mouse.x)
                            onPositionChanged: (mouse) => {
                                if (pressed)
                                    scrubTo(mouse.x)
                            }
                            // MouseArea would otherwise swallow wheel and block Ctrl-zoom.
                            onWheel: (wheel) => root.handleTimelineWheel(wheel)

                            ThemedToolTip {
                                text: qsTr("Click or drag to seek")
                                visible: rulerScrub.containsMouse && !rulerScrub.pressed
                                         && !playheadDragArea.drag.active
                            }
                        }

                        // Time ticks live in the upper half of the seek strip.
                        // Only instantiate ticks that intersect the viewport — a 2h
                        // timeline at 1× zoom would otherwise create thousands of Items.
                        Item {
                            id: ruler
                            width: parent.width
                            height: Theme.timelineRulerHeight
                            z: 0

                            readonly property real tickStepPx: root.tickStepSeconds * root.pxPerSecond
                            readonly property int tickIndexMax: Math.max(0,
                                Math.ceil(flick.contentWidth / Math.max(1, tickStepPx)))
                            readonly property int firstVisibleTick: Math.max(0,
                                Math.floor(flick.contentX / Math.max(1, tickStepPx)) - 1)
                            readonly property int visibleTickCount: Math.min(
                                tickIndexMax - firstVisibleTick + 1,
                                Math.ceil(flick.width / Math.max(1, tickStepPx)) + 3)

                            Repeater {
                                model: Math.max(0, ruler.visibleTickCount)
                                delegate: Item {
                                    readonly property real tickSeconds:
                                        (ruler.firstVisibleTick + index) * root.tickStepSeconds
                                    x: tickSeconds * root.pxPerSecond
                                    width: root.tickStepSeconds * root.pxPerSecond
                                    height: ruler.height

                                    Rectangle {
                                        x: 0
                                        y: parent.height - 10
                                        width: 1
                                        height: 8
                                        color: Qt.rgba(Theme.mutedForeground.r,
                                                       Theme.mutedForeground.g,
                                                       Theme.mutedForeground.b, 0.28)
                                    }

                                    Text {
                                        x: 4
                                        y: 4
                                        text: root.formatTick(parent.tickSeconds)
                                        color: Theme.mutedForeground
                                        font.pixelSize: Theme.fontSizeTick
                                        font.family: Theme.fontFamily
                                    }
                                }
                            }
                        }

                        Item {
                            id: bookmarkRow
                            y: Theme.timelineRulerHeight
                            width: parent.width
                            height: Theme.timelineBookmarkRowHeight
                            z: 2

                            property int renameIndex: -1

                            Repeater {
                                model: EditorState.bookmarks
                                delegate: Item {
                                    id: bookmarkDelegate
                                    x: (bookmarkMouse.dragging
                                        ? bookmarkMouse.dragSeconds
                                        : modelData.seconds) * root.pxPerSecond - width / 2
                                    width: 10
                                    height: parent.height
                                    z: bookmarkMouse.containsMouse || bookmarkMouse.dragging ? 4 : 3

                                    // Stem into the ruler so markers read as jump flags, not dots.
                                    Rectangle {
                                        anchors.horizontalCenter: parent.horizontalCenter
                                        y: 0
                                        width: 1
                                        height: parent.height + 6
                                        color: Theme.primary
                                        opacity: 0.7
                                    }

                                    Rectangle {
                                        anchors.horizontalCenter: parent.horizontalCenter
                                        anchors.verticalCenter: parent.verticalCenter
                                        width: 8
                                        height: 8
                                        rotation: 45
                                        radius: 1
                                        color: Theme.primary
                                        border.width: bookmarkMouse.containsMouse ? Theme.borderWidth : 0
                                        border.color: Theme.primaryForeground
                                    }

                                    ThemedToolTip {
                                        visible: bookmarkMouse.containsMouse && !bookmarkMouse.dragging
                                        text: modelData.label + " @ "
                                              + root.formatTime(modelData.seconds)
                                    }

                                    MouseArea {
                                        id: bookmarkMouse
                                        anchors.fill: parent
                                        anchors.margins: -6
                                        hoverEnabled: true
                                        cursorShape: pressed ? Qt.ClosedHandCursor : Qt.PointingHandCursor
                                        acceptedButtons: Qt.LeftButton | Qt.RightButton
                                        // Stop the seek strip from eating bookmark clicks / drags.
                                        preventStealing: true

                                        property bool dragging: false
                                        property bool didDrag: false
                                        property real dragSeconds: modelData.seconds
                                        property real pressX: 0

                                        onPressed: (mouse) => {
                                            if (mouse.button !== Qt.LeftButton)
                                                return
                                            didDrag = false
                                            dragging = false
                                            pressX = mouse.x
                                            dragSeconds = modelData.seconds
                                        }
                                        onPositionChanged: (mouse) => {
                                            if (!pressed || !(mouse.buttons & Qt.LeftButton))
                                                return
                                            if (!didDrag && Math.abs(mouse.x - pressX) < 3)
                                                return
                                            didDrag = true
                                            dragging = true
                                            const globalX = mapToItem(bookmarkRow, mouse.x, 0).x
                                            dragSeconds = EditorState.snapTime(
                                                Math.max(0, globalX / root.pxPerSecond))
                                        }
                                        onReleased: (mouse) => {
                                            if (mouse.button === Qt.LeftButton && didDrag) {
                                                EditorState.updateBookmark(
                                                    index, dragSeconds, modelData.label)
                                            }
                                            dragging = false
                                            // Keep didDrag through onClicked (fires after release).
                                            Qt.callLater(function() { didDrag = false })
                                        }
                                        onClicked: (mouse) => {
                                            if (mouse.button === Qt.RightButton) {
                                                bookmarkContextMenu.bookmarkIndex = index
                                                bookmarkContextMenu.bookmarkLabel = modelData.label
                                                bookmarkContextMenu.bookmarkSeconds = modelData.seconds
                                                bookmarkContextMenu.popup()
                                                return
                                            }
                                            if (!didDrag)
                                                EditorState.goToBookmark(index)
                                        }
                                        onDoubleClicked: (mouse) => {
                                            if (mouse.button !== Qt.LeftButton)
                                                return
                                            bookmarkRow.renameIndex = index
                                            bookmarkRenameField.text = modelData.label
                                            bookmarkRenameDialog.open()
                                        }
                                    }
                                }
                            }

                            ThemedContextMenu {
                                id: bookmarkContextMenu
                                property int bookmarkIndex: -1
                                property string bookmarkLabel: ""
                                property real bookmarkSeconds: 0

                                ThemedMenuItem {
                                    text: qsTr("Go to bookmark")
                                    icon.name: Theme.icons.bookmark
                                    onTriggered: EditorState.goToBookmark(bookmarkContextMenu.bookmarkIndex)
                                }
                                ThemedMenuItem {
                                    text: qsTr("Rename…")
                                    onTriggered: {
                                        bookmarkRow.renameIndex = bookmarkContextMenu.bookmarkIndex
                                        bookmarkRenameField.text = bookmarkContextMenu.bookmarkLabel
                                        bookmarkRenameDialog.open()
                                    }
                                }
                                ThemedMenuSeparator { }
                                ThemedMenuItem {
                                    text: qsTr("Delete")
                                    icon.name: Theme.icons.trash
                                    onTriggered: EditorState.removeBookmark(bookmarkContextMenu.bookmarkIndex)
                                }
                            }
                        }

                        // Work area highlight in the bookmark lane (between In/Out markers).
                        Rectangle {
                            visible: EditorState.workAreaActive
                            y: Theme.timelineRulerHeight
                            height: Theme.timelineBookmarkRowHeight
                            x: EditorState.workAreaInSeconds * root.pxPerSecond
                            width: Math.max(0, (EditorState.workAreaOutSeconds - EditorState.workAreaInSeconds)
                                             * root.pxPerSecond)
                            color: Qt.rgba(Theme.primary.r, Theme.primary.g, Theme.primary.b, 0.18)
                            z: 1
                        }
                    }

                    // Work area overlay across the track rows.
                    Item {
                        id: workAreaOverlay
                        z: 1
                        y: Theme.timelineRulerHeight + Theme.timelineBookmarkRowHeight
                        width: parent.width
                        height: Math.max(root.totalTracksHeight() + Theme.trackGap,
                                         flick.height - flick.headerHeight)

                        readonly property real inX: EditorState.workAreaInSeconds >= 0
                                                   ? EditorState.workAreaInSeconds * root.pxPerSecond
                                                   : -1
                        readonly property real outX: EditorState.workAreaOutSeconds >= 0
                                                    ? EditorState.workAreaOutSeconds * root.pxPerSecond
                                                    : -1

                        Rectangle {
                            visible: EditorState.workAreaActive
                            x: parent.inX
                            width: Math.max(0, parent.outX - parent.inX)
                            anchors.top: parent.top
                            anchors.bottom: parent.bottom
                            color: Qt.rgba(Theme.primary.r, Theme.primary.g, Theme.primary.b, 0.08)
                        }

                        Rectangle {
                            visible: EditorState.workAreaInSeconds >= 0
                            x: parent.inX
                            width: 2
                            anchors.top: parent.top
                            anchors.bottom: parent.bottom
                            color: Theme.primary
                            opacity: 0.85
                        }

                        Rectangle {
                            visible: EditorState.workAreaOutSeconds >= 0
                            x: parent.outX - width
                            width: 2
                            anchors.top: parent.top
                            anchors.bottom: parent.bottom
                            color: Theme.primary
                            opacity: 0.85
                        }
                    }

                    // A project with no tracks used to render as a completely
                    // blank timeline, with the only route to a first track being
                    // one button among sixteen in the toolbar.
                    EmptyState {
                        x: flick.contentX + (flick.width - width) / 2
                        y: flick.contentY + flick.headerHeight
                           + Math.max(0, (flick.height - flick.headerHeight - height) / 2)
                        width: Math.min(flick.width - Theme.spacing3xl, 320)
                        visible: root.tracks.length === 0
                        z: 4
                        glyph: Theme.icons.layers
                        title: qsTr("Your timeline is empty")
                        hint: qsTr("Drag media here from the library, or add an empty track to start.")
                        actionText: qsTr("New track")
                        actionVariant: "primary"
                        onActionTriggered: newTrackMenuFromEmpty.open()

                        NewTrackMenu {
                            id: newTrackMenuFromEmpty
                            y: parent.height
                        }
                    }

                    // track rows ---------------------------------------------------------
                    // CapCut marquee: drag empty track space to box-select.
                    // Sits under the track column so clips still receive presses;
                    // empty gaps fall through to this grabber.
                    MouseArea {
                        id: marqueeArea
                        x: 0
                        y: Theme.timelineRulerHeight + Theme.timelineBookmarkRowHeight
                        width: parent.width
                        height: Math.max(root.totalTracksHeight() + Theme.trackGap,
                                         flick.height - flick.headerHeight)
                        z: 0
                        enabled: root.timelineTool === ""
                        acceptedButtons: Qt.LeftButton
                        // CapCut: empty-space drag is marquee, not flick-scroll.
                        preventStealing: true
                        cursorShape: pressed ? Qt.CrossCursor : Qt.ArrowCursor

                        onPressed: (mouse) => {
                            root.marqueeAdditive = (mouse.modifiers & Qt.ShiftModifier) !== 0
                            root.marqueeOriginX = mouse.x
                            root.marqueeOriginY = mouse.y
                            root.marqueeCurrentX = mouse.x
                            root.marqueeCurrentY = mouse.y
                            root.marqueeActive = true
                            if (!root.marqueeAdditive)
                                EditorState.clearSelection()
                        }
                        onPositionChanged: (mouse) => {
                            if (!root.marqueeActive)
                                return
                            root.marqueeCurrentX = Math.max(0, mouse.x)
                            root.marqueeCurrentY = Math.max(0, mouse.y)
                            if (root.marqueeWidth > 2 || root.marqueeHeight > 2)
                                root.applyMarqueeSelection()
                        }
                        onReleased: root.endMarquee(true)
                        onCanceled: root.endMarquee(false)
                    }

                    Column {
                        id: trackColumn
                        y: Theme.timelineRulerHeight + Theme.timelineBookmarkRowHeight
                        width: parent.width
                        spacing: Theme.trackGap
                        z: 1

                        Repeater {
                            model: root.tracks.length
                            delegate: Rectangle {
                                id: trackRow
                                property int trackIndex: index
                                width: flick.contentWidth
                                height: root.trackHeight(trackIndex)
                                // Faint row tint on hover, and an empty track now
                                // reads as a track rather than as blank space.
                                color: trackHover.hovered
                                       ? Qt.rgba(Theme.panelAccent.r, Theme.panelAccent.g,
                                                 Theme.panelAccent.b, 0.5)
                                       : Qt.rgba(Theme.panelAccent.r, Theme.panelAccent.g,
                                                 Theme.panelAccent.b, 0.22)

                                Behavior on color {
                                    ColorAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
                                }

                                HoverHandler { id: trackHover }

                                DropArea {
                                    anchors.fill: parent
                                    keys: ["text/plain", "application/x-drift-effect",
                                           "application/x-drift-audio-effect",
                                           "application/x-drift-shape", "application/x-drift-transition"]

                                    function isEffectDrag(drop) {
                                        return drop.keys.indexOf("application/x-drift-effect") !== -1
                                    }

                                    function isAudioEffectDrag(drop) {
                                        return drop.keys.indexOf("application/x-drift-audio-effect") !== -1
                                    }

                                    function isShapeDrag(drop) {
                                        return drop.keys.indexOf("application/x-drift-shape") !== -1
                                    }

                                    function isTransitionDrag(drop) {
                                        return drop.keys.indexOf("application/x-drift-transition") !== -1
                                    }

                                    function assetIndexFromDrop(drop) {
                                        if (EditorState.draggingAssetIndex >= 0)
                                            return EditorState.draggingAssetIndex
                                        const text = drop.hasText ? drop.text : drop.getDataAsString("text/plain")
                                        const idx = parseInt(text)
                                        return isNaN(idx) ? -1 : idx
                                    }

                                    function updateAssetPreview(drop) {
                                        if (isTransitionDrag(drop)) {
                                            root.clearLandingPreview()
                                            root.clearEffectDropHighlight()
                                            return
                                        }
                                        if (isEffectDrag(drop) || isAudioEffectDrag(drop)) {
                                            root.updateEffectDropHighlight(trackRow.trackIndex, drop.x)
                                            return
                                        }
                                        root.clearEffectDropHighlight()
                                        if (isShapeDrag(drop)) {
                                            if (root.tracks[trackRow.trackIndex].type === "shape") {
                                                const desired = Math.max(0, drop.x / root.pxPerSecond)
                                                root.showLandingPreview(trackRow.trackIndex, desired, 5.0)
                                            } else {
                                                root.clearLandingPreview()
                                            }
                                            return
                                        }
                                        // Media fallback when the panel overlay does not
                                        // receive the drag (seen on some Wayland compositors).
                                        // drop.y is row-local, so lift it into track-column
                                        // coordinates and reuse the same resolution the
                                        // overlay uses — otherwise the two disagree about
                                        // where the drop would land.
                                        const assetIndex = assetIndexFromDrop(drop)
                                        if (assetIndex < 0)
                                            return
                                        root.updateAssetDropPreview(
                                            assetIndex, drop.x,
                                            root.trackOffsetY(trackRow.trackIndex) + drop.y)
                                    }

                                    onEntered: (drop) => updateAssetPreview(drop)
                                    onPositionChanged: (drop) => updateAssetPreview(drop)
                                    onExited: {
                                        root.clearLandingOutline()
                                        root.clearEffectDropHighlight()
                                    }
                                    onDropped: (drop) => {
                                        drop.accept(Qt.CopyAction)
                                        if (isTransitionDrag(drop)) {
                                            const kind = drop.getDataAsString("application/x-drift-transition")
                                            root.applyTransitionDrop(trackRow.trackIndex, drop.x, kind)
                                            return
                                        }
                                        if (isEffectDrag(drop)) {
                                            const effectId = drop.getDataAsString("application/x-drift-effect")
                                            const clipIndex = root.clipIndexAtPosition(trackRow.trackIndex, drop.x)
                                            root.clearEffectDropHighlight()
                                            if (clipIndex >= 0 && effectId.length > 0) {
                                                EditorState.addEffect(trackRow.trackIndex, clipIndex, effectId)
                                                EditorState.selectClip(trackRow.trackIndex, clipIndex)
                                            }
                                            return
                                        }
                                        if (isAudioEffectDrag(drop)) {
                                            const effectId = drop.getDataAsString("application/x-drift-audio-effect")
                                            const clipIndex = root.clipIndexAtPosition(trackRow.trackIndex, drop.x)
                                            root.clearEffectDropHighlight()
                                            if (clipIndex >= 0 && effectId.length > 0) {
                                                EditorState.addAudioEffect(trackRow.trackIndex, clipIndex, effectId)
                                                EditorState.selectClip(trackRow.trackIndex, clipIndex)
                                            }
                                            return
                                        }
                                        if (isShapeDrag(drop)) {
                                            const shapeId = drop.getDataAsString("application/x-drift-shape")
                                            const atSeconds = Math.max(0, drop.x / root.pxPerSecond)
                                            root.clearLandingPreview()
                                            EditorState.addShapeClipAt(shapeId, trackRow.trackIndex, atSeconds)
                                            return
                                        }
                                        const assetIndex = assetIndexFromDrop(drop)
                                        if (assetIndex < 0)
                                            return
                                        root.clearLandingPreview()
                                        // Reconstruct track-column Y so performAssetDrop
                                        // resolves the same target this row saw.
                                        const dropY = root.trackOffsetY(trackRow.trackIndex)
                                                    + drop.y
                                        root.performAssetDrop(assetIndex, drop.x, dropY)
                                    }
                                }

                                // Landing preview outline (library drop or clip move).
                                // Hidden while creating a new track — that state uses the
                                // reserved lane above, not a ghost on an existing row.
                                Rectangle {
                                    visible: root.dropTrackIndex === trackRow.trackIndex
                                             && !root.dropCreatesNewTrack
                                    x: root.dropStartSeconds * root.pxPerSecond
                                    width: root.dropDurationSeconds * root.pxPerSecond
                                    height: parent.height
                                    radius: Theme.radiusSm
                                    color: Qt.rgba(Theme.primary.r, Theme.primary.g, Theme.primary.b, 0.12)
                                    border.width: 2
                                    border.color: Theme.primary
                                    z: 5
                                }

                                Repeater {
                                    model: root.tracks[trackRow.trackIndex].clips.length
                                    delegate: TimelineClipItem {
                                        // panel: root is safe — TimelineClipItem's id is clipItem,
                                        // so it does not shadow TimelinePanel's root.
                                        panel: root
                                        timelineColumn: trackColumn
                                        trackIndex: trackRow.trackIndex
                                    }
                                }

                                // Cutout lane. The row stays one track — one header, one reorder
                                // handle, moved as a unit — but is divided so cutouts can be
                                // timed against the clips beneath them.
                                Item {
                                    id: maskLane
                                    readonly property real blockHeight:
                                        root.maskLaneBlockHeight(trackRow.trackIndex)
                                    visible: blockHeight > 0
                                    x: 0
                                    y: 0
                                    width: parent.width
                                    height: blockHeight
                                    z: 6

                                    Rectangle {
                                        anchors.fill: parent
                                        color: Qt.rgba(Theme.panelMuted.r, Theme.panelMuted.g,
                                                       Theme.panelMuted.b, 0.5)
                                    }

                                    // The dividing line the lane is named for.
                                    Rectangle {
                                        anchors.bottom: parent.bottom
                                        width: parent.width
                                        height: Theme.borderWidth
                                        color: Theme.panelBorder
                                    }

                                    Repeater {
                                        model: root.trackMasksFor(trackRow.trackIndex)
                                        delegate: TimelineMaskItem {
                                            panel: root
                                            trackIndex: trackRow.trackIndex
                                        }
                                    }

                                    // Dropping media here adds a cutout to this track.
                                    DropArea {
                                        id: maskDrop
                                        anchors.fill: parent
                                        keys: ["text/plain", "text/uri-list"]

                                        onDropped: (drop) => {
                                            let added = -1
                                            if (drop.hasUrls && drop.urls.length > 0) {
                                                for (const url of drop.urls) {
                                                    const at = EditorState.addTrackMaskMedia(
                                                        trackRow.trackIndex, url.toString())
                                                    if (at >= 0)
                                                        added = at
                                                }
                                            } else if (EditorState.draggingAssetIndex >= 0) {
                                                added = EditorState.addTrackMaskFromAsset(
                                                    trackRow.trackIndex,
                                                    EditorState.draggingAssetIndex)
                                            }
                                            if (added >= 0) {
                                                EditorState.selectMask(trackRow.trackIndex, added)
                                                drop.accept()
                                            } else {
                                                drop.accepted = false
                                            }
                                        }
                                    }

                                    Rectangle {
                                        visible: maskDrop.containsDrag
                                        anchors.fill: parent
                                        color: Theme.primary
                                        opacity: 0.2
                                    }
                                }

                                // Transition overlap regions (purple) — above clips for hit-testing.
                                Repeater {
                                    model: Math.max(0, root.tracks[trackRow.trackIndex].clips.length)
                                    delegate: Item {
                                        id: transitionRegion
                                        property int leftClipIndex: modelData
                                        property var leftClip: root.tracks[trackRow.trackIndex].clips[leftClipIndex]
                                        property string trackType: root.tracks[trackRow.trackIndex].type
                                        // Reading root.tracks keeps this bound to tracksChanged; without
                                        // that dependency the invokable is evaluated once and a transition
                                        // added later never registers (clicking would re-add a crossfade).
                                        property var transitionData: root.tracks[trackRow.trackIndex]
                                            ? EditorState.transitionBetweenClips(trackRow.trackIndex, leftClipIndex)
                                            : ({})
                                        property bool hasTransition: transitionData
                                                                       && Object.keys(transitionData).length > 0
                                        property bool transitionSelected: EditorState.selectedTransitionTrack === trackRow.trackIndex
                                                                          && EditorState.selectedTransitionLeftClip === leftClipIndex

                                        // Find partner clip that abuts/overlaps this one.
                                        property int partnerIndex: {
                                            const clips = root.tracks[trackRow.trackIndex].clips
                                            const left = leftClip
                                            if (!left)
                                                return -1
                                            let best = -1
                                            let bestStart = 1e12
                                            for (let i = 0; i < clips.length; i++) {
                                                if (i === leftClipIndex)
                                                    continue
                                                const right = clips[i]
                                                if (right.start < left.start)
                                                    continue
                                                const gap = right.start - (left.start + left.duration)
                                                if (gap > 0.001)
                                                    continue
                                                if (right.start < bestStart) {
                                                    bestStart = right.start
                                                    best = i
                                                }
                                            }
                                            return best
                                        }
                                        property var rightClip: partnerIndex >= 0
                                            ? root.tracks[trackRow.trackIndex].clips[partnerIndex]
                                            : null
                                        property bool physicallyOverlapping: leftClip && rightClip
                                            && rightClip.start < (leftClip.start + leftClip.duration)
                                        property real regionStart: {
                                            if (!leftClip || !rightClip)
                                                return 0
                                            if (hasTransition && transitionData.start !== undefined)
                                                return transitionData.start
                                            if (physicallyOverlapping)
                                                return rightClip.start
                                            return 0
                                        }
                                        property real regionEnd: {
                                            if (!leftClip || !rightClip)
                                                return 0
                                            if (hasTransition && transitionData.end !== undefined)
                                                return transitionData.end
                                            if (physicallyOverlapping)
                                                return leftClip.start + leftClip.duration
                                            return 0
                                        }
                                        property bool showRegion: (trackType === "video"
                                                                   || trackType === "shape"
                                                                   || trackType === "text")
                                                                  && leftClip && rightClip
                                                                  && (physicallyOverlapping || hasTransition)
                                                                  && regionEnd > regionStart

                                        z: 10
                                        visible: showRegion
                                        x: regionStart * root.pxPerSecond
                                        width: Math.max(8, (regionEnd - regionStart) * root.pxPerSecond)
                                        height: parent.height - Theme.clipSelectionRingWidth * 2
                                        y: Theme.clipSelectionRingWidth

                                        Rectangle {
                                            anchors.fill: parent
                                            radius: Theme.radiusSm
                                            color: transitionRegion.transitionSelected
                                                   ? Qt.rgba(Theme.transitionOverlap.r, Theme.transitionOverlap.g,
                                                             Theme.transitionOverlap.b, 0.85)
                                                   : (transitionRegion.hasTransition
                                                      ? Qt.rgba(Theme.transitionOverlap.r, Theme.transitionOverlap.g,
                                                                Theme.transitionOverlap.b, 0.65)
                                                      : Qt.rgba(Theme.transitionOverlap.r, Theme.transitionOverlap.g,
                                                                Theme.transitionOverlap.b, 0.35))
                                            border.width: transitionRegion.transitionSelected ? 2 : 1
                                            border.color: Theme.transitionOverlap

                                            // Diagonal hatch so overlaps read as a blend zone.
                                            Canvas {
                                                anchors.fill: parent
                                                anchors.margins: 1
                                                opacity: 0.35
                                                onPaint: {
                                                    const ctx = getContext("2d")
                                                    ctx.clearRect(0, 0, width, height)
                                                    ctx.strokeStyle = Theme.onMedia
                                                    ctx.lineWidth = 1
                                                    const step = 6
                                                    for (let x = -height; x < width; x += step) {
                                                        ctx.beginPath()
                                                        ctx.moveTo(x, height)
                                                        ctx.lineTo(x + height, 0)
                                                        ctx.stroke()
                                                    }
                                                }
                                                onWidthChanged: requestPaint()
                                                onHeightChanged: requestPaint()
                                            }

                                            Text {
                                                anchors.centerIn: parent
                                                visible: parent.width >= 28
                                                text: transitionRegion.hasTransition
                                                      ? (transitionRegion.transitionData.label
                                                         ? transitionRegion.transitionData.label.charAt(0)
                                                         : "≫")
                                                      : "≫"
                                                color: Theme.onMedia
                                                font.family: Theme.fontFamily
                                                font.pixelSize: Theme.fontSizeTiny
                                                font.weight: Font.Bold
                                            }

                                            ThemedToolTip {
                                                visible: transitionMouse.containsMouse
                                                         && transitionRegion.hasTransition
                                                delay: 400
                                                text: transitionRegion.transitionData.label
                                                      || transitionRegion.transitionData.kind
                                                      || ""
                                            }
                                        }

                                        DropArea {
                                            anchors.fill: parent
                                            keys: ["application/x-drift-transition"]
                                            onDropped: (drop) => {
                                                drop.accept(Qt.CopyAction)
                                                const kind = drop.getDataAsString("application/x-drift-transition")
                                                if (kind.length > 0)
                                                    EditorState.addTransition(trackRow.trackIndex,
                                                                              transitionRegion.leftClipIndex,
                                                                              kind, 0.5)
                                            }
                                        }

                                        MouseArea {
                                            id: transitionMouse
                                            anchors.fill: parent
                                            hoverEnabled: true
                                            cursorShape: Qt.PointingHandCursor
                                            onClicked: {
                                                if (transitionRegion.hasTransition) {
                                                    EditorState.selectTransition(trackRow.trackIndex,
                                                                                 transitionRegion.leftClipIndex)
                                                } else {
                                                    EditorState.addTransition(trackRow.trackIndex,
                                                                              transitionRegion.leftClipIndex,
                                                                              "crossfade", 0.5)
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }

                    // Library asset drops — declared after track rows so Wayland's
                    // Z-order DnD hit-test prefers this overlay. Do not set
                    // opacity: 0: Item docs say opacity 0 disables mouse events,
                    // and Mutter then never delivers drag moves to this DropArea.
                    DropArea {
                        id: timelineAssetDrop
                        // No `enabled: draggingAssetIndex >= 0` gate: `keys` below
                        // already excludes effect/shape/transition drags, and
                        // gating on state written by the drag source in the same
                        // tick meant that if it did not land before the first
                        // DragEnter the overlay was skipped entirely — leaving only
                        // the per-track DropAreas, which accept nothing but a
                        // compatible track and never a new-track boundary.
                        x: 0
                        y: Theme.timelineRulerHeight + Theme.timelineBookmarkRowHeight
                        width: parent.width
                        // Extends past the last track to the bottom of the
                        // content area: dropping in that empty space is how a
                        // track gets appended below the existing ones.
                        height: Math.max(root.totalTracksHeight(),
                                         flick.contentHeight - flick.headerHeight)
                        z: 250
                        keys: ["text/plain"]

                        function assetIndexFromDrop(drop) {
                            if (EditorState.draggingAssetIndex >= 0)
                                return EditorState.draggingAssetIndex
                            const text = drop.hasText ? drop.text : drop.getDataAsString("text/plain")
                            const idx = parseInt(text)
                            return isNaN(idx) ? -1 : idx
                        }

                        function updateAssetDrag(drop) {
                            const assetIndex = assetIndexFromDrop(drop)
                            if (assetIndex < 0) {
                                root.clearLandingPreview()
                                return
                            }
                            root.updateAssetDropPreview(assetIndex, drop.x, drop.y)
                        }

                        onEntered: (drop) => updateAssetDrag(drop)
                        onPositionChanged: (drop) => updateAssetDrag(drop)
                        onExited: root.clearLandingPreview()
                        onDropped: (drop) => {
                            drop.accept(Qt.CopyAction)
                            const assetIndex = assetIndexFromDrop(drop)
                            root.clearLandingPreview()
                            root.performAssetDrop(assetIndex, drop.x, drop.y)
                        }
                    }

                    // New-track indicator: a full-height ghost lane straddling the
                    // boundary the track would be inserted at, with the clip that
                    // would land in it. An overlay rather than a reserved row, so
                    // no DropArea geometry moves while a drag is live — and drawn
                    // at real track height so it reads as a track, not a hairline.
                    Item {
                        id: newTrackIndicator
                        // Not also gated on the drag being active: dropCreatesNewTrack
                        // is only ever set during one, and is cleared on exit, drop
                        // and abandon — an extra condition here is one more way for
                        // the ghost to silently not appear.
                        visible: root.dropCreatesNewTrack
                        readonly property real laneHeight:
                            root.trackBaseHeight(EditorState.trackTypeForAsset(
                                                     EditorState.draggingAssetIndex))
                        x: 0
                        y: Theme.timelineRulerHeight + Theme.timelineBookmarkRowHeight
                           + root.newTrackBoundaryY(root.dropNewTrackIndex)
                           - laneHeight / 2
                        width: parent.width
                        height: laneHeight
                        z: 240

                        Rectangle {
                            anchors.fill: parent
                            radius: Theme.radiusSm
                            color: Qt.rgba(Theme.panelBackground.r, Theme.panelBackground.g,
                                           Theme.panelBackground.b, 0.92)
                            border.width: Theme.borderWidth
                            border.color: Theme.primary
                        }

                        // The boundary itself, so it is unambiguous which gap the
                        // lane is going into.
                        Rectangle {
                            width: parent.width
                            height: 2
                            anchors.verticalCenter: parent.verticalCenter
                            color: Theme.primary
                        }

                        Rectangle {
                            x: root.dropStartSeconds * root.pxPerSecond
                            width: Math.max(2, root.dropDurationSeconds * root.pxPerSecond)
                            anchors.top: parent.top
                            anchors.bottom: parent.bottom
                            anchors.margins: 2
                            radius: Theme.radiusSm
                            color: Qt.rgba(Theme.primary.r, Theme.primary.g, Theme.primary.b, 0.3)
                            border.width: 2
                            border.color: Theme.primary
                        }
                    }

                    // CapCut-style selection box drawn while marquee-dragging.
                    Rectangle {
                        visible: root.marqueeActive
                                 && (root.marqueeWidth > 2 || root.marqueeHeight > 2)
                        x: root.marqueeLeft
                        y: Theme.timelineRulerHeight + Theme.timelineBookmarkRowHeight
                           + root.marqueeTop
                        width: root.marqueeWidth
                        height: root.marqueeHeight
                        z: 8
                        color: Qt.rgba(Theme.primary.r, Theme.primary.g, Theme.primary.b, 0.12)
                        border.width: Theme.borderWidth
                        border.color: Theme.primary
                    }

                    // snap guide -------------------------------------------------------------
                    Rectangle {
                        visible: opacity > 0
                        opacity: root.snapGuideSeconds >= 0 ? 1 : 0
                        x: root.snapGuideSeconds * root.pxPerSecond
                        y: Theme.timelineRulerHeight + Theme.timelineBookmarkRowHeight
                        width: Theme.borderWidth
                        height: root.totalTracksHeight()
                        color: Theme.snapGuide
                        z: 6

                        Behavior on opacity {
                            NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
                        }

                        // Says what the clip snapped to, instead of leaving a bare
                        // unexplained line on screen.
                        Rectangle {
                            visible: parent.opacity > 0
                            anchors.horizontalCenter: parent.horizontalCenter
                            anchors.top: parent.top
                            height: snapLabel.implicitHeight + Theme.spacingSm
                            width: snapLabel.implicitWidth + Theme.spacingLg
                            radius: Theme.radiusXs
                            color: Theme.snapGuide

                            Text {
                                id: snapLabel
                                anchors.centerIn: parent
                                text: root.formatTime(root.snapGuideSeconds)
                                color: Theme.overlayColor
                                font.family: Theme.monoFontFamily
                                font.pixelSize: Theme.fontSizeTiny
                            }
                        }
                    }

                    // playhead ---------------------------------------------------------------
                    Item {
                        id: playhead
                        y: 0
                        // Above the seek strip (z: 2), otherwise the scrub area
                        // covers the handle and swallows the press before drag starts.
                        z: 3
                        width: Theme.playheadLineWidth
                        height: Theme.timelineRulerHeight + Theme.timelineBookmarkRowHeight
                                + root.totalTracksHeight()

                        Binding {
                            target: playhead
                            property: "x"
                            value: EditorState.playheadSeconds * root.pxPerSecond
                            when: !playheadDragArea.drag.active && !playheadLineDrag.drag.active
                        }

                        // Follow either drag live so the preview scrubs with it.
                        onXChanged: {
                            if (playheadDragArea.drag.active || playheadLineDrag.drag.active)
                                EditorState.playheadSeconds = playhead.x / root.pxPerSecond
                        }

                        function finishSeek() {
                            EditorState.playheadSeconds =
                                EditorState.snapTime(playhead.x / root.pxPerSecond)
                        }

                        Rectangle {
                            anchors.left: parent.left
                            y: Theme.timelineRulerHeight * 0.55
                            width: Theme.playheadLineWidth
                            height: parent.height - y
                            color: Theme.primary
                        }

                        // CapCut/Premiere-style scrubber head in the seek strip.
                        Item {
                            id: playheadHandle
                            width: Theme.playheadHandleSize
                            height: Theme.playheadHandleSize + 2
                            x: -width / 2 + Theme.playheadLineWidth / 2
                            y: 3

                            Rectangle {
                                anchors.top: parent.top
                                anchors.horizontalCenter: parent.horizontalCenter
                                width: parent.width
                                height: parent.height * 0.55
                                radius: 2
                                color: Theme.primary
                            }

                            // Pointed tip so the head reads as a scrubber, not a knob.
                            Canvas {
                                anchors.horizontalCenter: parent.horizontalCenter
                                anchors.top: parent.top
                                anchors.topMargin: parent.height * 0.4
                                width: parent.width
                                height: parent.height * 0.6
                                onPaint: {
                                    const ctx = getContext("2d")
                                    ctx.reset()
                                    ctx.beginPath()
                                    ctx.moveTo(0, 0)
                                    ctx.lineTo(width, 0)
                                    ctx.lineTo(width * 0.5, height)
                                    ctx.closePath()
                                    ctx.fillStyle = Theme.primary
                                    ctx.fill()
                                }
                                onWidthChanged: requestPaint()
                                onHeightChanged: requestPaint()
                                Component.onCompleted: requestPaint()
                            }
                        }

                        // Wide grab across the seek strip head.
                        MouseArea {
                            id: playheadDragArea
                            width: Theme.playheadSeekGrabWidth
                            height: Theme.timelineRulerHeight + Theme.timelineBookmarkRowHeight
                            x: -(width - Theme.playheadLineWidth) / 2
                            y: 0
                            cursorShape: Qt.SizeHorCursor
                            preventStealing: true
                            hoverEnabled: true
                            drag.target: playhead
                            drag.axis: Drag.XAxis
                            drag.threshold: 0
                            drag.minimumX: 0
                            drag.maximumX: flick.contentWidth - Theme.playheadLineWidth
                            onReleased: playhead.finishSeek()
                        }

                        // Narrow grab down the timeline line — stays thin so clip
                        // clicks aren't stolen.
                        MouseArea {
                            id: playheadLineDrag
                            width: 7
                            height: parent.height - playheadDragArea.height
                            x: -(width - Theme.playheadLineWidth) / 2
                            y: playheadDragArea.height
                            cursorShape: Qt.SizeHorCursor
                            preventStealing: true
                            drag.target: playhead
                            drag.axis: Drag.XAxis
                            drag.threshold: 0
                            drag.minimumX: 0
                            drag.maximumX: flick.contentWidth - Theme.playheadLineWidth
                            onReleased: playhead.finishSeek()
                        }
                    }

                    // cut tools ----------------------------------------------------------------
                    // Sits above the tracks and playhead so it both draws the red
                    // dashed virtual playhead (and, for the trim tools, the red
                    // gradient over the doomed side) and intercepts the click.
                    Item {
                        id: cutOverlay
                        visible: root.timelineTool !== ""
                        enabled: root.timelineTool !== ""
                        x: 0
                        y: Theme.timelineRulerHeight + Theme.timelineBookmarkRowHeight
                        width: parent.width
                        height: root.totalTracksHeight()
                        z: 20

                        readonly property bool trimMode: root.timelineTool === "trimStart"
                                                         || root.timelineTool === "trimEnd"
                        readonly property var hoverClip: (root.cutHoverTrack >= 0 && root.cutHoverClip >= 0
                            && root.cutHoverTrack < root.tracks.length
                            && root.cutHoverClip < root.tracks[root.cutHoverTrack].clips.length)
                            ? root.tracks[root.cutHoverTrack].clips[root.cutHoverClip]
                            : null

                        function seconds(mx) {
                            return EditorState.snapTime(Math.max(0, mx) / root.pxPerSecond)
                        }

                        function updateHover(mx, my) {
                            root.cutHoverSeconds = seconds(mx)
                            const trackIdx = root.trackIndexAtY(my)
                            root.cutHoverTrack = trackIdx
                            root.cutHoverClip = trackIdx >= 0
                                ? root.clipIndexAtPosition(trackIdx, Math.max(0, mx))
                                : -1
                        }

                        MouseArea {
                            id: cutMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.CrossCursor
                            acceptedButtons: Qt.LeftButton
                            onEntered: cutOverlay.updateHover(mouseX, mouseY)
                            onPositionChanged: (mouse) => cutOverlay.updateHover(mouse.x, mouse.y)
                            onExited: {
                                root.cutHoverSeconds = -1
                                root.cutHoverTrack = -1
                                root.cutHoverClip = -1
                            }
                            onClicked: (mouse) => {
                                const atSeconds = cutOverlay.seconds(mouse.x)
                                const trackIdx = root.trackIndexAtY(mouse.y)
                                if (trackIdx < 0)
                                    return
                                const clipIdx = root.clipIndexAtPosition(trackIdx, Math.max(0, mouse.x))
                                if (clipIdx < 0)
                                    return
                                if (root.timelineTool === "trimStart")
                                    EditorState.splitClipLeftAt(trackIdx, clipIdx, atSeconds)
                                else if (root.timelineTool === "trimEnd")
                                    EditorState.splitClipRightAt(trackIdx, clipIdx, atSeconds)
                                else
                                    EditorState.splitClipAt(trackIdx, clipIdx, atSeconds)
                            }
                        }

                        // Red gradient on the doomed side of the hovered clip: a
                        // fixed band attached to the cut line, reddest at the
                        // pointer and fading away from it, clamped to the clip.
                        Rectangle {
                            id: doomGradient
                            readonly property real bandLength: 80
                            visible: cutOverlay.trimMode && cutOverlay.hoverClip !== null
                                     && root.cutHoverSeconds >= 0
                            readonly property real clipStartX: cutOverlay.hoverClip
                                ? cutOverlay.hoverClip.start * root.pxPerSecond : 0
                            readonly property real clipEndX: cutOverlay.hoverClip
                                ? (cutOverlay.hoverClip.start + cutOverlay.hoverClip.duration) * root.pxPerSecond : 0
                            readonly property real cutX: Math.max(clipStartX,
                                Math.min(clipEndX, root.cutHoverSeconds * root.pxPerSecond))
                            readonly property color solid: Qt.rgba(Theme.destructive.r, Theme.destructive.g,
                                                                   Theme.destructive.b, 0.55)
                            readonly property color clear: Qt.rgba(Theme.destructive.r, Theme.destructive.g,
                                                                   Theme.destructive.b, 0.0)

                            x: root.timelineTool === "trimStart"
                               ? Math.max(clipStartX, cutX - bandLength)
                               : cutX
                            width: root.timelineTool === "trimStart"
                                   ? cutX - x
                                   : Math.min(clipEndX, cutX + bandLength) - cutX
                            y: root.trackOffsetY(root.cutHoverTrack)
                            height: cutOverlay.hoverClip
                                ? root.trackHeight(root.cutHoverTrack) : 0
                            radius: Theme.radiusSm

                            gradient: Gradient {
                                orientation: Gradient.Horizontal
                                // Red toward the cut line (the pointer), fading away.
                                GradientStop {
                                    position: 0.0
                                    color: root.timelineTool === "trimStart"
                                           ? doomGradient.clear : doomGradient.solid
                                }
                                GradientStop {
                                    position: 1.0
                                    color: root.timelineTool === "trimStart"
                                           ? doomGradient.solid : doomGradient.clear
                                }
                            }
                        }

                        // Red dashed virtual playhead. A column of dashes reflows
                        // with the track height without any repaint plumbing.
                        Column {
                            visible: root.cutHoverSeconds >= 0
                            x: root.cutHoverSeconds * root.pxPerSecond
                            spacing: 3
                            Repeater {
                                model: Math.ceil(cutOverlay.height / 7)
                                delegate: Rectangle {
                                    width: 2
                                    height: 4
                                    color: Theme.destructive
                                }
                            }
                        }
                    }
                }
            }
        }
        } // Column (keyframes + tracks)
    }

    Connections {
        target: EditorState
        function onPlayheadSecondsChanged() {
            if (EditorState.playing)
                root.ensurePlayheadVisible()
        }
        // A drag abandoned outside the timeline never fires onExited/onDropped
        // on any DropArea here, so the landing outline used to stay painted.
        function onDraggingAssetIndexChanged() {
            if (EditorState.draggingAssetIndex < 0)
                root.clearLandingPreview()
        }
    }

    ThemedDialog {
        id: bookmarkRenameDialog
        title: qsTr("Rename bookmark")
        acceptText: qsTr("Rename")
        preferredWidth: Theme.dialogWidthSm

        contentItem: Column {
            width: parent ? parent.width : Theme.dialogWidthSm
            spacing: Theme.spacingMd

            ThemedLabel {
                width: parent.width
                text: qsTr("Label")
                size: "sm"
            }
            ThemedTextField {
                id: bookmarkRenameField
                width: parent.width
                placeholderText: qsTr("Bookmark name")
                // Focus the field once the dialog is up so typing starts immediately.
                Component.onCompleted: Qt.callLater(function() {
                    if (bookmarkRenameDialog.visible)
                        forceActiveFocus()
                })
            }
        }

        onOpened: {
            bookmarkRenameField.forceActiveFocus()
            bookmarkRenameField.selectAll()
        }
        onAccepted: {
            if (bookmarkRow.renameIndex < 0)
                return
            const bookmarks = EditorState.bookmarks
            if (bookmarkRow.renameIndex >= bookmarks.length)
                return
            const seconds = bookmarks[bookmarkRow.renameIndex].seconds
            const label = bookmarkRenameField.text.trim()
            EditorState.updateBookmark(bookmarkRow.renameIndex, seconds,
                                       label.length > 0 ? label : qsTr("Bookmark"))
            bookmarkRow.renameIndex = -1
        }
        onRejected: bookmarkRow.renameIndex = -1
    }

    ThemedDialog {
        id: clipRenameDialog
        title: qsTr("Rename clip")
        acceptText: qsTr("Rename")
        preferredWidth: Theme.dialogWidthSm

        contentItem: Column {
            width: parent ? parent.width : Theme.dialogWidthSm
            spacing: Theme.spacingMd

            ThemedLabel {
                width: parent.width
                text: qsTr("Name")
                size: "sm"
            }
            ThemedTextField {
                id: clipRenameField
                width: parent.width
                placeholderText: qsTr("Clip name")
            }
        }

        onOpened: {
            clipRenameField.forceActiveFocus()
            clipRenameField.selectAll()
        }
        onAccepted: {
            if (root.renameClipTrack < 0 || root.renameClipIndex < 0)
                return
            const label = clipRenameField.text.trim()
            if (label.length > 0)
                EditorState.setClipName(root.renameClipTrack, root.renameClipIndex, label)
            root.renameClipTrack = -1
            root.renameClipIndex = -1
        }
        onRejected: {
            root.renameClipTrack = -1
            root.renameClipIndex = -1
        }
    }
}
