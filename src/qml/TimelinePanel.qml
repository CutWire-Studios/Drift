import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Window
import Drift
PanelFrame {
    id: root

    // Keyframe lane height the user dragged to; session-only, kept here because the graph
    // itself is unloaded whenever no clip or no keyframe tab is selected.
    property real keyframeLaneHeight: 88

    property real zoom: 1.0
    property bool keyframesTab: false
    // Set by PanelPool while the Audio Mixer panel is on screen; the built-in strip steps aside.
    property bool externalMixerShown: false

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

    property real panLastSceneX: 0
    property real panLastSceneY: 0

    // CapCut: V = Select, B = Blade. These are registered actions ("selectTool" /
    // "bladeTool") so they appear in the shortcut list and can be rebound; the tool
    // state lives here rather than in the backend, so Main.qml dispatches them
    // alongside Escape, which exits the tool without clearing the selection.

    // Y offset of a track row within the track column (excludes ruler/bookmark).
    function trackOffsetY(index) {
        var cursor = 0
        for (var i = 0; i < index && i < tracks.length; i++) {
            if (!trackOccupiesARow(i))
                continue
            cursor += trackHeight(i) + Theme.trackGap
        }
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
    onPxPerSecondChanged: EditorState.setSnapRadius(8, pxPerSecond)
    Component.onCompleted: {
        TouchDrag.registerTarget(root)
        EditorState.setSnapRadius(8, pxPerSecond)
    }
    Component.onDestruction: TouchDrag.unregisterTarget(root)
    // Exposed for clip filmstrip viewport culling (Flickable ids are local).
    readonly property real timelineViewX: flick.contentX
    readonly property real timelineViewW: flick.width
    // Exposed so the overview strip can clamp its scroll target without
    // duplicating the Flickable's contentWidth formula.
    readonly property real timelineContentWidth: flick.contentWidth

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
    // What the scene-graph clip renderer reads from this panel, bound once here instead of once
    // per clip.
    TimelineViewState {
        id: timelineViewState
        viewX: root.timelineViewX
        viewW: root.timelineViewW
        pxPerSecond: root.pxPerSecond
        touchMode: false
        multiSelectActive: root.multiSelectActive === true
        totalTracksHeight: root.totalTracksHeightCached
        moveFollowActive: root.moveFollowActive
        moveLeaderTrack: root.moveLeaderTrack
        moveLeaderClip: root.moveLeaderClip
        moveFollowDeltaX: root.moveFollowDeltaX
        moveFollowDeltaY: root.moveFollowDeltaY
        trimFollowActive: root.trimFollowActive
        trimFollowLinkId: root.trimFollowLinkId
        trimFollowClipId: root.trimFollowClipId
        trimFollowStart: root.trimFollowStart
        trimFollowDuration: root.trimFollowDuration
        trimFollowIn: root.trimFollowIn
        trimFollowOut: root.trimFollowOut
        rippleShiftIds: root.rippleShiftIds
        rippleShiftDelta: root.rippleShiftDelta
        effectDropTrack: root.effectDropTrackIndex
        effectDropClip: root.effectDropClipIndex
        style: ({
            "clipVideo": Theme.c.timelineClipItem.videoBackground,
            "clipAudio": Theme.c.timelineClipItem.audioBackground,
            "clipText": Theme.c.timelineClipItem.textBackground,
            "clipSubtitle": Theme.c.timelineClipItem.subtitleBackground,
            "clipGraphic": Theme.c.timelineClipItem.graphicBackground,
            "clipEffect": Theme.c.timelineClipItem.effectBackground,
            "clipComposite": Theme.c.timelineClipItem.compositeBackground,
            "adjustmentVideo": Theme.c.timelineClipItem.adjustmentVideoBackground,
            "adjustmentAudio": Theme.c.timelineClipItem.adjustmentAudioBackground,
            "adjustmentMask": Theme.c.timelineClipItem.adjustmentMaskBackground,
            "adjustmentTransform": Theme.c.timelineClipItem.transformBackground,
            "primary": Theme.c.timelineClipItem.accent,
            "scrim": Theme.c.timelineClipItem.scrim,
            "proxyBand": Theme.c.timelineClipItem.proxyBand,
            "onMedia": Theme.c.timelineClipItem.foreground,
            "waveform": Theme.c.timelineClipItem.waveform,
            "mutedForeground": Theme.c.timelineClipItem.foregroundMuted,
            "panelBorder": Theme.c.timelineClipItem.border,
            "proxyPill": Theme.c.timelineClipItem.proxyBackground,
            "proxyPillForeground": Theme.c.timelineClipItem.proxyForeground,
            "editFriendlyPill": Theme.c.timelineClipItem.editFriendlyBackground,
            "editFriendlyPillForeground": Theme.c.timelineClipItem.editFriendlyForeground,
            "warning": Theme.c.timelineClipItem.warning,
            "missing": Theme.c.timelineClipItem.missing,
            "fontFamily": Theme.fontFamily,
            "fontSizeTiny": Theme.fontSizeTiny,
            "fontSizeXs": Theme.fontSizeXs,
            "radiusSm": Theme.c.timelineClipItem.radius,
            "radiusXs": Theme.c.timelineClipItem.radiusXs,
            "ringWidth": Theme.c.timelineClipItem.selectionBorderWidth,
            "borderWidthFocus": Theme.c.timelineClipItem.focusBorderWidth,
            "headerBandHeight": Theme.clipHeaderBandHeight,
            "clipMinWidth": Theme.clipMinWidth,
            "iconSizeSm": Theme.iconSizeSm,
            "spacingLg": Theme.spacingLg,
            "spacingMd": Theme.spacingMd,
            "edgeMarginDesktop": 14,
            "edgeMarginTouch": Theme.androidClipEdgeMargin,
            "trimHotspotExtraDesktop": 10,
            "trimHotspotExtraTouch": Theme.androidTrimHotspotExtra,
            "proxyLabel": qsTr("Proxy"),
            "proxyTooltip": qsTr("Previewing from a low-resolution proxy. Export uses the original."),
            "editFriendlyLabel": qsTr("Edit-friendly"),
            "editFriendlyTooltip": qsTr("Converted to a constant frame rate for smooth editing"),
            "vfrTooltip": qsTr("Variable frame rate. This clip can drift out of sync with its audio. Right-click it and choose Convert to edit-friendly format."),
            "missingTooltip": qsTr("Media file not found. Use Replace media… on this item to point it at the file again."),
            "spanToolTip": qsTr("Transform layer: moves, scales and turns every track its bracket covers")
        })
    }

    PlayheadInterpolator {
        id: needle
        active: root.visible
        onSecondsChanged: {
            if (EditorState.playing)
                root.ensurePlayheadVisible()
        }
    }
    // True between beginPlayheadSeek and endPlayheadSeek when playback was
    // interrupted so a click or drag could land, and should resume on release.
    property bool resumePlaybackAfterSeek: false
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
    // Window a dragged transition would cover (seconds; track -1 when hidden).
    property int transitionDropTrackIndex: -1
    property real transitionDropStart: 0
    property real transitionDropEnd: 0
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
    // ride along on the X and Y axes so they don't sit still until drop. CapCut-style.

    // A trim in progress, broadcast so the dragged clip's linked A/V companion can follow it on
    // screen. The commit gives the partner identical timing (syncLinkedTiming), so the preview
    // is simply the same geometry applied to both.
    property bool trimFollowActive: false
    property string trimFollowLinkId: ""
    property string trimFollowClipId: ""
    property real trimFollowStart: 0
    property real trimFollowDuration: 0
    property real trimFollowIn: 0
    property real trimFollowOut: 0
    // With ripple on, the clips the trim will push along and by how much. The map is for
    // per-clip lookups in QML; the list feeds the scene-graph renderer.
    property var rippleShiftIds: []
    property var rippleShiftMap: ({})
    property real rippleShiftDelta: 0

    function setTrimFollow(linkId, clipId, start, duration, inPoint, outPoint, rippleIds, rippleDelta) {
        trimFollowLinkId = linkId || ""
        trimFollowClipId = clipId || ""
        trimFollowStart = start
        trimFollowDuration = duration
        trimFollowIn = inPoint
        trimFollowOut = outPoint
        trimFollowActive = trimFollowLinkId !== ""
        const ids = rippleIds || []
        if (ids.join("\n") !== rippleShiftIds.join("\n")) {
            const map = {}
            for (let i = 0; i < ids.length; ++i)
                map[ids[i]] = true
            rippleShiftMap = map
            rippleShiftIds = ids
        }
        rippleShiftDelta = ids.length > 0 ? (rippleDelta || 0) : 0
    }

    function clearTrimFollow() {
        trimFollowActive = false
        trimFollowLinkId = ""
        trimFollowClipId = ""
        rippleShiftIds = []
        rippleShiftMap = {}
        rippleShiftDelta = 0
    }

    property bool moveFollowActive: false
    property int moveLeaderTrack: -1
    property int moveLeaderClip: -1
    property real moveFollowDeltaX: 0
    property real moveFollowDeltaY: 0

    function beginMoveFollow(trackIndex, clipIndex) {
        moveLeaderTrack = trackIndex
        moveLeaderClip = clipIndex
        moveFollowDeltaX = 0
        moveFollowDeltaY = 0
        moveFollowActive = true
    }

    function updateMoveFollow(deltaX, deltaY) {
        if (!moveFollowActive)
            return
        moveFollowDeltaX = deltaX
        moveFollowDeltaY = deltaY || 0
    }

    function clearMoveFollow() {
        moveFollowActive = false
        moveLeaderTrack = -1
        moveLeaderClip = -1
        moveFollowDeltaX = 0
        moveFollowDeltaY = 0
    }

    // Shared by library drops and in-timeline clip moves so both snap and show
    // the same outline the same way.
    function showLandingPreview(trackIndex, desiredStart, duration, excludeClipId) {
        const snapped = snapClipStart(desiredStart, duration, excludeClipId)
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

    // A visual-only transition dropped on an audio track becomes the crossfade it can actually be.
    function transitionKindForTrack(trackIndex, kind) {
        const kinds = EditorState.transitionKindsForTrack(trackIndex)
        for (let i = 0; i < kinds.length; ++i) {
            if (kinds[i].kind === kind)
                return kind
        }
        return "crossfade"
    }

    // The outgoing (earlier) clip for a transition drop at timeline x, and the window
    // (seconds) addTransition would give it: the overlap, or a 0.5 s window on the cut.
    function transitionDropTarget(trackIndex, xPixels) {
        if (trackIndex < 0 || trackIndex >= tracks.length)
            return null
        const track = tracks[trackIndex]
        if (track.type !== "video" && track.type !== "shape" && track.type !== "text"
                && track.type !== "audio")
            return null
        const seconds = xPixels / pxPerSecond
        const clips = track.clips
        const order = []
        for (let i = 0; i < clips.length; i++)
            order.push(i)
        order.sort((a, b) => clips[a].start - clips[b].start || a - b)
        let best = null
        let bestDist = 1e9
        let bestRight = -1
        let groupStart = 0
        for (let p = 0; p < order.length; p++) {
            const i = order[p]
            const left = clips[i]
            while (clips[order[groupStart]].start < left.start)
                groupStart++
            const leftEnd = left.start + left.duration
            for (let q = groupStart; q < order.length; q++) {
                const j = order[q]
                const right = clips[j]
                if (right.start - leftEnd > 0.001)
                    break
                if (i === j)
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
                    const dist = Math.abs(seconds - (regionStart + regionEnd) / 2)
                    const earlier = best !== null && dist === bestDist
                            && (i < best.leftClip || (i === best.leftClip && j < bestRight))
                    if (dist < bestDist || earlier) {
                        bestDist = dist
                        bestRight = j
                        best = {
                            leftClip: i,
                            start: Math.max(left.start, regionStart),
                            end: Math.min(right.start + right.duration, regionEnd)
                        }
                    }
                }
            }
        }
        return best
    }

    function updateTransitionDropPreview(trackIndex, xPixels) {
        const target = transitionDropTarget(trackIndex, xPixels)
        if (!target) {
            clearTransitionDropPreview()
            return
        }
        transitionDropTrackIndex = trackIndex
        transitionDropStart = target.start
        transitionDropEnd = target.end
    }

    function clearTransitionDropPreview() {
        transitionDropTrackIndex = -1
    }

    function applyTransitionDrop(trackIndex, xPixels, kind) {
        clearTransitionDropPreview()
        const target = transitionDropTarget(trackIndex, xPixels)
        if (!target || !kind || kind.length === 0)
            return
        EditorState.addTransition(trackIndex, target.leftClip, transitionKindForTrack(trackIndex, kind), EditorState.preferences.transitionDurationSeconds)
    }

    // The bin row's media kind ("video", "audio", "image", ...), which is what the track-fit
    // questions below are really asking about. A file still in flight from the file manager has
    // no row, so it answers them with AssetLibrary.provisionalKindForUrl() instead.
    function assetKind(assetIndex) {
        const asset = AssetLibrary.assetAt(assetIndex)
        return asset ? (asset.kind || "video") : "video"
    }

    function assetDurationSeconds(assetIndex) {
        const asset = AssetLibrary.assetAt(assetIndex)
        if (!asset)
            return 5.0
        if (asset.kind === "image" || !(asset.durationSeconds > 0))
            return 5.0
        // A bin-preview trim shortens what actually lands, so the landing preview matches it.
        return asset.placedDurationSeconds
    }

    // Snap a clip's desired start against timeline targets, testing both edges.
    // Returns {start, guide}; guide < 0 means no snap occurred.
    // `excludeClipId` is the clip being dragged, if any: it is still parked at its old start in
    // the model, so leaving its edges in the target set pins every short drag back to the origin.
    function snapClipStart(desiredStart, duration, excludeClipId) {
        const ex = excludeClipId || ""
        const l = EditorState.snapTime(desiredStart, ex)
        const rEdge = EditorState.snapTime(desiredStart + duration, ex)
        const lSnapped = Math.abs(l - desiredStart) > 0.0005
        const rSnapped = Math.abs(rEdge - (desiredStart + duration)) > 0.0005
        if (lSnapped && (!rSnapped || Math.abs(l - desiredStart) <= Math.abs(rEdge - duration - desiredStart)))
            return { "start": l, "guide": l }
        if (rSnapped)
            return { "start": rEdge - duration, "guide": rEdge }
        return { "start": desiredStart, "guide": -1 }
    }

    // Actual row height: the per-type default, the lane's DAW-style vertical zoom, and room for
    // any nested adjustment lanes. Zero for a nested lane itself, which is drawn inside its
    // parent's row rather than getting one of its own.
    //
    // The rule lives in C++ so TimelinePanel, TrackHeaderColumn and AndroidTimeline cannot drift
    // apart — they used to hold three copies of it that had to agree or the headers slid out of
    // line with the rows.
    function trackHeight(index) {
        // Reading `tracks` is deliberate: EditorState.trackRowHeight is a plain call with no
        // binding dependency of its own, so without this a caller's binding would never
        // re-evaluate when a lane is added or a track's scale changes.
        const dep = tracks.length
        return EditorState.trackRowHeight(index, {
            "video": Theme.trackHeightVideo,
            "audio": Theme.trackHeightAudio,
            "text": Theme.trackHeightText,
            "subtitle": Theme.trackHeightSubtitle,
            "shape": Theme.trackHeightShape,
            "adjustment": Theme.trackHeightAdjustment,
            "lane": Theme.adjustmentLaneHeight
        })
    }

    // Top of a row inside the track column, which stacks the visible rows with a gap.
    function trackRowTop(index) {
        var cursor = 0
        for (var i = 0; i < index && i < tracks.length; i++) {
            if (!trackOccupiesARow(i))
                continue
            cursor += trackHeight(i) + Theme.trackGap
        }
        return cursor
    }

    // Nested lanes take no row of their own, so they must not contribute a gap either.
    function trackOccupiesARow(index) {
        return index >= 0 && index < tracks.length && !tracks[index].isAdjustmentLane
    }

    // Adjustment layers are tinted by what they act on, so a glance at a lane says whether it is
    // grading the picture, treating the audio, or cutting a mask.
    function adjustmentColor(kind) {
        if (kind === "audioEffects") return Theme.c.timelineClipItem.adjustmentAudioBackground
        if (kind === "mask") return Theme.c.timelineClipItem.adjustmentMaskBackground
        if (kind === "transform") return Theme.c.timelineClipItem.transformBackground
        return Theme.c.timelineClipItem.adjustmentVideoBackground
    }
    function clipColor(type) {
        if (type === "text") return Theme.c.timelineClipItem.textBackground;
        if (type === "subtitle") return Theme.c.timelineClipItem.subtitleBackground;
        if (type === "audio") return Theme.c.timelineClipItem.audioBackground;
        if (type === "graphic") return Theme.c.timelineClipItem.graphicBackground;
        if (type === "effect" || type === "adjustment") return Theme.c.timelineClipItem.effectBackground;
        if (type === "composite") return Theme.c.timelineClipItem.compositeBackground;
        return Theme.c.timelineClipItem.videoBackground; // video: no flat fill, thumbnails would go here
    }

    // Computed once per model change instead of at each of the ten call sites below — one of
    // which is the drag bounds of every clip delegate, making it O(clips x tracks) on any edit.
    readonly property real totalTracksHeightCached: (root.tracks, root.totalTracksHeight())

    function totalTracksHeight() {
        var h = 0;
        var rows = 0;
        for (var i = 0; i < tracks.length; i++) {
            if (!trackOccupiesARow(i))
                continue;
            h += trackHeight(i);
            if (rows > 0) h += Theme.trackGap;
            rows++;
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

    function firstCompatibleTrackIndexForKind(mediaKind) {
        for (var i = 0; i < tracks.length; i++) {
            if (EditorState.trackAcceptsKind(i, mediaKind))
                return i
        }
        return -1
    }

    function firstCompatibleTrackIndex(assetIndex) {
        return firstCompatibleTrackIndexForKind(assetKind(assetIndex))
    }

    function trackIndexAtY(y) {
        var cursor = 0;
        for (var i = 0; i < tracks.length; i++) {
            if (!trackOccupiesARow(i))
                continue;
            const th = trackHeight(i);
            if (y >= cursor && y < cursor + th)
                return i;
            cursor += th + Theme.trackGap;
        }
        return -1;
    }

    // Where a dragged clip should land, from a y in track-column coordinates.
    // `track` is the row it fell on (-1 for empty space below every row); `lane` is the track
    // index of the nested lane inside that row, or -1 for the row's own clip area.
    function dropTargetAtY(y) {
        const track = trackIndexAtY(y)
        if (track < 0)
            return { "track": -1, "lane": -1 }
        return { "track": track, "lane": laneIndexAtY(track, y - trackOffsetY(track)) }
    }

    // Which nested lane of `trackIndex`, if any, a y inside that row falls in. Lanes stack as
    // strips across the top of the row; -1 means the clip area below them.
    function laneIndexAtY(trackIndex, yInRow) {
        const lanes = EditorState.adjustmentLanes(trackIndex)
        for (var i = 0; i < lanes.length; i++) {
            const top = i * Theme.adjustmentLaneHeight
            if (yInRow >= top && yInRow < top + Theme.adjustmentLaneHeight)
                return lanes[i]
        }
        return -1
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
        return dropTargetAtYForKind(assetKind(assetIndex), y)
    }

    function dropTargetAtYForKind(mediaKind, y) {
        const count = tracks.length
        if (count === 0)
            return { "newTrack": true, "insertIndex": 0 }

        var cursor = 0
        for (var i = 0; i < count; i++) {
            if (!trackOccupiesARow(i))
                continue
            const h = trackHeight(i)
            const rowEnd = cursor + h
            // Claim the trailing gap too, so the 6px between rows resolves to a
            // boundary rather than to nothing.
            if (y < rowEnd + Theme.trackGap / 2) {
                if (!EditorState.trackAcceptsKind(i, mediaKind))
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
        for (var i = 0; i < insertIndex && i < tracks.length; i++) {
            if (!trackOccupiesARow(i))
                continue
            cursor += trackHeight(i) + Theme.trackGap
        }
        return Math.max(0, cursor - Theme.trackGap / 2)
    }

    // The one place that decides where a dropped clip lands, so the preview during the drag and
    // the placement on release can never disagree — they used to hold two copies of this rule.
    // An empty project fills its existing track wherever you aim, rather than stacking a second
    // one on top of it.
    function resolveDropTarget(mediaKind, dropY) {
        if (!timelineHasClips()) {
            const firstIdx = firstCompatibleTrackIndexForKind(mediaKind)
            if (firstIdx >= 0)
                return { "newTrack": false, "track": firstIdx }
        }
        return dropTargetAtYForKind(mediaKind, dropY)
    }

    function updateDropPreviewForKind(mediaKind, duration, dropX, dropY) {
        const desired = Math.max(0, dropX / pxPerSecond)
        const target = resolveDropTarget(mediaKind, dropY)

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

    function updateAssetDropPreview(assetIndex, dropX, dropY) {
        if (EditorState.masking.assetMaskRole(assetIndex) !== "") {
            dropRouter.hover("maskMedia", String(assetIndex), dropX, dropY)
            return
        }
        updateDropPreviewForKind(assetKind(assetIndex), assetDurationSeconds(assetIndex),
                                 dropX, dropY)
    }

    // Placement against an already-resolved target. Split out because the file-manager drop has
    // to resolve its target at drop time and place minutes later, once the import has probed:
    // dropY would be meaningless by then if the user scrolled the tracks in the meantime.
    // `onPlaced` runs once the clip is actually in the project, which is not necessarily before
    // this returns: the first clip of a pristine project opens the canvas setup dialog first, and
    // a multi-file drop cannot place its second clip until that has been answered. It never runs
    // if the dialog is dismissed, which is the existing meaning of dismissing it — nothing added.
    function placeAssetAtTarget(assetIndex, atSeconds, target, onPlaced) {
        if (assetIndex < 0 || !target)
            return

        function runAdd() {
            if (target.newTrack)
                EditorState.addClipFromAssetOnNewTrackAt(assetIndex, target.insertIndex, atSeconds)
            else
                EditorState.addClipFromAssetAt(assetIndex, target.track, atSeconds)
            if (onPlaced)
                onPlaced()
        }

        if (ShellBridge.main && ShellBridge.main.configureAndAddAsset)
            ShellBridge.main.configureAndAddAsset(assetIndex, runAdd)
        else
            runAdd()
    }

    function performAssetDrop(assetIndex, dropX, dropY) {
        if (assetIndex < 0)
            return
        if (EditorState.masking.assetMaskRole(assetIndex) !== "") {
            dropRouter.drop("maskMedia", String(assetIndex), "", dropX, dropY)
            return
        }
        placeAssetAtTarget(assetIndex, Math.max(0, dropX / pxPerSecond),
                           resolveDropTarget(assetKind(assetIndex), dropY))
    }

    // File-manager drops ---------------------------------------------------------------------
    //
    // A file dragged straight from the file manager onto a track is imported and placed in one
    // gesture. It still lands in the bin: a clip is addressed by asset index, so there is no such
    // thing as a clip whose media is outside the library — this is "import, then place", and the
    // assets panel shows the row either way. Dragging to the panel first is untouched.

    // Media kind of the file drag currently over the timeline, "" when there is none. The
    // new-track ghost sizes its lane from this, having no asset index to ask about yet.
    property string pendingDropKind: ""

    readonly property TimelineDropRouter dropRouter: TimelineDropRouter { panel: root }

    // Set from the drop until the import it started settles. The landing outline stays painted at
    // the frozen spot for that whole window — probing a large file takes seconds, and clearing
    // the outline on release made the drop look like it had been ignored.
    property bool importDropPending: false

    // Nothing has been opened at drag time, let alone probed, so the outline cannot promise a
    // real length. Same fallback assetDurationSeconds() uses for a row whose duration is unknown.
    readonly property real urlDropPreviewDuration: 5.0

    // What the drag lands as. A mixed selection is placed file by file, each on a track that
    // fits it, but the outline can only promise one spot — so it promises the first file's.
    function urlDropKind(urls) {
        if (!urls)
            return ""
        for (var i = 0; i < urls.length; ++i) {
            const kind = AssetLibrary.provisionalKindForUrl(urls[i])
            if (kind.length > 0)
                return kind
        }
        return ""
    }

    function updateUrlDropPreview(drop) {
        // A drag that cannot say what it carries yet — some platforms only hand the payload over
        // on release — is previewed as video rather than not previewed at all. The drop itself
        // re-reads the urls and places by what they really are.
        const kind = drop.hasUrls ? urlDropKind(drop.urls) : "video"
        pendingDropKind = kind
        if (kind.length === 0) {
            // Nothing in the drag is media. Promise nothing rather than a spot it cannot land on.
            clearLandingPreview()
            return
        }
        updateDropPreviewForKind(kind, urlDropPreviewDuration, drop.x, drop.y)
    }

    function clearUrlDropState() {
        pendingDropKind = ""
        importDropPending = false
    }

    function performUrlDrop(urls, dropX, dropY) {
        if (!urls || urls.length === 0) {
            clearUrlDropState()
            clearLandingPreview()
            return
        }

        const kind = urlDropKind(urls)
        if (kind.length === 0) {
            clearUrlDropState()
            clearLandingPreview()
            // Still handed to the import, so it reports why in the usual words instead of the
            // drop appearing to do nothing at all.
            MediaImport.importUrls(urls, true)
            return
        }

        // Frozen here rather than read back in the callback: the import is asynchronous, and the
        // user is free to scroll and zoom while it runs — by then dropY would name a different
        // track and dropX a different second.
        const atSeconds = Math.max(0, dropX / pxPerSecond)
        const target = resolveDropTarget(kind, dropY)

        root.importDropPending = true
        const started = MediaImport.importUrls(urls, true, function (added) {
            root.clearUrlDropState()
            root.clearLandingPreview()
            if (added <= 0)
                return
            // The idiom every caller of this callback uses: the rows it reports are the last
            // `added` in the library. MediaImport waits for their probes first, so their
            // durations and resolutions are real by now — which is what the clip lengths and the
            // canvas setup both need.
            root.placeImportedAssets(AssetLibrary.count - added, added, atSeconds, target)
        })

        if (!started) {
            root.clearUrlDropState()
            root.clearLandingPreview()
        }
    }

    // Where a file after the first one goes when the track the previous one landed on will not
    // take it — an audio file among videos, typically. The first track that fits, else a new one
    // at the bottom, which is where adding it from the bin would have put it.
    function targetForFollowOnKind(mediaKind) {
        const idx = firstCompatibleTrackIndexForKind(mediaKind)
        if (idx >= 0)
            return { "newTrack": false, "track": idx }
        return { "newTrack": true, "insertIndex": tracks.length }
    }

    // The toolbar's + button picks files and places them at the playhead as soon as
    // their metadata is ready. Keep the original time/track while the dialog is open
    // and while asynchronous media probing runs.
    function importMediaDirectlyToTimeline() {
        if (MediaImport.importing)
            return

        const atSeconds = Math.max(0, playheadSeconds)
        const selectedTrack = EditorState.selectedTrack
        MediaImport.browseAndImport(function (added) {
            if (added <= 0)
                return

            const firstIndex = AssetLibrary.count - added
            const kind = root.assetKind(firstIndex)
            const target = selectedTrack >= 0 && selectedTrack < root.tracks.length
                           && EditorState.trackAcceptsKind(selectedTrack, kind)
                           ? { "newTrack": false, "track": selectedTrack }
                           : root.targetForFollowOnKind(kind)
            root.placeImportedAssets(firstIndex, added, atSeconds, target)
        })
    }

    // Places the rows a drop just imported: the first at the spot the drag promised, the rest
    // back to back after it. One at a time rather than a loop, because placing the first can
    // block on the canvas setup dialog and the rest have to queue behind that answer.
    function placeImportedAssets(firstIndex, count, atSeconds, target) {
        function placeAt(n, cursor, landing) {
            if (n >= count)
                return

            const assetIndex = firstIndex + n
            const kind = assetKind(assetIndex)
            // The promised spot belongs to the first file. Each one after it follows on the track
            // the previous one actually landed on, whenever that track will have it, so a batch
            // reads as one sequence instead of fanning out across new tracks.
            const fits = landing && !landing.newTrack
                         && EditorState.trackAcceptsKind(landing.track, kind)
            const here = fits ? landing : root.targetForFollowOnKind(kind)

            root.placeAssetAtTarget(assetIndex, cursor, here, function () {
                // Read back rather than advanced by the asset's duration: snapping and a
                // bin-preview trim both change where the clip really ends, and the next one has
                // to start there or it lands after a gap.
                const t = EditorState.selectedTrack
                const c = EditorState.selectedClip
                var next = cursor
                var landed = here
                if (t >= 0 && t < root.tracks.length) {
                    const clips = root.tracks[t].clips || []
                    if (c >= 0 && c < clips.length)
                        next = clips[c].start + clips[c].duration
                    landed = { "newTrack": false, "track": t }
                }
                placeAt(n + 1, next, landed)
            })
        }

        placeAt(0, atSeconds, target)
    }

    function handleTimelineWheel(wheel) {
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
        const invert = EditorState.preferences.invertTimelineScroll

        // Invert (Kdenlive-style): wheel pans along time, Shift+wheel moves
        // between tracks. Trackpad horizontal motion always pans time.
        if (invert) {
            if (wheel.modifiers & Qt.ShiftModifier) {
                const delta = dy !== 0 ? dy : dx
                panTimelineBy(0, delta)
                return
            }
            if (dx !== 0)
                panTimelineBy(dx, 0)
            if (dy !== 0)
                panTimelineBy(dy, 0)
            return
        }

        // Shift forces horizontal scrolling regardless of overflow.
        if (wheel.modifiers & Qt.ShiftModifier) {
            const delta = dy !== 0 ? dy : dx
            panTimelineBy(delta, 0)
            return
        }

        // Trackpad horizontal component always scrolls horizontally.
        if (dx !== 0)
            panTimelineBy(dx, 0)

        // Vertical wheel scrolls the tracks when they overflow the viewport;
        // otherwise it falls back to horizontal so short timelines keep the
        // previous wheel-to-pan behaviour.
        if (dy !== 0) {
            const maxY = Math.max(0, flick.contentHeight - flick.height)
            if (maxY > 0)
                panTimelineBy(0, dy)
            else
                panTimelineBy(dy, 0)
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

    property int savePresetTrack: -1
    property int savePresetClip: -1

    function requestSaveEffectPreset(trackIndex, clipIndex) {
        if (trackIndex < 0 || clipIndex < 0)
            return
        root.savePresetTrack = trackIndex
        root.savePresetClip = clipIndex
        const clips = root.tracks[trackIndex].clips || []
        const name = clipIndex < clips.length ? (clips[clipIndex].name || "") : ""
        effectPresetNameDialog.openWith(qsTr("Save effect preset"), name)
    }

    function requestConvertTextToSubtitle() {
        convertToSubtitleDialog.open()
    }

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

    // Seeking while the engine clock is running lands ahead of the click: the
    // sink's processedUSecs is cumulative from play(), so the visible playhead
    // becomes clickTime + elapsed. Pause for the gesture and resume on release.
    function beginPlayheadSeek() {
        if (EditorState.playing) {
            resumePlaybackAfterSeek = true
            EditorState.playing = false
        }
        EditorState.beginScrub()
    }

    function endPlayheadSeek() {
        EditorState.endScrub()
        if (!resumePlaybackAfterSeek)
            return
        resumePlaybackAfterSeek = false
        EditorState.playing = true
    }

    function ensurePlayheadVisible() {
        const playheadX = needle.seconds * pxPerSecond;
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

    // --- TouchDrag drop target ------------------------------------------------

    function touchDropContains(sceneX, sceneY) {
        return touchDropPoint(sceneX, sceneY).inside
    }

    // Scene point -> { inside, x (content px), y (track-column px), viewX, viewY }.
    // The ruler strip is pinned to the top of the viewport, so anything under it is
    // ruler, not track, however far the tracks have been scrolled.
    function touchDropPoint(sceneX, sceneY) {
        const p = flick.mapFromItem(null, sceneX, sceneY)
        const inside = p.x >= 0 && p.x <= flick.width
                       && p.y >= flick.headerHeight && p.y <= flick.height
        return { "inside": inside,
                 "x": p.x + flick.contentX,
                 "y": p.y + flick.contentY - flick.headerHeight,
                 "viewX": p.x,
                 "viewY": p.y }
    }

    function clearTouchDrop() {
        clearLandingPreview()
        clearEffectDropHighlight()
        pendingDropKind = ""
        touchDropEdgeScroll.stop()
    }

    function updateTouchDrop(kind, payload, sceneX, sceneY) {
        const pt = touchDropPoint(sceneX, sceneY)
        if (!pt.inside) {
            clearTouchDrop()
            return false
        }

        touchDropEdgeScroll.viewX = pt.viewX
        touchDropEdgeScroll.viewY = pt.viewY
        touchDropEdgeScroll.start()

        if (kind === "media") {
            clearEffectDropHighlight()
            updateAssetDropPreview(payload, pt.x, pt.y)
            return true
        }
        return dropRouter.hover(kind, payload, pt.x, pt.y)
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
        dropRouter.drop(kind, payload, TouchDrag.label, pt.x, pt.y)
    }

    // The pane shows only a few tracks and seconds, so without this a lifted card
    // could only land on what happened to be on screen.
    Timer {
        id: touchDropEdgeScroll
        interval: 16
        repeat: true
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
            if (viewY < flick.headerHeight + band)
                dy = -speed * (1 - (viewY - flick.headerHeight) / band)
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

    function panTimelineBy(dx, dy) {
        contentXAnimation.stop()
        const maxX = Math.max(0, flick.contentWidth - flick.width)
        const maxY = Math.max(0, flick.contentHeight - flick.height)
        if (dx)
            flick.contentX = Math.max(0, Math.min(maxX, flick.contentX - dx))
        if (dy)
            flick.contentY = Math.max(0, Math.min(maxY, flick.contentY - dy))
    }

    // Viewport geometry read by TimelineTrackArea's lifted-clip autoscroll (same names as AndroidTimeline).
    readonly property real seekHeaderHeight: flick.headerHeight
    readonly property real timelineViewY: flick.contentY
    readonly property real timelineViewH: flick.height

    // Touch-lift autoscroll and edge-strip scrolling in TimelineTrackArea / TimelineClipItem call
    // this by name. Returns how far the view actually moved.
    function dragEdgeScroll(dx, dy) {
        const beforeX = flick.contentX
        const beforeY = flick.contentY
        panTimelineBy(-dx, -dy)
        return { "x": flick.contentX - beforeX, "y": flick.contentY - beforeY }
    }

    function beginTimelinePan(item, mouse) {
        const p = item.mapToItem(null, mouse.x, mouse.y)
        panLastSceneX = p.x
        panLastSceneY = p.y
        contentXAnimation.stop()
    }

    function updateTimelinePan(item, mouse) {
        const p = item.mapToItem(null, mouse.x, mouse.y)
        panTimelineBy(p.x - panLastSceneX, p.y - panLastSceneY)
        panLastSceneX = p.x
        panLastSceneY = p.y
    }

    Column {
        anchors.fill: parent
        anchors.margins: root.border.width

        // === toolbar =================================================================
        TimelineToolbar {
            id: toolbar
            width: parent.width
            panel: root
        }

        // === full-project overview strip ==============================================
        // Toggled from the toolbar's right end; collapses to nothing rather than merely
        // hiding, so the tracks get the height back. Unloaded while off.
        Loader {
            id: overviewStrip
            width: parent.width
            active: EditorState.preferences.timelineOverviewVisible
            visible: active
            height: active ? Theme.timelineOverviewHeight : 0
            sourceComponent: Component {
                TimelineOverview { panel: root }
            }
        }

        // === ruler + track labels + tracks ================================================
        Column {
            width: parent.width
            height: parent.height - toolbar.height - overviewStrip.height

            // Loaded only on the tabs it can show on; the graph still hides itself when the
            // clip has no curves. Its height is left to the item, so the lane can resize.
            Loader {
                id: keyframesBar
                width: parent.width - (audioMixer.visible ? audioMixer.width : 0)
                active: root.keyframesTab && EditorState.selectedTrack >= 0 && EditorState.selectedClip >= 0
                sourceComponent: Component {
                    KeyframeGraph {
                        pxPerSecond: root.pxPerSecond
                        labelsWidth: EditorState.preferences.trackLabelsWidth
                        keyframesTab: root.keyframesTab
                        // Keep keys/playhead lined up with the track scroll view below.
                        contentX: flick.contentX
                        contentWidth: flick.contentWidth
                        // Outlives the graph being unloaded between selections.
                        laneHeight: root.keyframeLaneHeight
                        onLaneHeightChanged: root.keyframeLaneHeight = laneHeight
                    }
                }
            }

            Loader {
                id: subtitleLane
                width: parent.width - (audioMixer.visible ? audioMixer.width : 0)
                active: EditorState.subtitleEditing
                sourceComponent: Component {
                    SubtitleCueLane {
                        pxPerSecond: root.pxPerSecond
                        labelsWidth: EditorState.preferences.trackLabelsWidth
                        contentX: flick.contentX
                        contentWidth: flick.contentWidth
                    }
                }
            }

            Row {
                width: parent.width
                height: parent.height - (keyframesBar.item ? keyframesBar.item.height : 0)
                        - (subtitleLane.item ? subtitleLane.item.height : 0)

            // --- fixed left label column --------------------------------------------
            Column {
                id: labelsColumn
                width: EditorState.preferences.trackLabelsWidth
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
                        color: Theme.c.timelinePanel.headerBorder
                    }
                    Rectangle {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        height: 1
                        color: Theme.c.timelinePanel.headerBorder
                    }
                }

                TrackHeaderColumn {
                    id: trackLabelsArea
                    width: parent.width
                    // Clamped: went negative at small panel heights, which spilled
                    // the absolutely-positioned label rows out of the column.
                    height: Math.max(0, parent.height - Theme.timelineRulerHeight
                                        - Theme.timelineBookmarkRowHeight)
                    labelsWidth: EditorState.preferences.trackLabelsWidth
                    resizable: true
                    tracks: root.tracks
                    contentY: flick.contentY
                    onScrollRequested: (dy) => {
                        flick.contentY = Math.max(0, Math.min(flick.contentHeight - flick.height,
                                                              flick.contentY + dy))
                    }
                }
            }

            // --- scrollable ruler + tracks --------------------------------------------
            Flickable {
                id: flick
                width: parent.width - EditorState.preferences.trackLabelsWidth - (audioMixer.visible ? audioMixer.width : 0)
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
                                        headerHeight + root.totalTracksHeightCached
                                        + Theme.trackGap)
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.horizontal: AppScrollBar { policy: ScrollBar.AlwaysOn }
                ScrollBar.vertical: AppScrollBar { }

                PinchHandler {
                    id: pinch
                    target: null
                    acceptedDevices: PointerDevice.TouchScreen
                    property real startZoom: 1
                    property real anchorSeconds: 0

                    onActiveChanged: {
                        if (!active)
                            return
                        startZoom = root.zoom
                        anchorSeconds = Math.max(0, (flick.contentX + centroid.position.x)
                                                    / root.pxPerSecond)
                    }
                    onScaleChanged: {
                        if (active)
                            root.setZoomAround(startZoom * activeScale, anchorSeconds,
                                               centroid.position.x)
                    }
                }

                Item {
                    id: timelineContent
                    width: flick.contentWidth
                    height: flick.contentHeight

                    // Track-area surface. Same panel colour as the frame behind it,
                    // so a theme can make this layer translucent on its own.
                    Rectangle {
                        anchors.fill: parent
                        color: Theme.c.timelinePanel.background
                        z: -1
                    }

                    // Wheel + middle-click pan: scoped to the ruler so it does not
                    // block timeline drops. Left/right clicks fall through.
                    MouseArea {
                        id: rulerWheelArea
                        width: parent.width
                        y: flick.contentY
                        height: Theme.timelineRulerHeight + Theme.timelineBookmarkRowHeight
                        z: 1
                        acceptedButtons: Qt.MiddleButton
                        preventStealing: true
                        cursorShape: pressed ? Qt.ClosedHandCursor : Qt.ArrowCursor
                        onWheel: (wheel) => root.handleTimelineWheel(wheel)
                        onPressed: (mouse) => root.beginTimelinePan(rulerWheelArea, mouse)
                        onPositionChanged: (mouse) => {
                            if (pressed)
                                root.updateTimelinePan(rulerWheelArea, mouse)
                        }
                    }

                    // Horizontal scroll / zoom wheel over track rows (below the drop overlay).
                    MouseArea {
                        id: tracksWheelArea
                        x: 0
                        y: Theme.timelineRulerHeight + Theme.timelineBookmarkRowHeight
                        width: parent.width
                        height: Math.max(root.totalTracksHeightCached, Theme.trackHeightVideo)
                        z: 150
                        acceptedButtons: Qt.MiddleButton
                        preventStealing: true
                        cursorShape: pressed ? Qt.ClosedHandCursor : Qt.ArrowCursor
                        onWheel: (wheel) => root.handleTimelineWheel(wheel)
                        onPressed: (mouse) => root.beginTimelinePan(tracksWheelArea, mouse)
                        onPositionChanged: (mouse) => {
                            if (pressed)
                                root.updateTimelinePan(tracksWheelArea, mouse)
                        }
                    }

                    // seek strip (ruler + bookmark lane) ------------------------------------
                    // CapCut/Premiere-style: the whole pinned header is one scrub area so
                    // the playhead is easy to move without hunting for a tiny hit target.
                    TimelineSeekStrip {
                        id: seekStrip
                        panel: root
                        view: flick
                        playheadDragging: playhead.headDragging
                        renameDialog: bookmarkRenameDialog
                        renameField: bookmarkRenameField
                    }

                    // Work area overlay across the track rows.
                    TimelineWorkAreaOverlay {
                        panel: root
                        view: flick
                        headerHeight: flick.headerHeight
                    }

                    // A project with no tracks used to render as a completely
                    // blank timeline, with the only route to a first track being
                    // one button among sixteen in the toolbar.
                    TimelineEmptyState {
                        view: flick
                        panel: root
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
                        height: Math.max(root.totalTracksHeightCached + Theme.trackGap,
                                         flick.height - flick.headerHeight)
                        z: 0
                        enabled: root.timelineTool === ""
                        acceptedButtons: Qt.LeftButton
                        // CapCut: empty-space drag is marquee, not flick-scroll. A finger has no
                        // marquee: the same drag pans the timeline instead, so only a real
                        // mouse press sets preventStealing (in onPressed, undone on release).
                        cursorShape: pressed ? Qt.CrossCursor : Qt.ArrowCursor

                        onPressed: (mouse) => {
                            root.forceActiveFocus()
                            if (mouse.source !== Qt.MouseEventNotSynthesized) {
                                if (!(mouse.modifiers & (Qt.ShiftModifier | Qt.ControlModifier)))
                                    EditorState.clearSelection()
                                return
                            }
                            preventStealing = true
                            root.marqueeAdditive = (mouse.modifiers & (Qt.ShiftModifier | Qt.ControlModifier)) !== 0
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
                        onReleased: {
                            preventStealing = false
                            root.endMarquee(true)
                        }
                        onCanceled: {
                            preventStealing = false
                            root.endMarquee(false)
                        }
                    }

                    TransformCoverageOverlay {
                        y: trackColumn.y
                        width: trackColumn.width
                        height: root.totalTracksHeightCached
                        z: 2
                        tracks: root.tracks
                        pxPerSecond: root.pxPerSecond
                        rowTop: root.trackRowTop
                        rowHeight: root.trackHeight
                        draggingTrack: trackLabelsArea.draggingTrackFrom
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
                                // Above the rows below it while one of its clips is dragged, so the clip draws over them.
                                z: trackClipsRenderer.dragging ? 20 : 0
                                property int trackIndex: index
                                // Drawn inside its parent's row instead of getting one here.
                                // Column skips invisible children entirely, so this costs no
                                // spacing either.
                                visible: !root.tracks[trackIndex].isAdjustmentLane
                                width: flick.contentWidth
                                height: root.trackHeight(trackIndex)
                                // Faint row tint on hover, and an empty track now
                                // reads as a track rather than as blank space.
                                color: trackHover.hovered
                                       ? Qt.rgba(Theme.c.timelinePanel.trackBackground.r,
                                                 Theme.c.timelinePanel.trackBackground.g,
                                                 Theme.c.timelinePanel.trackBackground.b, 0.5)
                                       : Qt.rgba(Theme.c.timelinePanel.trackBackground.r,
                                                 Theme.c.timelinePanel.trackBackground.g,
                                                 Theme.c.timelinePanel.trackBackground.b, 0.22)

                                Behavior on color {
                                    ColorAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
                                }

                                HoverHandler { id: trackHover }

                                // A transform layer's row carries a faint wash of its colour.
                                TimelineTrackTransformWash {
                                    panel: root
                                    trackIndex: trackRow.trackIndex
                                }

                                DropArea {
                                    anchors.fill: parent
                                    keys: AssetDrag.allKeys()

                                    // Every asset kind but media goes through the shared router.
                                    // Placeable kinds normally land on the overlay above; here they
                                    // are the Wayland fallback, like media below.
                                    function routedKind(drop) {
                                        const kind = AssetDrag.kindFromKeys(drop.keys)
                                        return kind === "media" ? "" : kind
                                    }

                                    function isEffectDrag(drop) {
                                        return drop.keys.indexOf("application/x-drift-effect") !== -1
                                    }

                                    function isMaskDrag(drop) {
                                        return drop.keys.indexOf("application/x-drift-mask") !== -1
                                    }

                                    // A mask lane only ever masks the track it is nested in, so
                                    // audio (no picture) and adjustment rows (a lane inside a lane
                                    // would have two scopes) take neither drop.
                                    function acceptsMask(trackIndex) {
                                        const track = root.tracks[trackIndex]
                                        return !!track && track.type !== "audio"
                                               && !track.isAdjustmentLane
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
                                        const kind = routedKind(drop)
                                        if (kind.length > 0 && !isTransitionDrag(drop)) {
                                            root.dropRouter.hover(kind, AssetDrag.payloadFromDrop(drop, kind),
                                                                  drop.x, root.trackOffsetY(trackRow.trackIndex) + drop.y)
                                            return
                                        }
                                        if (isTransitionDrag(drop)) {
                                            root.clearLandingPreview()
                                            root.clearEffectDropHighlight()
                                            root.updateTransitionDropPreview(trackRow.trackIndex, drop.x)
                                            return
                                        }
                                        if (isMaskDrag(drop)) {
                                            if (!acceptsMask(trackRow.trackIndex)) {
                                                root.clearLandingOutline()
                                                root.clearEffectDropHighlight()
                                                return
                                            }
                                            root.updateEffectDropHighlight(trackRow.trackIndex, drop.x)
                                            // Over a gap the mask lands as its own lane clip, so
                                            // promise the span the same way a media drop does.
                                            if (root.clipIndexAtPosition(trackRow.trackIndex, drop.x) < 0) {
                                                const at = Math.max(0, drop.x / root.pxPerSecond)
                                                root.showLandingPreview(trackRow.trackIndex, at, 5.0)
                                            } else {
                                                root.clearLandingOutline()
                                            }
                                            return
                                        }
                                        if (isEffectDrag(drop) || isAudioEffectDrag(drop)) {
                                            root.updateEffectDropHighlight(trackRow.trackIndex, drop.x)
                                            if (isEffectDrag(drop) && root.tracks[trackRow.trackIndex].type === "video"
                                                    && root.clipIndexAtPosition(trackRow.trackIndex, drop.x) < 0) {
                                                const desired = Math.max(0, drop.x / root.pxPerSecond)
                                                root.showLandingPreview(trackRow.trackIndex, desired, 5.0)
                                            } else {
                                                root.clearLandingOutline()
                                            }
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
                                        root.clearTransitionDropPreview()
                                    }
                                    onDropped: (drop) => {
                                        drop.accept(Qt.CopyAction)
                                        const kind = routedKind(drop)
                                        if (kind.length > 0 && !isTransitionDrag(drop)) {
                                            root.dropRouter.drop(kind, AssetDrag.payloadFromDrop(drop, kind),
                                                                 AssetDrag.labelFromDrop(drop, kind), drop.x,
                                                                 root.trackOffsetY(trackRow.trackIndex) + drop.y)
                                            return
                                        }
                                        if (isTransitionDrag(drop)) {
                                            const kind = drop.getDataAsString("application/x-drift-transition")
                                            root.applyTransitionDrop(trackRow.trackIndex, drop.x, kind)
                                            return
                                        }
                                        if (isMaskDrag(drop)) {
                                            const maskId = drop.getDataAsString("application/x-drift-mask")
                                            const clipIndex = root.clipIndexAtPosition(trackRow.trackIndex, drop.x)
                                            root.clearEffectDropHighlight()
                                            root.clearLandingOutline()
                                            if (maskId.length === 0 || !acceptsMask(trackRow.trackIndex))
                                                return
                                            // No selectClip here, unlike the effect branch: both
                                            // calls select the mask clip they minted, which is
                                            // what opens its inspector and preview handles.
                                            if (clipIndex >= 0) {
                                                EditorState.addMaskToClip(trackRow.trackIndex, clipIndex, maskId)
                                            } else {
                                                const atSec = Math.max(0, drop.x / root.pxPerSecond)
                                                EditorState.addMaskLaneClip(trackRow.trackIndex, maskId, atSec)
                                            }
                                            return
                                        }
                                        if (isEffectDrag(drop)) {
                                            const effectId = drop.getDataAsString("application/x-drift-effect")
                                            const clipIndex = root.clipIndexAtPosition(trackRow.trackIndex, drop.x)
                                            root.clearEffectDropHighlight()
                                            root.clearLandingOutline()
                                            // No selectClip: addEffect selects the adjustment it
                                            // put the stack on, which is where the Effects tab is.
                                            if (clipIndex >= 0 && effectId.length > 0) {
                                                EditorState.addEffect(trackRow.trackIndex, clipIndex, effectId)
                                            } else if (effectId.length > 0 && root.tracks[trackRow.trackIndex].type === "video") {
                                                const atSec = Math.max(0, drop.x / root.pxPerSecond)
                                                EditorState.addAdjustmentClipWithEffect(effectId, trackRow.trackIndex, atSec)
                                            }
                                            return
                                        }
                                        if (isAudioEffectDrag(drop)) {
                                            const effectId = drop.getDataAsString("application/x-drift-audio-effect")
                                            const clipIndex = root.clipIndexAtPosition(trackRow.trackIndex, drop.x)
                                            root.clearEffectDropHighlight()
                                            if (clipIndex >= 0 && effectId.length > 0)
                                                EditorState.addAudioEffect(trackRow.trackIndex, clipIndex, effectId)
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
                                TimelineLandingOutline {
                                    panel: root
                                    trackIndex: trackRow.trackIndex
                                }

                                // Where a dragged transition would land. Above the transition
                                // regions so it stays visible over an existing one it replaces.
                                TimelineTransitionDropOutline {
                                    panel: root
                                    trackIndex: trackRow.trackIndex
                                }

                                // Nested adjustment lanes, drawn as strips across the top of
                                // this row. A lane is a track in its own right — that is what
                                // keeps (trackIndex, clipIndex) addressing flat everywhere else —
                                // but it has no row of its own, so it is rendered in here.
                                Column {
                                    id: adjustmentLaneStrips
                                    width: trackRow.width
                                    spacing: 0
                                    z: 4

                                    Repeater {
                                        // From the track's clip model, which only re-announces
                                        // the list when a lane actually comes or goes. A JS
                                        // array here rebuilt every lane and its clips per edit.
                                        model: EditorState.clipsModel(trackRow.trackIndex).adjustmentLanes
                                        delegate: TimelineAdjustmentLaneStrip {
                                            panel: root
                                            timelineColumn: trackColumn
                                            viewState: timelineViewState
                                            stripWidth: trackRow.width
                                        }
                                    }
                                }

                                // The track's own clips sit below the strips. This wrapper is
                                // what TimelineClipItem reads as its `trackRow` (it takes its
                                // parent), so the clips size themselves to the space left over
                                // without knowing lanes exist.
                                Item {
                                    id: trackClipArea
                                    // Carried explicitly, because TimelineClipItem takes this
                                    // Item as its own `trackRow` property — which shadows the
                                    // outer `trackRow` id at the binding below. Reaching for
                                    // trackRow.trackIndex there silently resolves to this Item,
                                    // and an Item without the property yields 0, so every track
                                    // rendered track 0's clips.
                                    property int trackIndex: trackRow.trackIndex
                                    y: adjustmentLaneStrips.height
                                    width: trackRow.width
                                    height: Math.max(0, trackRow.height - adjustmentLaneStrips.height)

                                    TimelineTrackArea {
                                        id: trackClipsRenderer
                                        anchors.fill: parent
                                        panel: root
                                        timelineColumn: trackColumn
                                        trackIndex: trackClipArea.trackIndex
                                        viewState: timelineViewState
                                    }

                                    // Live voiceover recording indicator and waveform on this track
                                    TimelineLiveRecordingBlock {
                                        panel: root
                                        trackIndex: trackClipArea.trackIndex
                                    }
                                }

                                // Right-click a gap to ripple everything after it (and any
                                // linked partner clips on other tracks) left to close it. The
                                // menu is one per panel; gaps only carry a hover tint.
                                Repeater {
                                    model: EditorState.clipsModel(trackRow.trackIndex).gaps
                                    delegate: Item {
                                        id: gapItem
                                        required property var modelData
                                        x: modelData.start * root.pxPerSecond
                                        width: (modelData.end - modelData.start) * root.pxPerSecond
                                        height: trackRow.height
                                        z: 1

                                        TimelineGapHover {
                                            hovered: gapMouse.containsMouse
                                        }

                                        MouseArea {
                                            id: gapMouse
                                            anchors.fill: parent
                                            hoverEnabled: true
                                            acceptedButtons: Qt.RightButton
                                            onPressed: root.openGapMenu(trackRow.trackIndex,
                                                                        gapItem.modelData.start)
                                        }

                                        // A handler rather than a left-button MouseArea, so the press
                                        // still reaches the marquee/pan area underneath.
                                        TapHandler {
                                            acceptedDevices: PointerDevice.TouchScreen
                                            onLongPressed: root.openGapMenu(trackRow.trackIndex,
                                                                            gapItem.modelData.start,
                                                                            gapItem, point.position.x,
                                                                            point.position.y)
                                        }
                                    }
                                }

                                // Transition overlap regions (purple) — above clips for hit-testing.
                                // One delegate per region that exists, computed in C++, instead of
                                // one per clip each scanning the whole track for its partner.
                                Repeater {
                                    model: EditorState.clipsModel(trackRow.trackIndex).transitionRegions
                                    delegate: TimelineTransitionRegion {
                                        panel: root
                                        trackIndex: trackRow.trackIndex
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
                        height: Math.max(root.totalTracksHeightCached,
                                         flick.contentHeight - flick.headerHeight)
                        z: 250
                        // "text/uri-list" is a drag from outside the app — the file manager,
                        // usually. It carries files that have not been imported yet, where
                        // "text/plain" carries the index of a row already in the bin.
                        keys: ["text/plain", "text/uri-list"].concat(AssetDrag.placeableKeys())

                        function isFileDrag(drop) {
                            return drop.keys.indexOf("text/uri-list") !== -1
                        }

                        // Shapes, stickers, emoji, text styles and adjustment layers: placed like
                        // media, new-track bands and the empty space below the tracks included.
                        function placeableKind(drop) {
                            const kind = AssetDrag.kindFromKeys(drop.keys)
                            return kind !== "media" && EditorState.isPlaceableDropKind(kind) ? kind : ""
                        }

                        function assetIndexFromDrop(drop) {
                            if (EditorState.draggingAssetIndex >= 0)
                                return EditorState.draggingAssetIndex
                            const text = drop.hasText ? drop.text : drop.getDataAsString("text/plain")
                            const idx = parseInt(text)
                            return isNaN(idx) ? -1 : idx
                        }

                        function updateAssetDrag(drop) {
                            if (isFileDrag(drop)) {
                                root.updateUrlDropPreview(drop)
                                return
                            }
                            const kind = placeableKind(drop)
                            if (kind.length > 0) {
                                root.dropRouter.hover(kind, AssetDrag.payloadFromDrop(drop, kind), drop.x, drop.y)
                                return
                            }
                            const assetIndex = assetIndexFromDrop(drop)
                            if (assetIndex < 0) {
                                root.clearLandingPreview()
                                return
                            }
                            root.updateAssetDropPreview(assetIndex, drop.x, drop.y)
                        }

                        onEntered: (drop) => updateAssetDrag(drop)
                        onPositionChanged: (drop) => updateAssetDrag(drop)
                        onExited: {
                            root.clearUrlDropState()
                            root.clearLandingPreview()
                        }
                        onDropped: (drop) => {
                            drop.accept(Qt.CopyAction)
                            if (isFileDrag(drop)) {
                                // Not cleared first, unlike the branch below: performUrlDrop
                                // keeps the outline on screen at the frozen spot for as long as
                                // the import it starts is still running.
                                root.performUrlDrop(drop.hasUrls ? drop.urls : [], drop.x, drop.y)
                                return
                            }
                            const kind = placeableKind(drop)
                            if (kind.length > 0) {
                                root.dropRouter.drop(kind, AssetDrag.payloadFromDrop(drop, kind),
                                                     AssetDrag.labelFromDrop(drop, kind), drop.x, drop.y)
                                return
                            }
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
                    TimelineNewTrackGhost {
                        panel: root
                    }

                    // CapCut-style selection box drawn while marquee-dragging.
                    TimelineMarqueeRect {
                        panel: root
                    }

                    // snap guide -------------------------------------------------------------
                    TimelineSnapGuide {
                        panel: root
                    }

                    // playhead ---------------------------------------------------------------
                    TimelinePlayhead {
                        id: playhead
                        panel: root
                        view: flick
                        interpolator: needle
                    }

                    // cut tools ----------------------------------------------------------------
                    // Sits above the tracks and playhead so it both draws the red
                    // dashed virtual playhead (and, for the trim tools, the red
                    // gradient over the doomed side) and intercepts the click.
                    TimelineCutOverlay {
                        panel: root
                    }
                }
            }

            // --- right-docked audio mixer strip --------------------------------------
            AudioMixerStrip {
                id: audioMixer
                height: parent.height
                visible: width > 0 && !root.externalMixerShown
            }
        }
        } // Column (keyframes + tracks)
    }

    Connections {
        target: EditorState
        // A drag abandoned outside the timeline never fires onExited/onDropped
        // on any DropArea here, so the landing outline used to stay painted.
        function onDraggingAssetIndexChanged() {
            if (EditorState.draggingAssetIndex < 0)
                root.clearLandingPreview()
        }
    }

    // One gap menu for the whole timeline, rather than one built up front inside every gap.
    property int gapMenuTrack: -1
    property real gapMenuStart: 0
    function openGapMenu(trackIndex, start, anchor, ax, ay) {
        gapMenuTrack = trackIndex
        gapMenuStart = start
        if (anchor)
            gapContextMenu.popup(anchor, ax, ay)
        else
            gapContextMenu.popup()
    }

    ThemedContextMenu {
        id: gapContextMenu
        ThemedMenuItem {
            text: qsTr("Close Gap")
            icon.name: Theme.icons.chevronsRightLeft
            onTriggered: EditorState.closeGap(root.gapMenuTrack, root.gapMenuStart)
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
            if (seekStrip.bookmarkRenameIndex < 0)
                return
            const bookmarks = EditorState.bookmarks
            if (seekStrip.bookmarkRenameIndex >= bookmarks.length)
                return
            const seconds = bookmarks[seekStrip.bookmarkRenameIndex].seconds
            const label = bookmarkRenameField.text.trim()
            EditorState.updateBookmark(seekStrip.bookmarkRenameIndex, seconds,
                                       label.length > 0 ? label : qsTr("Bookmark"))
            seekStrip.bookmarkRenameIndex = -1
        }
        onRejected: seekStrip.bookmarkRenameIndex = -1
    }

    ThemedDialog {
        id: convertToSubtitleDialog
        title: qsTr("Convert to subtitle?")
        acceptText: qsTr("Convert")
        preferredWidth: Theme.dialogWidthSm

        contentItem: ThemedLabel {
            width: parent ? parent.width : Theme.dialogWidthSm
            wrapMode: Text.WordWrap
            size: "sm"
            text: qsTr("The selected text clips will be replaced by one subtitle clip. Every caption will use the position and style of the first text clip.")
        }

        onAccepted: EditorState.convertSelectionToSubtitle()
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
}
