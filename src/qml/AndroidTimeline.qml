import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Window
import Drift
import "components"
import "components/timeline"

// CapCut-style phone timeline: multi-track, seek, select/move/trim/blade, pinch zoom,
// plus the keyframe, subtitle-cue and beat lanes and transition chrome.
// Omits the marquee and library drops — neither has a touch gesture; box-select is
// replaced by the long-press multi-select mode below.
// Edit tools live in AndroidEditActions (above the bottom rail), not on this panel.
Item {
    id: root

    // The window is edge-to-edge, and in landscape the system nav bar lands on one
    // side of this pane — over the track headers on one rotation and over the
    // scrollbar on the other. Read off root: insetting the content Column would move
    // it clear of the unsafe area and zero the very margins that put it there.
    readonly property real leftInset: SafeArea.margins.left
    readonly property real rightInset: SafeArea.margins.right

    property real zoom: 1.0
    property string timelineTool: ""
    property real cutHoverSeconds: -1
    property int cutHoverTrack: -1
    property int cutHoverClip: -1

    // Keyframe lane: opened from the lane bar, and only offered when the selected
    // clip has something animated. KeyframeGraph gates itself on propertiesTab, so
    // the collapse goes through that rather than through `visible` — otherwise the
    // lane would keep its height while folded away.
    property bool keyframeLaneOpen: false

    // Touch replacement for marquee + shift-click: armed from a clip's long-press
    // menu, after which a tap adds or removes instead of replacing the selection.
    property bool multiSelectActive: false
    // True between beginPlayheadSeek and endPlayheadSeek when playback was
    // interrupted so a tap or drag could land, and should resume on release.
    property bool resumePlaybackAfterSeek: false

    function toggleInSelection(trackIndex, clipIndex) {
        const existing = EditorState.selection
        const next = []
        var found = false
        for (var i = 0; i < existing.length; i++) {
            if (existing[i].track === trackIndex && existing[i].clip === clipIndex) {
                found = true
                continue
            }
            next.push({ "track": existing[i].track, "clip": existing[i].clip })
        }
        if (found)
            EditorState.setSelection(next)
        else
            EditorState.addToSelection(trackIndex, clipIndex)
    }

    signal openMediaRequested()
    signal openPropertiesRequested()

    function openClipProperties() {
        openPropertiesRequested()
    }

    property int renameClipTrack: -1
    property int renameClipIndex: -1
    property int savePresetTrack: -1
    property int savePresetClip: -1

    // TimelineClipItem's context menu calls this on its `panel`, so the touch timeline
    // has to answer it exactly as TimelinePanel does or "Rename…" is a TypeError.
    function requestRenameClip(trackIndex, clipIndex) {
        if (trackIndex < 0 || clipIndex < 0 || trackIndex >= root.tracks.length)
            return
        const clips = root.tracks[trackIndex].clips || []
        if (clipIndex >= clips.length)
            return
        root.renameClipTrack = trackIndex
        root.renameClipIndex = clipIndex
        clipRenameField.text = clips[clipIndex].name || ""
        clipRenameDialog.open()
    }

    function requestSaveEffectPreset(trackIndex, clipIndex) {
        if (trackIndex < 0 || clipIndex < 0)
            return
        root.savePresetTrack = trackIndex
        root.savePresetClip = clipIndex
        const clips = root.tracks[trackIndex].clips || []
        const name = clipIndex < clips.length ? (clips[clipIndex].name || "") : ""
        effectPresetNameDialog.openWith(qsTr("Save effect preset"), name)
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

    ThemedDialog {
        id: bookmarkRenameDialog
        title: qsTr("Rename bookmark")
        acceptText: qsTr("Rename")
        preferredWidth: Theme.dialogWidthSm

        property int bookmarkIndex: -1

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
            }
        }

        onOpened: {
            bookmarkRenameField.forceActiveFocus()
            bookmarkRenameField.selectAll()
        }
        onAccepted: {
            const marks = EditorState.bookmarks
            if (bookmarkRenameDialog.bookmarkIndex < 0
                    || bookmarkRenameDialog.bookmarkIndex >= marks.length)
                return
            const label = bookmarkRenameField.text.trim()
            EditorState.updateBookmark(bookmarkRenameDialog.bookmarkIndex,
                                       marks[bookmarkRenameDialog.bookmarkIndex].seconds,
                                       label.length > 0 ? label : qsTr("Bookmark"))
            bookmarkRenameDialog.bookmarkIndex = -1
        }
        onRejected: bookmarkRenameDialog.bookmarkIndex = -1
    }

    NameDialog {
        id: effectPresetNameDialog
        placeholder: qsTr("My look")
        onSubmitted: function(name) {
            if (root.savePresetTrack < 0 || root.savePresetClip < 0)
                return
            EditorState.saveClipEffectsAsPreset(root.savePresetTrack, root.savePresetClip, name)
            root.savePresetTrack = -1
            root.savePresetClip = -1
        }
        onRejected: {
            root.savePresetTrack = -1
            root.savePresetClip = -1
        }
    }

    onTimelineToolChanged: if (timelineTool === "") {
        cutHoverSeconds = -1
        cutHoverTrack = -1
        cutHoverClip = -1
    }

    // Matches TimelinePanel's floor. 0.05 was 2.5 px/second: a ten-minute project was
    // 1500px against a ~330px viewport with no way to see it end to end.
    readonly property real minZoom: 0.0001
    readonly property real maxZoom: 40.0
    readonly property real pxPerSecond: Theme.pixelsPerSecondBase * zoom
    // Trailing runway after the last clip: a constant strip of viewport, not a fixed
    // number of seconds, so it does not become 10000px of dead scroll when zoomed in.
    readonly property real timelineEndPadPx: Math.max(
        Theme.timelineEndPadMinPx, flick.width * Theme.timelineEndPadFraction)

    // Keep `anchorSeconds` glued to the same viewport X across the scale change.
    // Zoom buttons used to assign root.zoom directly, so the view expanded from the
    // content origin and whatever was on screen slid away to the right.
    function setZoomAround(newZoom, anchorSeconds, viewportX) {
        const z = Math.max(minZoom, Math.min(maxZoom, newZoom))
        if (z === zoom)
            return
        contentXAnimation.stop()
        zoom = z
        const newMaxX = Math.max(0, flick.contentWidth - flick.width)
        flick.contentX = Math.max(0, Math.min(newMaxX,
                                              anchorSeconds * pxPerSecond - viewportX))
        filmstripRefreshEpoch++
    }

    // Zoom buttons: hold the playhead steady.
    function setZoom(newZoom) {
        const anchorSeconds = Math.max(0, EditorState.playheadSeconds)
        const viewportX = anchorSeconds * pxPerSecond - flick.contentX
        setZoomAround(newZoom, anchorSeconds, viewportX)
    }

    function fitZoom() {
        contentXAnimation.stop()
        if (!(EditorState.durationSeconds > 0)) {
            zoom = 1.0
            flick.contentX = 0
            filmstripRefreshEpoch++
            return
        }
        const usable = Math.max(Math.max(flick.width, 1) - timelineEndPadPx, 1)
        const fit = usable / (EditorState.durationSeconds * Theme.pixelsPerSecondBase)
        zoom = Math.max(minZoom, Math.min(maxZoom, fit))
        flick.contentX = 0
        filmstripRefreshEpoch++
    }
    readonly property real labelsWidth: Theme.androidTrackLabelsWidth

    // Beat analysis is triggered from AndroidEditActions (the music button, which sets
    // beatGridVisible / onsetsVisible and calls analyzeBeats over the whole timeline);
    // this panel only draws what came back.
    readonly property var beatData: EditorState.beatAnalysis
    readonly property bool beatGridOn: EditorState.beatGridVisible && !!beatData
                                       && !!beatData.beats && beatData.beats.length > 0
    readonly property bool onsetsOn: EditorState.onsetsVisible && !!beatData
                                     && !!beatData.onsets && beatData.onsets.length > 0
    readonly property bool beatLaneVisible: beatGridOn || onsetsOn
    readonly property real beatLaneHeight: beatLaneVisible ? 14 : 0

    // An onset this close to a grid line is already drawn as one.
    function nearAnyBeat(seconds) {
        const list = beatData ? beatData.beats : null
        if (!list)
            return false
        for (var i = 0; i < list.length; i++) {
            if (Math.abs(list[i] - seconds) < 0.06)
                return true
        }
        return false
    }

    // Both beat canvases are viewport-sized and shifted by contentX rather than sized
    // to the content: at 40x zoom a content-wide Canvas is past GL_MAX_TEXTURE_SIZE and
    // gets silently downsampled. `alpha` separates the marker lane from the fainter
    // grid drawn down the tracks.
    function paintBeatMarks(ctx, w, h, alpha) {
        ctx.clearRect(0, 0, w, h)
        const a = root.beatData
        if (!a)
            return
        const localX = (t) => Math.round(t * root.pxPerSecond - flick.contentX)
        ctx.globalAlpha = alpha
        if (root.beatGridOn) {
            const perBar = a.beatsPerBar || 4
            const first = a.firstDownbeat || 0
            for (var i = 0; i < a.beats.length; i++) {
                const px = localX(a.beats[i])
                if (px < -2 || px > w + 2)
                    continue
                const isBar = ((i - first) % perBar + perBar) % perBar === 0
                ctx.fillStyle = String(isBar ? Theme.beatBarColor : Theme.beatGridColor)
                ctx.fillRect(px, isBar ? 0 : h * 0.35, isBar ? 2 : 1, isBar ? h : h * 0.65)
            }
        }
        if (root.onsetsOn) {
            ctx.fillStyle = String(Theme.beatOnsetColor)
            const onsets = a.onsets || []
            for (var j = 0; j < onsets.length; j++) {
                if (root.beatGridOn && root.nearAnyBeat(onsets[j].seconds))
                    continue
                const ox = localX(onsets[j].seconds)
                if (ox < -2 || ox > w + 2)
                    continue
                const oh = Math.max(4, onsets[j].strength * h * 0.5)
                ctx.fillRect(ox - 1, (h - oh) / 2, 2, oh)
            }
        }
        ctx.globalAlpha = 1
    }

    // Ruler, bookmark lane and (when analysis is showing) the beat marker lane. Also
    // the playhead's scrub target and, in its left corner, the add-track button — so it
    // is deliberately taller than the desktop ruler.
    readonly property real seekHeaderHeight:
        Theme.timelineRulerHeight + Theme.timelineBookmarkRowHeight + beatLaneHeight
    // Exposed for clip filmstrip viewport culling (Flickable id is local).
    readonly property real timelineViewX: flick.contentX
    readonly property real timelineViewW: flick.width
    // Bumped when pinch-zoom ends so ClipFilmstrip rebinds Images after the gesture.
    property int filmstripRefreshEpoch: 0

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

    function formatTime(seconds) {
        const total = Math.max(0, Math.round(seconds))
        const m = Math.floor(total / 60)
        const s = total % 60
        return m.toString().padStart(2, "0") + ":" + s.toString().padStart(2, "0")
    }

    readonly property var tracks: EditorState.tracks

    property real snapGuideSeconds: -1
    // The guide is where the panel already records that a drag is held by a snap target, and it
    // carries which target — exactly the identity the latch needs. Every drag that snaps sets it:
    // the library drop, and the clip move through showLandingPreview. One handler covers both.
    onSnapGuideSecondsChanged: Haptics.snap(snapGuideSeconds)

    property int dropTrackIndex: -1
    // A clip crossing into another track is a discrete choice changing under the finger, and at
    // phone track heights the landing outline jumping one row is easy to miss.
    onDropTrackIndexChanged: Haptics.lane(dropTrackIndex)
    property real dropStartSeconds: 0
    property real dropDurationSeconds: 0
    property bool dropCreatesNewTrack: false
    property int dropNewTrackIndex: 0
    property int effectDropTrackIndex: -1
    property int effectDropClipIndex: -1

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

    readonly property real timelineViewY: flick.contentY
    readonly property real timelineViewH: flick.height

    // Set while a clip drag owns the gesture, so the Flickable cannot steal it. Autoscroll
    // still moves the view — it writes contentX/contentY directly, which an inactive
    // Flickable honours.
    property bool scrollLocked: false
    function setScrollLocked(locked) { scrollLocked = locked }

    // Edge autoscroll for an in-progress clip drag. The timeline pane is barely two
    // track rows tall and a few seconds wide on a phone, so without this a clip could
    // only ever be moved to a track and a time already on screen — the drag simply
    // stopped at the viewport edge. Returns the delta actually applied so the caller
    // can advance the dragged clip by the same amount; scrolling alone would only
    // slide it out of view, because MouseArea rewrites the drag target's position on
    // move events and none arrive while the finger is parked at the edge.
    function dragEdgeScroll(dx, dy) {
        // ensurePlayheadVisible's animation would otherwise fight these ticks.
        contentXAnimation.stop()
        const maxX = Math.max(0, flick.contentWidth - flick.width)
        const maxY = Math.max(0, flick.contentHeight - flick.height)
        const nx = Math.max(0, Math.min(maxX, flick.contentX + dx))
        const ny = Math.max(0, Math.min(maxY, flick.contentY + dy))
        const applied = { "x": nx - flick.contentX, "y": ny - flick.contentY }
        flick.contentX = nx
        flick.contentY = ny
        return applied
    }

    function showLandingPreview(trackIndex, desiredStart, duration) {
        const snapped = snapClipStart(desiredStart, duration)
        dropTrackIndex = trackIndex
        dropStartSeconds = snapped.start
        dropDurationSeconds = duration
        snapGuideSeconds = snapped.guide
    }
    function clearLandingOutline() {
        dropTrackIndex = -1
        snapGuideSeconds = -1
    }
    function clearLandingPreview() {
        clearLandingOutline()
        dropCreatesNewTrack = false
    }

    // Feedback for a seek that is being dragged rather than dropped. snapTime counts the playhead
    // among its own targets, so a scrub within snapping distance of where it started snaps to
    // itself; without the second test that reads as a snap and every scrub would tick once against
    // its own starting position before it had passed anything.
    function reportSeekSnap(rawSeconds, previousSeconds) {
        const snapped = EditorState.snapTime(rawSeconds)
        const took = Math.abs(snapped - rawSeconds) > 0.0005
                     && Math.abs(snapped - previousSeconds) > 0.0005
        Haptics.snap(took ? snapped : -1)
        return snapped
    }

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

    // --- Lift-and-drop landing resolution -------------------------------------
    // Mirrors TimelinePanel's drop maths, driven by TouchDrag instead of a
    // platform DropArea: on a phone the library is a modal sheet over this panel,
    // so there is no drag the compositor can route here.

    function clearEffectDropHighlight() {
        effectDropTrackIndex = -1
        effectDropClipIndex = -1
    }

    function updateEffectDropHighlight(trackIndex, xPixels) {
        const clipIndex = clipIndexAtPosition(trackIndex, xPixels)
        effectDropTrackIndex = clipIndex >= 0 ? trackIndex : -1
        effectDropClipIndex = clipIndex
    }

    function assetDurationSeconds(assetIndex) {
        const asset = AssetLibrary.assetAt(assetIndex)
        if (!asset)
            return 5.0
        if (asset.kind === "image" || !(asset.durationSeconds > 0))
            return 5.0
        return asset.durationSeconds
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

    // Depth of the "insert a new track here" band on a track row.
    function newTrackEdge(index) {
        return Math.min(Theme.newTrackHitSlop, trackHeight(index) / 4)
    }

    // Resolve a track-column y into a drop target. Returns
    // { newTrack: false, track: i } to land on an existing track, or
    // { newTrack: true, insertIndex: n } to create one at that boundary — the
    // top/bottom band of a row means "new track above/below it", which is what
    // makes a lane under the last track reachable at all.
    function assetDropTargetAtY(assetIndex, y) {
        const count = tracks.length
        if (count === 0)
            return { "newTrack": true, "insertIndex": 0 }

        var cursor = 0
        for (var i = 0; i < count; i++) {
            const h = trackHeight(i)
            const rowEnd = cursor + h
            if (y < rowEnd + Theme.trackGap / 2) {
                if (!EditorState.trackAcceptsAsset(i, assetIndex))
                    return { "newTrack": true,
                             "insertIndex": y < cursor + h / 2 ? i : i + 1 }
                const edge = newTrackEdge(i)
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
    // coordinates — where the ghost lane is drawn.
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
        if (assetIndex < 0)
            return
        const atSeconds = Math.max(0, dropX / pxPerSecond)

        function runAdd() {
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

    // Outgoing (earlier) clip of the boundary a transition dropped at this x
    // would bridge.
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

    // --- TouchDrag drop target ------------------------------------------------

    Component.onCompleted: TouchDrag.dropTarget = root
    Component.onDestruction: if (TouchDrag.dropTarget === root) TouchDrag.dropTarget = null

    // Scene point → { inside, x (content px), y (track-column px), viewX, viewY }.
    // The seek strip is pinned to the top of the viewport, so anything under it is
    // ruler, not track, however far the tracks have been scrolled.
    function touchDropPoint(sceneX, sceneY) {
        const p = flick.mapFromItem(null, sceneX, sceneY)
        const inside = p.x >= 0 && p.x <= flick.width
                       && p.y >= root.seekHeaderHeight && p.y <= flick.height
        return { "inside": inside,
                 "x": p.x + flick.contentX,
                 "y": p.y + flick.contentY - root.seekHeaderHeight,
                 "viewX": p.x,
                 "viewY": p.y }
    }

    function clearTouchDrop() {
        clearLandingPreview()
        clearEffectDropHighlight()
        dropEdgeScroll.stop()
    }

    // Returns true while the finger is somewhere this panel would accept, so the
    // ghost can show it is about to land rather than about to be thrown away.
    function updateTouchDrop(kind, payload, sceneX, sceneY) {
        const pt = touchDropPoint(sceneX, sceneY)
        if (!pt.inside) {
            clearTouchDrop()
            return false
        }

        dropEdgeScroll.viewX = pt.viewX
        dropEdgeScroll.viewY = pt.viewY
        dropEdgeScroll.start()

        if (kind === "media") {
            clearEffectDropHighlight()
            updateAssetDropPreview(payload, pt.x, pt.y)
            return true
        }

        clearLandingPreview()
        const trackIdx = trackIndexAtY(pt.y)
        if (trackIdx < 0) {
            clearEffectDropHighlight()
            return false
        }
        if (kind === "transition") {
            const leftClip = transitionLeftClipAtPosition(trackIdx, Math.max(0, pt.x))
            effectDropTrackIndex = leftClip >= 0 ? trackIdx : -1
            effectDropClipIndex = leftClip
            return leftClip >= 0
        }
        updateEffectDropHighlight(trackIdx, Math.max(0, pt.x))
        return effectDropClipIndex >= 0
    }

    function performTouchDrop(kind, payload, sceneX, sceneY) {
        const pt = touchDropPoint(sceneX, sceneY)
        clearTouchDrop()
        if (!pt.inside)
            return

        if (kind === "media") {
            performAssetDrop(payload, pt.x, pt.y)
            return
        }

        const trackIdx = trackIndexAtY(pt.y)
        const clipX = Math.max(0, pt.x)

        if (kind === "transition") {
            const leftClip = trackIdx >= 0
                             ? transitionLeftClipAtPosition(trackIdx, clipX) : -1
            if (leftClip < 0) {
                Toasts.info(qsTr("Drop a transition where two clips meet."))
                return
            }
            EditorState.addTransition(trackIdx, leftClip, payload, 0.5)
            return
        }

        const clipIdx = trackIdx >= 0 ? clipIndexAtPosition(trackIdx, clipX) : -1
        if (clipIdx < 0) {
            Toasts.info(qsTr("Drop that onto a clip to apply it."))
            return
        }
        if (kind === "effect")
            EditorState.addEffect(trackIdx, clipIdx, payload)
        else if (kind === "audioEffect")
            EditorState.addAudioEffect(trackIdx, clipIdx, payload)
        else if (kind === "template")
            EditorState.applyEffectTemplate(trackIdx, clipIdx, payload)
        EditorState.selectClip(trackIdx, clipIdx)
    }

    // The pane is a couple of track rows tall and a few seconds wide, so without
    // this a lifted card could only ever be dropped on a track and a time that
    // already happened to be on screen.
    Timer {
        id: dropEdgeScroll
        interval: 16
        repeat: true
        running: false
        property real viewX: 0
        property real viewY: 0
        readonly property real band: 48
        readonly property real speed: 12

        onTriggered: {
            if (!TouchDrag.active) {
                stop()
                return
            }
            var dx = 0
            var dy = 0
            if (viewX < band)
                dx = -speed * (1 - viewX / band)
            else if (viewX > flick.width - band)
                dx = speed * (1 - (flick.width - viewX) / band)
            if (viewY < root.seekHeaderHeight + band)
                dy = -speed * (1 - (viewY - root.seekHeaderHeight) / band)
            else if (viewY > flick.height - band)
                dy = speed * (1 - (flick.height - viewY) / band)
            if (dx === 0 && dy === 0)
                return
            const applied = root.dragEdgeScroll(dx, dy)
            if (applied.x === 0 && applied.y === 0)
                return
            // Re-resolve against the view that just moved, or the landing outline
            // would stay pinned to the content the finger has scrolled away from.
            root.updateTouchDrop(TouchDrag.kind, TouchDrag.payload,
                                 TouchDrag.sceneX, TouchDrag.sceneY)
        }
    }

    function trackOffsetY(index) {
        var cursor = 0
        for (var i = 0; i < index && i < tracks.length; i++)
            cursor += trackHeight(i) + Theme.trackGap
        return cursor
    }

    function trackBaseHeight(type) {
        if (type === "video") return Theme.trackHeightVideo
        if (type === "audio") return Theme.trackHeightAudio
        if (type === "shape") return Theme.trackHeightShape
        if (type === "subtitle") return Theme.trackHeightSubtitle
        return Theme.trackHeightText
    }

    function trackHeight(index) {
        if (index < 0 || index >= tracks.length)
            return Theme.trackHeightVideo
        const track = tracks[index]
        const scale = track.heightScale > 0 ? track.heightScale : 1
        return Math.round(Math.max(20, trackBaseHeight(track.type) * scale))
    }

    function clipColor(type) {
        if (type === "text") return Theme.clipText
        if (type === "subtitle") return Theme.clipSubtitle
        if (type === "audio") return Theme.clipAudio
        if (type === "graphic") return Theme.clipGraphic
        if (type === "effect" || type === "adjustment") return Theme.clipEffect
        return Theme.clipVideoPlaceholder
    }

    function totalTracksHeight() {
        var h = 0
        for (var i = 0; i < tracks.length; i++) {
            h += trackHeight(i)
            if (i > 0) h += Theme.trackGap
        }
        return h
    }

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

    // Empty stretches on a track, sorted left-to-right. Gaps are never stored —
    // they are whatever time is not covered by a clip.
    function gapsForTrack(trackIndex) {
        if (trackIndex < 0 || trackIndex >= tracks.length)
            return []
        const sorted = tracks[trackIndex].clips.slice().sort(function (a, b) {
            return a.start - b.start
        })
        const gaps = []
        for (var i = 0; i < sorted.length - 1; i++) {
            const gapStart = sorted[i].start + sorted[i].duration
            const gapEnd = sorted[i + 1].start
            if (gapEnd > gapStart)
                gaps.push({ start: gapStart, end: gapEnd })
        }
        return gaps
    }

    function trackIndexAtY(y) {
        var cursor = 0
        for (var i = 0; i < tracks.length; i++) {
            const th = trackHeight(i)
            if (y >= cursor && y < cursor + th)
                return i
            cursor += th + Theme.trackGap
        }
        return -1
    }

    // Seeking while the engine clock is running lands ahead of the tap: the
    // sink's processedUSecs is cumulative from play(), so the visible playhead
    // becomes tapTime + elapsed. Pause for the gesture and resume on release.
    function beginPlayheadSeek() {
        if (!EditorState.playing)
            return
        resumePlaybackAfterSeek = true
        EditorState.playing = false
    }

    function endPlayheadSeek() {
        if (!resumePlaybackAfterSeek)
            return
        resumePlaybackAfterSeek = false
        EditorState.playing = true
    }

    function ensurePlayheadVisible() {
        const playheadX = EditorState.playheadSeconds * pxPerSecond
        const margin = 64
        var target = -1
        if (playheadX < flick.contentX + margin)
            target = Math.max(0, playheadX - margin)
        else if (playheadX > flick.contentX + flick.width - margin)
            target = Math.min(Math.max(0, flick.contentWidth - flick.width),
                              playheadX - flick.width + margin)
        if (target < 0)
            return
        if (EditorState.playing || flick.dragging || flick.flicking) {
            flick.contentX = target
            return
        }
        scrollToX(target)
    }

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
        easing.type: Theme.easing
    }

    Column {
        anchors.fill: parent
        anchors.leftMargin: root.leftInset
        anchors.rightMargin: root.rightInset

        // Lane bar: the keyframe lane's collapse toggle, and — while multi-select is
        // armed — the controls that gesture has no other home for.
        Item {
            id: laneBar
            width: parent.width
            visible: root.multiSelectActive || keyframeLane.allSeries.length > 0
            height: visible ? 32 : 0

            Rectangle {
                anchors.fill: parent
                color: Theme.panelBackground
            }
            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 1
                color: Theme.panelBorder
            }

            Row {
                anchors.left: parent.left
                anchors.leftMargin: Theme.spacingLg
                anchors.right: parent.right
                anchors.rightMargin: Theme.spacingLg
                anchors.verticalCenter: parent.verticalCenter
                spacing: Theme.spacingSm
                visible: !root.multiSelectActive

                ThemedChip {
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Keyframes")
                    chipHeight: 26
                    selected: root.keyframeLaneOpen
                    onClicked: root.keyframeLaneOpen = !root.keyframeLaneOpen
                }
            }

            Row {
                anchors.left: parent.left
                anchors.leftMargin: Theme.spacingLg
                anchors.right: parent.right
                anchors.rightMargin: Theme.spacingLg
                anchors.verticalCenter: parent.verticalCenter
                spacing: Theme.spacingSm
                visible: root.multiSelectActive

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("%n clip(s)", "", EditorState.selection.length)
                    color: Theme.panelForeground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeXs
                }
                ThemedChip {
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("All")
                    chipHeight: 26
                    variant: "outline"
                    onClicked: EditorState.selectAllClips()
                }
                ThemedChip {
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("None")
                    chipHeight: 26
                    variant: "outline"
                    onClicked: EditorState.clearSelection()
                }
                ThemedChip {
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Done")
                    chipHeight: 26
                    selected: true
                    onClicked: root.multiSelectActive = false
                }
            }
        }

        // Both lanes size themselves to zero when they have nothing to show, so the
        // track area only loses height while one is actually open.
        KeyframeGraph {
            id: keyframeLane
            width: parent.width
            pxPerSecond: root.pxPerSecond
            labelsWidth: root.labelsWidth
            propertiesTab: root.keyframeLaneOpen ? "transform" : ""
            contentX: flick.contentX
            contentWidth: flick.contentWidth
            // Tangent grips are only armed above 140px, so on a phone the lane opens
            // tall enough to shape a curve rather than at the desktop's 88px overview —
            // but a flat 140px floor outranked the 50%-of-pane budget and left the track
            // area at zero (negative, with the subtitle lane also open) on a short pane.
            // Below the grip threshold the lane shrinks instead of eating the tracks.
            laneHeight: Math.round(Math.max(keyframeLane.minLaneHeight,
                                            Math.min(220, root.height * 0.5,
                                                     root.height - laneBar.height
                                                     - subtitleLane.height
                                                     - root.seekHeaderHeight
                                                     - Theme.trackHeightAudio)))
        }

        SubtitleCueLane {
            id: subtitleLane
            width: parent.width
            pxPerSecond: root.pxPerSecond
            labelsWidth: root.labelsWidth
            contentX: flick.contentX
            contentWidth: flick.contentWidth
        }

        Row {
            width: parent.width
            height: Math.max(0, parent.height - laneBar.height - keyframeLane.height
                                - subtitleLane.height)

            Column {
                width: root.labelsWidth
                height: parent.height

                Item {
                    width: parent.width
                    height: root.seekHeaderHeight

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

                    // Desktop puts the add-track button in this exact corner; on the
                    // phone it was an empty box, leaving no way to start a text, audio
                    // or shape track that a dropped asset does not create for you.
                    IconButton {
                        id: addTrackButton
                        anchors.centerIn: parent
                        buttonSize: Math.min(parent.height, Theme.androidIconButtonSize)
                        iconSize: Theme.iconSizeMd
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
                }

                TrackHeaderColumn {
                    width: parent.width
                    height: Math.max(0, parent.height - root.seekHeaderHeight)
                    tracks: root.tracks
                    contentY: flick.contentY
                    labelsWidth: root.labelsWidth
                    compact: true
                    touchMode: true
                }
            }

            Flickable {
                id: flick
                width: parent.width - root.labelsWidth
                height: parent.height
                contentWidth: Math.max(width, EditorState.durationSeconds * root.pxPerSecond
                                              + root.timelineEndPadPx)
                contentHeight: Math.max(height,
                                        root.seekHeaderHeight + root.totalTracksHeight() + Theme.trackGap)
                clip: true
                interactive: !root.scrollLocked
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.horizontal: AppScrollBar { policy: ScrollBar.AsNeeded }
                ScrollBar.vertical: AppScrollBar { policy: ScrollBar.AsNeeded }

                PinchHandler {
                    id: pinch
                    target: null
                    property real startZoom: 1
                    property real startContentX: 0
                    property real centroidViewportX: 0
                    // Latched, not just rate limited: fingers held spread past the end of the range
                    // keep delivering scale events, and a limiter alone would turn the end of the
                    // zoom into a continuous buzz instead of the single bump it should be.
                    property bool atZoomLimit: false

                    onActiveChanged: {
                        atZoomLimit = false
                        if (active) {
                            startZoom = root.zoom
                            startContentX = flick.contentX
                            // centroid.position is in the handler's parent coordinates,
                            // which for a handler declared inside a Flickable is the
                            // *content* item — already shifted by contentX. Mapping the
                            // scene position into the Flickable itself is independent of
                            // that reparenting and gives a true viewport offset; using
                            // centroid.position directly added contentX a second time and
                            // made the content jump sideways on every pinch.
                            centroidViewportX =
                                flick.mapFromItem(null, centroid.scenePosition).x
                        } else {
                            // Pinch stops dirtying the scene graph; force filmstrip Images
                            // to rebind so Android/ANGLE does not leave blank tiles.
                            root.filmstripRefreshEpoch++
                        }
                    }
                    onScaleChanged: {
                        if (!active)
                            return
                        // activeScale, not scale: `scale` is persistentScale, which keeps
                        // accumulating across gestures, so the second pinch started from
                        // the first one's total and multiplied the zoom twice over.
                        const factor = activeScale
                        const wanted = startZoom * factor
                        const next = Math.max(root.minZoom, Math.min(root.maxZoom, wanted))
                        // The pinch continuing while the timeline has stopped scaling is the only
                        // sign the range has run out; the content simply holds still.
                        const limited = Math.abs(next - wanted) > 1e-4
                        if (limited && !pinch.atZoomLimit)
                            Haptics.boundary()
                        pinch.atZoomLimit = limited
                        if (next === root.zoom)
                            return
                        const t = (startContentX + centroidViewportX)
                                  / (Theme.pixelsPerSecondBase * startZoom)
                        root.zoom = next
                        const newMaxX = Math.max(0, flick.contentWidth - flick.width)
                        flick.contentX = Math.max(0, Math.min(newMaxX,
                                                              t * root.pxPerSecond - centroidViewportX))
                    }
                }

                Item {
                    id: timelineContent
                    width: flick.contentWidth
                    height: flick.contentHeight

                    Item {
                        id: seekStrip
                        width: parent.width
                        y: flick.contentY
                        z: 2
                        height: root.seekHeaderHeight

                        Rectangle {
                            anchors.fill: parent
                            color: Theme.panelBackground
                        }
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
                            preventStealing: true
                            z: 1

                            function scrubTo(x) {
                                const raw = Math.max(0, x) / root.pxPerSecond
                                EditorState.playheadSeconds =
                                    root.reportSeekSnap(raw, EditorState.playheadSeconds)
                            }
                            onPressed: (mouse) => {
                                // A previous gesture that ended on a snap would otherwise leave the
                                // latch engaged and swallow this one's first tick.
                                Haptics.reset()
                                root.beginPlayheadSeek()
                                scrubTo(mouse.x)
                            }
                            onPositionChanged: (mouse) => {
                                if (pressed)
                                    scrubTo(mouse.x)
                            }
                            onReleased: {
                                Haptics.reset()
                                root.endPlayheadSeek()
                            }
                            onCanceled: {
                                Haptics.reset()
                                root.endPlayheadSeek()
                            }
                        }

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

                        // Bookmark lane. Tap a flag to jump; hold it for the menu desktop
                        // gets from the right button. Retiming is "Move to playhead"
                        // rather than a drag: this strip has already promised the drag to
                        // the scrubber, and a 10px flag is not a drag target on touch.
                        // Sits above rulerScrub with preventStealing so a tap on a flag is
                        // not read as a seek.
                        Item {
                            id: bookmarkRow
                            y: Theme.timelineRulerHeight
                            width: parent.width
                            height: Theme.timelineBookmarkRowHeight
                            z: 2

                            Rectangle {
                                visible: EditorState.workAreaActive
                                y: 0
                                height: parent.height
                                x: EditorState.workAreaInSeconds * root.pxPerSecond
                                width: Math.max(0, (EditorState.workAreaOutSeconds
                                                    - EditorState.workAreaInSeconds)
                                             * root.pxPerSecond)
                                color: Qt.rgba(Theme.primary.r, Theme.primary.g, Theme.primary.b, 0.18)
                            }

                            ThemedContextMenu {
                                id: bookmarkContextMenu
                                property int bookmarkIndex: -1
                                property string bookmarkLabel: ""

                                ThemedMenuItem {
                                    text: qsTr("Go to bookmark")
                                    icon.name: Theme.icons.bookmark
                                    onTriggered: EditorState.goToBookmark(bookmarkContextMenu.bookmarkIndex)
                                }
                                ThemedMenuItem {
                                    text: qsTr("Move to playhead")
                                    icon.name: Theme.icons.moveHorizontal
                                    onTriggered: EditorState.updateBookmark(
                                                     bookmarkContextMenu.bookmarkIndex,
                                                     EditorState.playheadSeconds,
                                                     bookmarkContextMenu.bookmarkLabel)
                                }
                                ThemedMenuItem {
                                    text: qsTr("Rename…")
                                    icon.name: Theme.icons.pencil
                                    onTriggered: {
                                        bookmarkRenameDialog.bookmarkIndex = bookmarkContextMenu.bookmarkIndex
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

                            Repeater {
                                model: EditorState.bookmarks
                                delegate: Item {
                                    id: bookmarkFlag
                                    required property int index
                                    required property var modelData

                                    x: modelData.seconds * root.pxPerSecond - width / 2
                                    width: 10
                                    height: parent.height

                                    Rectangle {
                                        anchors.horizontalCenter: parent.horizontalCenter
                                        y: -6
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
                                    }

                                    // A project marked up on desktop arrived as a row of
                                    // anonymous diamonds; the label is the whole point of
                                    // a bookmark. Overflows the 10px flag deliberately.
                                    Text {
                                        x: 8
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: bookmarkFlag.modelData.label
                                        color: Theme.mutedForeground
                                        font.family: Theme.fontFamily
                                        font.pixelSize: Theme.fontSizeTiny
                                    }

                                    MouseArea {
                                        anchors.fill: parent
                                        // The flag is 10px; the target is a fingertip.
                                        anchors.margins: -14
                                        preventStealing: true
                                        pressAndHoldInterval: 400

                                        property bool held: false

                                        onPressed: held = false
                                        onPressAndHold: {
                                            held = true
                                            Haptics.pickUp()
                                            bookmarkContextMenu.bookmarkIndex = bookmarkFlag.index
                                            bookmarkContextMenu.bookmarkLabel = bookmarkFlag.modelData.label
                                            bookmarkContextMenu.popup()
                                        }
                                        onClicked: if (!held) {
                                            Haptics.select()
                                            EditorState.goToBookmark(bookmarkFlag.index)
                                        }
                                    }
                                }
                            }
                        }

                        // Beat / onset markers. Analysis itself is triggered from
                        // AndroidEditActions; this lane only appears once a result is in
                        // and the corresponding layer is switched on.
                        Canvas {
                            id: beatLane
                            x: flick.contentX
                            y: Theme.timelineRulerHeight + Theme.timelineBookmarkRowHeight
                            width: flick.width
                            height: root.beatLaneHeight
                            visible: root.beatLaneVisible
                            z: 1

                            onPaint: root.paintBeatMarks(getContext("2d"), width, height, 1.0)
                            onWidthChanged: requestPaint()
                            onHeightChanged: requestPaint()
                            onVisibleChanged: if (visible) requestPaint()

                            Connections {
                                target: root
                                function onBeatDataChanged() { beatLane.requestPaint() }
                                function onBeatGridOnChanged() { beatLane.requestPaint() }
                                function onOnsetsOnChanged() { beatLane.requestPaint() }
                                function onPxPerSecondChanged() { beatLane.requestPaint() }
                                function onTimelineViewXChanged() { beatLane.requestPaint() }
                            }
                        }
                    }

                    Column {
                        id: trackColumn
                        y: root.seekHeaderHeight
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
                                color: Qt.rgba(Theme.panelAccent.r, Theme.panelAccent.g,
                                               Theme.panelAccent.b, 0.22)

                                Repeater {
                                    model: root.tracks[trackRow.trackIndex].clips.length
                                    delegate: TimelineClipItem {
                                        panel: root
                                        timelineColumn: trackColumn
                                        trackIndex: trackRow.trackIndex
                                        touchMode: true
                                    }
                                }

                                // Long-press a gap to ripple everything after it left.
                                Repeater {
                                    model: root.gapsForTrack(trackRow.trackIndex)
                                    delegate: Item {
                                        id: gapItem
                                        required property var modelData
                                        x: modelData.start * root.pxPerSecond
                                        width: Math.max(Theme.androidClipEdgeMargin,
                                                        (modelData.end - modelData.start) * root.pxPerSecond)
                                        height: trackRow.height
                                        z: 1

                                        Rectangle {
                                            anchors.fill: parent
                                            visible: gapPress.pressed
                                            color: Qt.rgba(Theme.panelAccent.r, Theme.panelAccent.g,
                                                           Theme.panelAccent.b, 0.35)
                                        }

                                        MouseArea {
                                            id: gapPress
                                            anchors.fill: parent
                                            pressAndHoldInterval: 400
                                            onPressAndHold: gapContextMenu.popup()

                                            ThemedContextMenu {
                                                id: gapContextMenu
                                                ThemedMenuItem {
                                                    text: qsTr("Close gap")
                                                    icon.name: Theme.icons.chevronsRightLeft
                                                    onTriggered: EditorState.closeGap(trackRow.trackIndex,
                                                                                      gapItem.modelData.start)
                                                }
                                            }
                                        }
                                    }
                                }

                                // Transition chrome. Same geometry as the desktop panel,
                                // minus its DropArea — there is no drag-and-drop here, so
                                // a tap on a bare overlap creates the crossfade instead.
                                // No preventStealing: a pan across an overlap must still
                                // reach the Flickable.
                                Repeater {
                                    model: root.tracks[trackRow.trackIndex].clips.length
                                    delegate: Item {
                                        id: transitionRegion
                                        required property int index
                                        readonly property int leftClipIndex: index
                                        readonly property var leftClip:
                                            root.tracks[trackRow.trackIndex].clips[leftClipIndex]
                                        readonly property string trackType: root.tracks[trackRow.trackIndex].type
                                        // transitionBetweenClips is a plain call with no
                                        // notifier of its own, and adding a transition
                                        // leaves the clip count — the Repeater's model —
                                        // untouched, so this has to depend on tracks
                                        // explicitly or the region never repaints.
                                        readonly property var transitionData: {
                                            void EditorState.tracks
                                            return EditorState.transitionBetweenClips(
                                                       trackRow.trackIndex, leftClipIndex)
                                        }
                                        readonly property bool hasTransition:
                                            transitionData && Object.keys(transitionData).length > 0
                                        readonly property bool transitionSelected:
                                            EditorState.selectedTransitionTrack === trackRow.trackIndex
                                            && EditorState.selectedTransitionLeftClip === leftClipIndex

                                        // Nearest later clip that abuts or overlaps this one.
                                        readonly property int partnerIndex: {
                                            const clips = root.tracks[trackRow.trackIndex].clips
                                            const left = leftClip
                                            if (!left)
                                                return -1
                                            var best = -1
                                            var bestStart = 1e12
                                            for (var i = 0; i < clips.length; i++) {
                                                if (i === leftClipIndex)
                                                    continue
                                                const right = clips[i]
                                                if (right.start < left.start)
                                                    continue
                                                if (right.start - (left.start + left.duration) > 0.001)
                                                    continue
                                                if (right.start < bestStart) {
                                                    bestStart = right.start
                                                    best = i
                                                }
                                            }
                                            return best
                                        }
                                        readonly property var rightClip: partnerIndex >= 0
                                            ? root.tracks[trackRow.trackIndex].clips[partnerIndex]
                                            : null
                                        readonly property bool physicallyOverlapping:
                                            leftClip && rightClip
                                            && rightClip.start < (leftClip.start + leftClip.duration)
                                        readonly property real regionStart: {
                                            if (!leftClip || !rightClip)
                                                return 0
                                            if (hasTransition && transitionData.start !== undefined)
                                                return transitionData.start
                                            return physicallyOverlapping ? rightClip.start : 0
                                        }
                                        readonly property real regionEnd: {
                                            if (!leftClip || !rightClip)
                                                return 0
                                            if (hasTransition && transitionData.end !== undefined)
                                                return transitionData.end
                                            return physicallyOverlapping
                                                   ? leftClip.start + leftClip.duration : 0
                                        }
                                        readonly property bool showRegion:
                                            (trackType === "video" || trackType === "shape"
                                             || trackType === "text")
                                            && leftClip && rightClip
                                            && (physicallyOverlapping || hasTransition)
                                            && regionEnd > regionStart

                                        z: 10
                                        visible: showRegion
                                        x: regionStart * root.pxPerSecond
                                        // Floored to a fingertip rather than the desktop's
                                        // 8px, so a half-second crossfade is still tappable.
                                        width: Math.max(Theme.androidIconButtonSize,
                                                        (regionEnd - regionStart) * root.pxPerSecond)
                                        y: Theme.clipSelectionRingWidth
                                        height: parent.height - Theme.clipSelectionRingWidth * 2

                                        Rectangle {
                                            anchors.fill: parent
                                            radius: Theme.radiusSm
                                            color: Qt.rgba(Theme.transitionOverlap.r,
                                                           Theme.transitionOverlap.g,
                                                           Theme.transitionOverlap.b,
                                                           transitionRegion.transitionSelected ? 0.85
                                                           : (transitionRegion.hasTransition ? 0.65 : 0.35))
                                            border.width: transitionRegion.transitionSelected ? 2 : 1
                                            border.color: Theme.transitionOverlap

                                            Canvas {
                                                anchors.fill: parent
                                                anchors.margins: 1
                                                opacity: 0.35
                                                onPaint: {
                                                    const ctx = getContext("2d")
                                                    ctx.clearRect(0, 0, width, height)
                                                    ctx.strokeStyle = Theme.onMedia
                                                    ctx.lineWidth = 1
                                                    for (var x = -height; x < width; x += 6) {
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
                                        }

                                        MouseArea {
                                            anchors.fill: parent
                                            onClicked: {
                                                if (transitionRegion.hasTransition) {
                                                    Haptics.select()
                                                    EditorState.selectTransition(trackRow.trackIndex,
                                                                                 transitionRegion.leftClipIndex)
                                                } else {
                                                    Haptics.confirm()
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

                    // Beat grid down the tracks. The 14px marker lane is pinned to the top
                    // of the viewport, which is no help at all when the cut you are lining
                    // up is a hundred pixels below it.
                    Canvas {
                        id: beatGridOverlay
                        x: flick.contentX
                        y: root.seekHeaderHeight
                        width: flick.width
                        height: root.totalTracksHeight()
                        visible: root.beatLaneVisible
                        // Same z as the track column, declared after it: over the clips,
                        // but still under the seek strip, which slides down over the
                        // tracks whenever they are scrolled vertically.
                        z: 1

                        onPaint: root.paintBeatMarks(getContext("2d"), width, height, 0.35)
                        onWidthChanged: requestPaint()
                        onHeightChanged: requestPaint()
                        onVisibleChanged: if (visible) requestPaint()

                        Connections {
                            target: root
                            function onBeatDataChanged() { beatGridOverlay.requestPaint() }
                            function onBeatGridOnChanged() { beatGridOverlay.requestPaint() }
                            function onOnsetsOnChanged() { beatGridOverlay.requestPaint() }
                            function onPxPerSecondChanged() { beatGridOverlay.requestPaint() }
                            function onTimelineViewXChanged() { beatGridOverlay.requestPaint() }
                        }
                    }

                    Item {
                        id: workAreaOverlay
                        z: 1
                        y: root.seekHeaderHeight
                        width: parent.width
                        height: Math.max(root.totalTracksHeight() + Theme.trackGap,
                                         flick.height - root.seekHeaderHeight)

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

                    Rectangle {
                        visible: root.dropTrackIndex >= 0
                        x: root.dropStartSeconds * root.pxPerSecond
                        y: root.seekHeaderHeight + root.trackOffsetY(root.dropTrackIndex)
                        width: root.dropDurationSeconds * root.pxPerSecond
                        height: root.dropTrackIndex >= 0 ? root.trackHeight(root.dropTrackIndex) : 0
                        radius: Theme.radiusSm
                        color: Qt.rgba(Theme.primary.r, Theme.primary.g, Theme.primary.b, 0.12)
                        border.width: 2
                        border.color: Theme.primary
                        z: 5
                    }

                    // New-track indicator: a ghost lane straddling the boundary the
                    // track would be inserted at, drawn at real track height so it
                    // reads as a track rather than a hairline. Aiming at the top or
                    // bottom band of a row is the only way to say "put this on its
                    // own track" without a keyboard.
                    Item {
                        id: newTrackIndicator
                        visible: root.dropCreatesNewTrack
                        readonly property real laneHeight:
                            root.trackBaseHeight(EditorState.trackTypeForAsset(
                                                     EditorState.draggingAssetIndex))
                        x: 0
                        y: root.seekHeaderHeight
                           + root.newTrackBoundaryY(root.dropNewTrackIndex)
                           - laneHeight / 2
                        width: parent.width
                        height: laneHeight
                        z: 7

                        Rectangle {
                            anchors.fill: parent
                            radius: Theme.radiusSm
                            color: Qt.rgba(Theme.panelBackground.r, Theme.panelBackground.g,
                                           Theme.panelBackground.b, 0.92)
                            border.width: Theme.borderWidth
                            border.color: Theme.primary
                        }

                        // The boundary itself, so which gap the lane is going into
                        // is unambiguous.
                        Rectangle {
                            width: parent.width
                            height: 2
                            anchors.verticalCenter: parent.verticalCenter
                            color: Theme.primary
                        }

                        // Where in time the clip would land, inside the lane it would
                        // land on — the outline on an existing row is suppressed while
                        // this is up.
                        Rectangle {
                            x: root.dropStartSeconds * root.pxPerSecond
                            width: root.dropDurationSeconds * root.pxPerSecond
                            height: parent.height
                            radius: Theme.radiusSm
                            color: Qt.rgba(Theme.primary.r, Theme.primary.g, Theme.primary.b, 0.18)
                            border.width: 2
                            border.color: Theme.primary
                        }

                        // Pinned to the viewport: the lane spans the whole timeline,
                        // and a label at its centre is usually off screen.
                        Text {
                            x: flick.contentX + Theme.spacingLg
                            anchors.verticalCenter: parent.verticalCenter
                            text: qsTr("New track")
                            color: Theme.primary
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSizeXs
                            font.weight: Font.Medium
                        }
                    }

                    Rectangle {
                        visible: opacity > 0
                        opacity: root.snapGuideSeconds >= 0 ? 1 : 0
                        x: root.snapGuideSeconds * root.pxPerSecond
                        y: root.seekHeaderHeight
                        width: Theme.borderWidth
                        height: root.totalTracksHeight()
                        color: Theme.snapGuide
                        z: 6

                        Behavior on opacity {
                            NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
                        }

                        Rectangle {
                            anchors.horizontalCenter: parent.horizontalCenter
                            anchors.top: parent.top
                            anchors.topMargin: Theme.spacingSm
                            width: snapLabel.implicitWidth + Theme.spacingMd * 2
                            height: snapLabel.implicitHeight + Theme.spacingSm
                            radius: Theme.radiusSm
                            color: Theme.snapGuide

                            Text {
                                id: snapLabel
                                anchors.centerIn: parent
                                text: root.formatTime(root.snapGuideSeconds)
                                color: Theme.primaryForeground
                                font.family: Theme.monoFontFamily
                                font.pixelSize: Theme.fontSizeTick
                            }
                        }
                    }

                    Item {
                        id: playhead
                        y: 0
                        z: 3
                        width: Theme.playheadLineWidth
                        height: root.seekHeaderHeight + root.totalTracksHeight()

                        Binding {
                            target: playhead
                            property: "x"
                            value: EditorState.playheadSeconds * root.pxPerSecond
                            when: !playheadDragArea.drag.active && !playheadLineDrag.drag.active
                        }

                        onXChanged: {
                            if (!playheadDragArea.drag.active && !playheadLineDrag.drag.active)
                                return
                            const raw = playhead.x / root.pxPerSecond
                            // The drag deliberately does not snap the position — finishSeek does
                            // that on release — but the targets it passes are still worth feeling,
                            // and this is where the finger covers the edges it is passing over.
                            root.reportSeekSnap(raw, EditorState.playheadSeconds)
                            EditorState.playheadSeconds = raw
                        }

                        function finishSeek() {
                            EditorState.playheadSeconds =
                                EditorState.snapTime(playhead.x / root.pxPerSecond)
                            Haptics.reset()
                        }

                        Rectangle {
                            anchors.left: parent.left
                            y: flick.contentY + Theme.timelineRulerHeight * 0.55
                            width: Theme.playheadLineWidth
                            height: parent.height - y
                            color: Theme.primary
                        }

                        // The seek strip is pinned to the viewport (y: flick.contentY),
                        // so the head and its grab area have to travel with it. Left in
                        // content coordinates they scrolled off the top the moment the
                        // tracks were panned vertically, taking the scrub target with them.
                        Item {
                            id: playheadHandle
                            width: Theme.playheadHandleSize
                            height: Theme.playheadHandleSize + 2
                            x: -width / 2 + Theme.playheadLineWidth / 2
                            y: flick.contentY + 3

                            Rectangle {
                                anchors.top: parent.top
                                anchors.horizontalCenter: parent.horizontalCenter
                                width: parent.width
                                height: parent.height * 0.55
                                radius: 2
                                color: Theme.primary
                            }

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

                        MouseArea {
                            id: playheadDragArea
                            width: Math.max(Theme.playheadSeekGrabWidth, Theme.androidIconButtonSize)
                            height: root.seekHeaderHeight
                            x: -(width - Theme.playheadLineWidth) / 2
                            y: flick.contentY
                            preventStealing: true
                            drag.target: playhead
                            drag.axis: Drag.XAxis
                            drag.threshold: 0
                            drag.minimumX: 0
                            drag.maximumX: flick.contentWidth - Theme.playheadLineWidth
                            onPressed: root.beginPlayheadSeek()
                            onReleased: {
                                playhead.finishSeek()
                                root.endPlayheadSeek()
                            }
                            onCanceled: {
                                playhead.finishSeek()
                                root.endPlayheadSeek()
                            }
                        }

                        // A short stem under the ruler, not the full column height: at
                        // 12px wide over every track any touch within ±6px of the playhead
                        // grabbed the scrubber instead of the clip underneath it.
                        MouseArea {
                            id: playheadLineDrag
                            width: 12
                            height: 24
                            x: -(width - Theme.playheadLineWidth) / 2
                            y: flick.contentY + playheadDragArea.height
                            preventStealing: true
                            drag.target: playhead
                            drag.axis: Drag.XAxis
                            drag.threshold: 0
                            drag.minimumX: 0
                            drag.maximumX: flick.contentWidth - Theme.playheadLineWidth
                            onPressed: root.beginPlayheadSeek()
                            onReleased: {
                                playhead.finishSeek()
                                root.endPlayheadSeek()
                            }
                            onCanceled: {
                                playhead.finishSeek()
                                root.endPlayheadSeek()
                            }
                        }
                    }

                    Item {
                        id: cutOverlay
                        visible: root.timelineTool !== ""
                        enabled: root.timelineTool !== ""
                        x: 0
                        y: root.seekHeaderHeight
                        width: parent.width
                        height: Math.max(root.totalTracksHeight(), flick.height - root.seekHeaderHeight)
                        z: 20

                        function seconds(mx) {
                            return EditorState.snapTime(Math.max(0, mx) / root.pxPerSecond)
                        }

                        function clearHover() {
                            root.cutHoverSeconds = -1
                            root.cutHoverTrack = -1
                            root.cutHoverClip = -1
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
                            id: cutArea
                            anchors.fill: parent
                            acceptedButtons: Qt.LeftButton
                            // No preventStealing: this covers the whole track area while a
                            // cut tool is active, and holding the grab froze pan and pinch
                            // outright — the timeline could not be moved to reach the cut.
                            // Instead the cut is abandoned as soon as the gesture reads as
                            // a pan, and the Flickable takes over.
                            property real pressX: 0
                            property real pressY: 0
                            property bool panning: false

                            onPressed: (mouse) => {
                                pressX = mouse.x
                                pressY = mouse.y
                                panning = false
                                cutOverlay.updateHover(mouse.x, mouse.y)
                            }
                            onPositionChanged: (mouse) => {
                                if (!panning
                                        && (Math.abs(mouse.x - pressX) > Qt.styleHints.startDragDistance
                                            || Math.abs(mouse.y - pressY) > Qt.styleHints.startDragDistance)) {
                                    panning = true
                                    cutOverlay.clearHover()
                                }
                                if (!panning)
                                    cutOverlay.updateHover(mouse.x, mouse.y)
                            }
                            onReleased: cutOverlay.clearHover()
                            onCanceled: {
                                panning = true
                                cutOverlay.clearHover()
                            }
                            onClicked: (mouse) => {
                                if (panning)
                                    return
                                const atSeconds = cutOverlay.seconds(mouse.x)
                                const trackIdx = root.trackIndexAtY(mouse.y)
                                if (trackIdx < 0)
                                    return
                                const clipIdx = root.clipIndexAtPosition(trackIdx, Math.max(0, mouse.x))
                                if (clipIdx < 0)
                                    return
                                // timelineTool is a mode, not a boolean: the trim tools drop
                                // everything to one side of the cut instead of splitting.
                                if (root.timelineTool === "trimStart")
                                    EditorState.splitClipLeftAt(trackIdx, clipIdx, atSeconds)
                                else if (root.timelineTool === "trimEnd")
                                    EditorState.splitClipRightAt(trackIdx, clipIdx, atSeconds)
                                else
                                    EditorState.splitClipAt(trackIdx, clipIdx, atSeconds)
                                // One of the few taps here that changes the project rather than the
                                // view, and on a phone the cut lands under the finger that made it.
                                Haptics.confirm()
                            }
                        }

                        Column {
                            visible: root.cutHoverSeconds >= 0 && !cutArea.panning
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
    }

    Connections {
        target: EditorState
        function onPlayheadSecondsChanged() {
            if (EditorState.playing)
                root.ensurePlayheadVisible()
        }
    }
}
