import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Window
import Drift
// CapCut-style phone timeline: multi-track, seek, select/move/trim/blade, pinch zoom,
// plus the keyframe, subtitle-cue and beat lanes and transition chrome.
// Omits the marquee and library drops — neither has a touch gesture; box-select is
// replaced by the long-press multi-select mode below.
// Edit tools live in AndroidClipToolbar (above the bottom rail), not on this panel.
Item {
    id: root

    // The window is edge-to-edge, and in landscape the system nav bar lands on one
    // side of this pane — over the track headers on one rotation and over the
    // scrollbar on the other. Read off root: insetting the content Column would move
    // it clear of the unsafe area and zero the very margins that put it there.
    readonly property real leftInset: SafeArea.margins.left
    readonly property real rightInset: SafeArea.margins.right

    property real zoom: 1.0

    // Keyframe lane: opened from the lane bar, and only offered when the selected
    // clip has something animated. KeyframeGraph gates itself on keyframesTab, so
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
    signal subtitleCueTapped(int index)

    function openClipProperties() {
        openPropertiesRequested()
    }

    property int renameClipTrack: -1
    property int renameClipIndex: -1
    property int savePresetTrack: -1
    property int savePresetClip: -1

    function requestConvertTextToSubtitle() {
        convertToSubtitleDialog.open()
    }

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

    // === Voiceover control bar (top-right of mobile timeline) ===
    VoiceoverControlBar {
        id: voiceoverBar
        anchors.top: parent.top
        anchors.topMargin: 4
        anchors.right: parent.right
        anchors.rightMargin: Math.max(8, root.rightInset + 8)
        z: 200
        visible: EditorState.isRecordingAudio
        opacity: visible ? 1 : 0
        Behavior on opacity {
            NumberAnimation { duration: 180 }
        }
    }

    // One gap menu for the whole timeline, rather than one built up front inside every gap.
    property int gapMenuTrack: -1
    property real gapMenuStart: 0
    function openGapMenu(trackIndex, start) {
        gapMenuTrack = trackIndex
        gapMenuStart = start
        gapContextMenu.popup()
    }

    ThemedContextMenu {
        id: gapContextMenu
        ThemedMenuItem {
            text: qsTr("Close gap")
            icon.name: Theme.icons.chevronsRightLeft
            onTriggered: EditorState.closeGap(root.gapMenuTrack, root.gapMenuStart)
        }
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

    // Matches TimelinePanel's floor. 0.05 was 2.5 px/second: a ten-minute project was
    // 1500px against a ~330px viewport with no way to see it end to end.
    readonly property real minZoom: 0.0001
    readonly property real maxZoom: 40.0
    readonly property real pxPerSecond: Theme.pixelsPerSecondBase * zoom
    onPxPerSecondChanged: EditorState.setSnapRadius(14, pxPerSecond)
    // Trailing runway after the last clip: a constant strip of viewport, not a fixed
    // number of seconds, so it does not become 10000px of dead scroll when zoomed in.
    readonly property real timelineEndPadPx: Math.max(
        Theme.timelineEndPadMinPx, flick.width * Theme.timelineEndPadFraction)

    // --- Fixed centre playhead -------------------------------------------------------
    // Time and horizontal scroll are the same quantity here: the head is painted at the
    // viewport centre and the content moves under it.
    //
    //     contentX == playheadSeconds * pxPerSecond - flick.width / 2
    //
    // The half-viewport of slack at each end is Flickable.leftMargin/rightMargin, NOT
    // padding baked into contentWidth: margins extend the scrollable range without
    // shifting content coordinates, so every `x: t * pxPerSecond` binding — including the
    // ones in TimelineClipItem, which desktop shares — is untouched by this.
    readonly property real minContentX: -flick.leftMargin
    readonly property real maxContentX: Math.max(
        minContentX, flick.contentWidth - flick.width + flick.rightMargin)

    function clampContentX(x) {
        return Math.max(minContentX, Math.min(maxContentX, x))
    }

    readonly property real viewCentreSeconds:
        Math.max(0, (flick.contentX + flick.width / 2) / pxPerSecond)

    // The conflict between "time drives scroll" and "scroll drives time" is removed
    // rather than arbitrated: exactly one of them is live at any moment. While the user
    // owns the view the Binding below is inert; the rest of the time it is the only
    // writer of contentX. scrollLocked is in the set because dragEdgeScroll writes
    // contentX directly during a clip drag, and without it that would drag the playhead
    // along with the clip.
    readonly property bool userDrivingView: flick.dragging || flick.flicking
                                            || pinch.active || root.scrollLocked

    // No anchor argument any more: the anchor is the playhead, and the playhead is the
    // centre of the viewport by construction. Set the scale and contentX follows.
    function setZoom(newZoom) {
        const z = Math.max(minZoom, Math.min(maxZoom, newZoom))
        if (z === zoom)
            return
        zoom = z
        filmstripRefreshEpoch++
    }

    function fitZoom() {
        if (!(EditorState.durationSeconds > 0)) {
            zoom = 1.0
            filmstripRefreshEpoch++
            return
        }
        const usable = Math.max(Math.max(flick.width, 1) - timelineEndPadPx, 1)
        const fit = usable / (EditorState.durationSeconds * Theme.pixelsPerSecondBase)
        zoom = Math.max(minZoom, Math.min(maxZoom, fit))
        filmstripRefreshEpoch++
    }
    readonly property real labelsWidth: Theme.androidTrackLabelsWidth

    // Beat analysis is triggered from AndroidMoreToolsSheet (the beat toggle, which sets
    // beatGridVisible / onsetsVisible and calls analyzeBeats over the whole timeline);
    // this panel only draws what came back.
    readonly property var beatData: EditorState.beatAnalysis
    readonly property bool beatGridOn: EditorState.beatGridVisible && !!beatData
                                       && !!beatData.beats && beatData.beats.length > 0
    readonly property bool onsetsOn: EditorState.onsetsVisible && !!beatData
                                     && !!beatData.onsets && beatData.onsets.length > 0
    readonly property bool beatLaneVisible: beatGridOn || onsetsOn
    readonly property real beatLaneHeight: beatLaneVisible ? 14 : 0

    function lowerBound(list, value, key) {
        var lo = 0
        var hi = list.length
        while (lo < hi) {
            const mid = (lo + hi) >> 1
            if ((key ? list[mid][key] : list[mid]) < value)
                lo = mid + 1
            else
                hi = mid
        }
        return lo
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
            const t0 = (flick.contentX - 3) / root.pxPerSecond
            const t1 = (flick.contentX + w + 3) / root.pxPerSecond
            for (var i = Math.max(0, root.lowerBound(a.beats, t0) - 1); i < a.beats.length; i++) {
                if (a.beats[i] > t1)
                    break
                const px = localX(a.beats[i])
                if (px < -2 || px > w + 2)
                    continue
                const isBar = ((i - first) % perBar + perBar) % perBar === 0
                ctx.fillStyle = String(isBar ? Theme.c.androidTimeline.beatBar : Theme.c.androidTimeline.beatGrid)
                ctx.fillRect(px, isBar ? 0 : h * 0.35, isBar ? 2 : 1, isBar ? h : h * 0.65)
            }
        }
        if (root.onsetsOn) {
            ctx.fillStyle = String(Theme.c.androidTimeline.beatOnset)
            const onsets = a.onsets || []
            const t0 = (flick.contentX - 3) / root.pxPerSecond
            const t1 = (flick.contentX + w + 3) / root.pxPerSecond
            for (var j = Math.max(0, root.lowerBound(onsets, t0, "seconds") - 1); j < onsets.length; j++) {
                if (onsets[j].seconds > t1)
                    break
                if (root.beatGridOn && onsets[j].nearBeat)
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
    // What the scene-graph clip renderer reads from this panel, bound once here instead of once
    // per clip.
    TimelineViewState {
        id: timelineViewState
        viewX: root.timelineViewX
        viewW: root.timelineViewW
        pxPerSecond: root.pxPerSecond
        touchMode: true
        multiSelectActive: root.multiSelectActive === true
        totalTracksHeight: root.totalTracksHeight()
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
            "clipVideo": Theme.c.androidTimeline.clipVideo,
            "clipAudio": Theme.c.androidTimeline.clipAudio,
            "clipText": Theme.c.androidTimeline.clipText,
            "clipSubtitle": Theme.c.androidTimeline.clipSubtitle,
            "clipGraphic": Theme.c.androidTimeline.clipGraphic,
            "clipEffect": Theme.c.androidTimeline.clipEffect,
            "clipComposite": Theme.c.androidTimeline.clipComposite,
            "adjustmentVideo": Theme.c.androidTimeline.adjustmentVideo,
            "adjustmentAudio": Theme.c.androidTimeline.adjustmentAudio,
            "adjustmentMask": Theme.c.androidTimeline.adjustmentMask,
            "adjustmentTransform": Theme.c.androidTimeline.adjustmentTransform,
            "primary": Theme.c.androidTimeline.primary,
            "scrim": Theme.c.androidTimeline.scrim,
            "proxyBand": Theme.c.androidTimeline.proxyBand,
            "onMedia": Theme.c.androidTimeline.onMedia,
            "waveform": Theme.c.androidTimeline.waveform,
            "mutedForeground": Theme.c.androidTimeline.mutedForeground,
            "panelBorder": Theme.c.androidTimeline.panelBorder,
            "proxyPill": Theme.c.androidTimeline.proxyPill,
            "proxyPillForeground": Theme.c.androidTimeline.proxyPillForeground,
            "editFriendlyPill": Theme.c.androidTimeline.editFriendlyPill,
            "editFriendlyPillForeground": Theme.c.androidTimeline.editFriendlyPillForeground,
            "warning": Theme.c.androidTimeline.warning,
            "fontFamily": Theme.fontFamily,
            "fontSizeTiny": Theme.fontSizeTiny,
            "fontSizeXs": Theme.fontSizeXs,
            "radiusSm": Theme.c.androidTimeline.radiusSm,
            "radiusXs": Theme.c.androidTimeline.radiusXs,
            "ringWidth": Theme.clipSelectionRingWidth,
            "borderWidthFocus": Theme.c.androidTimeline.borderWidthFocus,
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
            "spanToolTip": qsTr("Transform layer: moves, scales and turns every track its bracket covers")
        })
    }

    PlayheadInterpolator {
        id: needle
        active: root.visible
    }

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
    // Clip type of a non-media drag headed for a new track (see TimelineDropRouter); sizes the
    // ghost lane when there is no asset index to ask.
    property string pendingDropKind: ""
    readonly property TimelineDropRouter dropRouter: TimelineDropRouter { panel: root }
    property int effectDropTrackIndex: -1
    property int effectDropClipIndex: -1

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

    readonly property real timelineViewY: flick.contentY
    readonly property real timelineViewH: flick.height

    // Set while a clip drag owns the gesture, so the Flickable cannot steal it. Autoscroll
    // still moves the view — it writes contentX/contentY directly, which an inactive
    // Flickable honours.
    property bool scrollLocked: false
    property real scrollLockContentX: 0
    function setScrollLocked(locked) {
        if (locked === scrollLocked)
            return
        if (locked) {
            scrollLockContentX = flick.contentX
        } else if (flick.contentX !== scrollLockContentX) {
            // Edge autoscroll moved the view without moving time; unlocking would let the
            // contentX Binding snap it back to the old playhead.
            EditorState.playheadSeconds = EditorState.snapTime(root.viewCentreSeconds)
        }
        scrollLocked = locked
    }

    property bool laneBarShown: false
    readonly property bool laneBarWanted: root.multiSelectActive
                                          || keyframeLane.allSeries.length > 0
    Binding {
        target: root
        property: "laneBarShown"
        value: root.laneBarWanted
        when: !root.scrollLocked
    }

    // Edge autoscroll for an in-progress clip drag. The timeline pane is barely two
    // track rows tall and a few seconds wide on a phone, so without this a clip could
    // only ever be moved to a track and a time already on screen — the drag simply
    // stopped at the viewport edge. Returns the delta actually applied so the caller
    // can advance the dragged clip by the same amount; scrolling alone would only
    // slide it out of view, because MouseArea rewrites the drag target's position on
    // move events and none arrive while the finger is parked at the edge.
    function dragEdgeScroll(dx, dy) {
        const maxY = Math.max(0, flick.contentHeight - flick.height)
        const nx = root.clampContentX(flick.contentX + dx)
        const ny = Math.max(0, Math.min(maxY, flick.contentY + dy))
        const applied = { "x": nx - flick.contentX, "y": ny - flick.contentY }
        flick.contentX = nx
        flick.contentY = ny
        return applied
    }

    function showLandingPreview(trackIndex, desiredStart, duration, excludeClipId) {
        const snapped = snapClipStart(desiredStart, duration, excludeClipId)
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
        // A bin-preview trim shortens what actually lands, so the landing preview matches it.
        return asset.placedDurationSeconds
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
            if (!trackOccupiesARow(i))
                continue
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
        for (var i = 0; i < insertIndex && i < tracks.length; i++) {
            if (!trackOccupiesARow(i))
                continue
            cursor += trackHeight(i) + Theme.trackGap
        }
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

        if (ShellBridge.main && ShellBridge.main.configureAndAddAsset)
            ShellBridge.main.configureAndAddAsset(assetIndex, runAdd)
        else
            runAdd()
    }

    // --- TouchDrag drop target ------------------------------------------------

    Component.onCompleted: {
        TouchDrag.registerTarget(root)
        EditorState.setSnapRadius(14, pxPerSecond)
    }
    Component.onDestruction: TouchDrag.unregisterTarget(root)

    function touchDropContains(sceneX, sceneY) {
        return touchDropPoint(sceneX, sceneY).inside
    }

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
        if (dropEdgeScroll.scrolled) {
            dropEdgeScroll.scrolled = false
            EditorState.playheadSeconds = EditorState.snapTime(root.viewCentreSeconds)
        }
        clearLandingPreview()
        clearEffectDropHighlight()
        pendingDropKind = ""
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
        property bool scrolled: false
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
            if (applied.x !== 0)
                scrolled = true
            // Re-resolve against the view that just moved, or the landing outline
            // would stay pinned to the content the finger has scrolled away from.
            root.updateTouchDrop(TouchDrag.kind, TouchDrag.payload,
                                 TouchDrag.sceneX, TouchDrag.sceneY)
        }
    }

    function trackOffsetY(index) {
        var cursor = 0
        for (var i = 0; i < index && i < tracks.length; i++) {
            if (!trackOccupiesARow(i))
                continue
            cursor += trackHeight(i) + Theme.trackGap
        }
        return cursor
    }

    // Shared with TimelinePanel and TrackHeaderColumn — see the note there on why the rule
    // moved out of QML.
    function trackHeight(index) {
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

    // A nested lane is drawn inside its parent's row rather than taking one of its own, so it
    // must not contribute a gap either.
    function trackOccupiesARow(index) {
        return index >= 0 && index < tracks.length && !tracks[index].isAdjustmentLane
    }

    // Adjustment layers are tinted by what they act on, so a glance at a lane says whether it is
    // grading the picture, treating the audio, or cutting a mask.
    function adjustmentColor(kind) {
        if (kind === "audioEffects") return Theme.c.androidTimeline.adjustmentAudio
        if (kind === "mask") return Theme.c.androidTimeline.adjustmentMask
        if (kind === "transform") return Theme.c.androidTimeline.adjustmentTransform
        return Theme.c.androidTimeline.adjustmentVideo
    }
    function clipColor(type) {
        if (type === "text") return Theme.c.androidTimeline.clipText
        if (type === "subtitle") return Theme.c.androidTimeline.clipSubtitle
        if (type === "audio") return Theme.c.androidTimeline.clipAudio
        if (type === "graphic") return Theme.c.androidTimeline.clipGraphic
        if (type === "effect" || type === "adjustment") return Theme.c.androidTimeline.clipEffect
        if (type === "composite") return Theme.c.androidTimeline.clipComposite
        return Theme.c.androidTimeline.clipVideo
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

    function totalTracksHeight() {
        var h = 0
        var rows = 0
        for (var i = 0; i < tracks.length; i++) {
            if (!trackOccupiesARow(i))
                continue
            h += trackHeight(i)
            if (rows > 0) h += Theme.trackGap
            rows++
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

    function trackIndexAtY(y) {
        var cursor = 0
        for (var i = 0; i < tracks.length; i++) {
            if (!trackOccupiesARow(i))
                continue
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

    Column {
        anchors.fill: parent
        anchors.leftMargin: root.leftInset
        anchors.rightMargin: root.rightInset

        AndroidTimelineLaneBar {
            id: laneBar
            width: parent.width
            visible: root.laneBarShown
            height: visible ? 32 : 0
            multiSelectActive: root.multiSelectActive
            keyframeLaneOpen: root.keyframeLaneOpen
            onKeyframesToggled: root.keyframeLaneOpen = !root.keyframeLaneOpen
            onMultiSelectFinished: root.multiSelectActive = false
        }

        // Both lanes size themselves to zero when they have nothing to show, so the
        // track area only loses height while one is actually open.
        KeyframeGraph {
            id: keyframeLane
            width: parent.width
            pxPerSecond: root.pxPerSecond
            labelsWidth: root.labelsWidth
            keyframesTab: root.keyframeLaneOpen
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
            onCueTapped: index => root.subtitleCueTapped(index)
        }

        // Not a Row: a Row positioner assigns x to every child, which silently
        // overwrote the playhead's centring binding and left it parked at the labels
        // column's right edge — and pushed the Flickable a playhead-width past where
        // its own width expected to start.
        Item {
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
                        width: Theme.c.androidTimeline.labelBorderWidth
                        height: parent.height
                        color: Theme.c.androidTimeline.labelBorder
                    }
                    Rectangle {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        height: Theme.c.androidTimeline.labelBorderWidth
                        color: Theme.c.androidTimeline.labelBorder
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
                    onScrollRequested: (dy) => {
                        flick.contentY = Math.max(0, Math.min(flick.contentHeight - flick.height,
                                                              flick.contentY + dy))
                    }
                }
            }

            // The head is a viewport-fixed overlay, a sibling of the Flickable rather than
            // a child of its content item. `flick` starts after the labels column, so the
            // centre of the *viewport* is labelsWidth + flick.width/2 — measuring from the
            // row's left edge instead is exactly the 88px off-centre bug this replaces.
            //
            // Being outside the content item is also what deletes the four
            // `y: flick.contentY` compensations the old head needed to stay put while the
            // tracks panned vertically.
            AndroidTimelinePlayhead {
                id: playhead
                x: root.labelsWidth + flick.width / 2 - Theme.androidPlayheadCentreWidth / 2
                y: 0
                z: 30
                width: Theme.androidPlayheadCentreWidth
                height: parent.height
            }

            Flickable {
                id: flick
                x: root.labelsWidth
                width: parent.width - root.labelsWidth
                height: parent.height
                contentWidth: Math.max(width, EditorState.durationSeconds * root.pxPerSecond
                                              + root.timelineEndPadPx)
                contentHeight: Math.max(height,
                                        root.seekHeaderHeight + root.totalTracksHeight() + Theme.trackGap)
                clip: true
                interactive: !root.scrollLocked
                boundsBehavior: Flickable.StopAtBounds
                // Half a viewport of slack at each end so both t=0 and t=duration can
                // reach the centre line. Margins, not content padding — see root.
                leftMargin: width / 2
                rightMargin: width / 2
                ScrollBar.horizontal: AppScrollBar { policy: ScrollBar.AsNeeded }
                ScrollBar.vertical: AppScrollBar { policy: ScrollBar.AsNeeded }

                // Time drives scroll, except while the user is driving the view.
                Binding {
                    target: flick
                    property: "contentX"
                    value: root.clampContentX(needle.seconds * root.pxPerSecond
                                              - flick.width / 2)
                    when: !root.userDrivingView
                    restoreMode: Binding.RestoreNone
                }

                // ...and scroll drives time while they are. beginPlayheadSeek already
                // pauses transport for the gesture, so no playheadSecondsChanged from
                // playback can arrive mid-drag to fight this.
                onMovementStarted: root.beginPlayheadSeek()
                onMovementEnded: {
                    EditorState.playheadSeconds = EditorState.snapTime(root.viewCentreSeconds)
                    Haptics.reset()
                    root.endPlayheadSeek()
                }

                onContentXChanged: {
                    // Pinch writes contentX itself to hold the head centred, and a clip
                    // drag's edge autoscroll must move the view without moving time.
                    if (!root.userDrivingView || root.scrollLocked || pinch.active)
                        return
                    const raw = root.viewCentreSeconds
                    root.reportSeekSnap(raw, EditorState.playheadSeconds)
                    EditorState.playheadSeconds = raw
                }

                PinchHandler {
                    id: pinch
                    target: null
                    property real startZoom: 1
                    // Latched, not just rate limited: fingers held spread past the end of the range
                    // keep delivering scale events, and a limiter alone would turn the end of the
                    // zoom into a continuous buzz instead of the single bump it should be.
                    property bool atZoomLimit: false

                    onActiveChanged: {
                        atZoomLimit = false
                        if (active) {
                            startZoom = root.zoom
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
                        root.zoom = next
                        // Anchored on the centre — which is the playhead — the way every
                        // other phone editor does it. The Binding is inert while the pinch
                        // is active, so contentX is written here instead.
                        flick.contentX = root.clampContentX(
                                    EditorState.playheadSeconds * root.pxPerSecond
                                    - flick.width / 2)
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
                            color: Theme.c.androidTimeline.seekBackground
                        }
                        Rectangle {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.bottom: parent.bottom
                            height: Theme.c.androidTimeline.seekBorderWidth
                            color: Theme.c.androidTimeline.seekBorder
                        }

                        AndroidTimelineRuler {
                            id: ruler
                            pxPerSecond: root.pxPerSecond
                            tickStepSeconds: root.tickStepSeconds
                            contentWidth: flick.contentWidth
                            contentX: flick.contentX
                            viewWidth: flick.width
                            formatTick: function(seconds) { return root.formatTick(seconds) }
                        }

                        // Bookmark lane. Tap a flag to jump; hold it for the menu desktop
                        // gets from the right button. Retiming is "Move to playhead"
                        // rather than a drag: this strip has already promised the drag to
                        // the scrubber, and a 10px flag is not a drag target on touch.
                        // preventStealing keeps a tap on a flag from being read as a seek.
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
                                color: Qt.rgba(Theme.c.androidTimeline.primary.r, Theme.c.androidTimeline.primary.g, Theme.c.androidTimeline.primary.b, 0.18)
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
                                delegate: AndroidTimelineBookmarkFlag {
                                    pxPerSecond: root.pxPerSecond
                                    menu: bookmarkContextMenu
                                }
                            }
                        }

                        // Beat / onset markers. Analysis itself is triggered from
                        // AndroidMoreToolsSheet; this lane only appears once a result is in
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

                    TransformCoverageOverlay {
                        y: trackColumn.y
                        width: trackColumn.width
                        height: root.totalTracksHeight()
                        z: 2
                        tracks: root.tracks
                        pxPerSecond: root.pxPerSecond
                        rowTop: root.trackRowTop
                        rowHeight: root.trackHeight
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
                                // Above the rows below it while one of its clips is dragged, so the clip draws over them.
                                z: trackClipsRenderer.dragging ? 20 : 0
                                property int trackIndex: index
                                // A nested lane is drawn inside its parent's row, not given one
                                // of its own. Column skips invisible children, so no gap either.
                                visible: !root.tracks[trackIndex].isAdjustmentLane
                                width: flick.contentWidth
                                height: root.trackHeight(trackIndex)
                                color: Qt.rgba(Theme.c.androidTimeline.track.r,
                                               Theme.c.androidTimeline.track.g,
                                               Theme.c.androidTimeline.track.b, 0.22)

                                // A transform layer's row carries a faint wash of its colour.
                                AndroidTimelineTrackWash {
                                    shown: root.tracks[trackRow.trackIndex].isTransformLayer === true
                                }

                                // A tap on empty track clears the selection. Clips take their
                                // own presses first, and a drag still belongs to the Flickable.
                                TapHandler {
                                    enabled: !root.multiSelectActive
                                    onTapped: EditorState.clearSelection()
                                }

                                // Nested adjustment lanes as strips across the top of this row.
                                Column {
                                    id: adjustmentLaneStrips
                                    width: trackRow.width
                                    spacing: 0
                                    z: 4

                                    Repeater {
                                        model: EditorState.clipsModel(trackRow.trackIndex).adjustmentLanes
                                        delegate: AndroidTimelineAdjustmentStrip {
                                            width: trackRow.width
                                            panel: root
                                            timelineColumn: trackColumn
                                            viewState: timelineViewState
                                        }
                                    }
                                }

                                // The track's own clips sit below the strips. TimelineClipItem
                                // takes its parent as `trackRow`, so they size themselves to
                                // what is left without knowing lanes exist.
                                Item {
                                    id: trackClipArea
                                    // Above the gap presses, which are declared later: the trim
                                    // handles reach outside their clips and a gap tap there would
                                    // clear the selection. The renderer only claims clip areas.
                                    z: 2
                                    // Explicit for the same reason as the desktop panel: this
                                    // Item becomes TimelineClipItem's `trackRow`, shadowing the
                                    // outer id at the binding below.
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
                                        touchMode: true
                                    }

                                    AndroidTimelineLiveRecording {
                                        trackIndex: trackClipArea.trackIndex
                                        pxPerSecond: root.pxPerSecond
                                    }
                                }

                                // Long-press a gap to ripple everything after it left; a tap
                                // clears the selection like any other empty stretch of track.
                                Repeater {
                                    model: EditorState.clipsModel(trackRow.trackIndex).gaps
                                    delegate: AndroidTimelineGapPress {
                                        trackIndex: trackRow.trackIndex
                                        pxPerSecond: root.pxPerSecond
                                        multiSelectActive: root.multiSelectActive
                                        onGapHeld: (trackIndex, startSeconds) =>
                                                   root.openGapMenu(trackIndex, startSeconds)
                                    }
                                }

                                // Transition chrome. Same geometry as the desktop panel,
                                // minus its DropArea — there is no drag-and-drop here, so
                                // a tap on a bare overlap creates the crossfade instead.
                                // No preventStealing: a pan across an overlap must still
                                // reach the Flickable.
                                Repeater {
                                    model: EditorState.clipsModel(trackRow.trackIndex).transitionRegions
                                    delegate: AndroidTimelineTransitionMark {
                                        trackIndex: trackRow.trackIndex
                                        pxPerSecond: root.pxPerSecond
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

                    AndroidTimelineWorkArea {
                        pxPerSecond: root.pxPerSecond
                        seekHeaderHeight: root.seekHeaderHeight
                        tracksHeight: root.totalTracksHeight()
                        viewHeight: flick.height
                    }

                    AndroidTimelineDropGhost {
                        dropTrackIndex: root.dropTrackIndex
                        dropStartSeconds: root.dropStartSeconds
                        dropDurationSeconds: root.dropDurationSeconds
                        pxPerSecond: root.pxPerSecond
                        seekHeaderHeight: root.seekHeaderHeight
                        trackOffsetY: root.dropTrackIndex >= 0
                                      ? root.trackOffsetY(root.dropTrackIndex) : 0
                        trackHeight: root.dropTrackIndex >= 0
                                     ? root.trackHeight(root.dropTrackIndex) : 0
                    }

                    AndroidTimelineNewTrackGhost {
                        panel: root
                        viewX: flick.contentX
                    }

                    AndroidTimelineSnapGuide {
                        snapSeconds: root.snapGuideSeconds
                        pxPerSecond: root.pxPerSecond
                        seekHeaderHeight: root.seekHeaderHeight
                        tracksHeight: root.totalTracksHeight()
                        formatTime: function(seconds) { return root.formatTime(seconds) }
                    }

                }
            }
        }
    }

}
