import QtQuick
import Drift

// Fixed left-hand track header column: per-track mute/hide/clip-display toggles,
// name, reorder-by-drag handle and the track context menu. Owns the
// track-drag state; the panel only supplies the track list and vertical scroll.
Item {
    id: root

    // Track model (EditorState.tracks) and the timeline's live vertical scroll,
    // so headers stay aligned with their rows.
    property var tracks: []
    property real contentY: 0
    // Desktop passes EditorState.preferences.trackLabelsWidth; Android a narrower fixed value.
    property real labelsWidth: Theme.trackLabelsWidth
    // Phone: grip + mute/hide only (no type glyph / waveform / name band).
    property bool compact: false
    // Touch shell: reorder has to be asked for, and the context menu needs a
    // route that is not the right mouse button.
    property bool touchMode: false
    // The hold-to-reorder / hold-for-menu gestures, which a touchscreen needs even when
    // the desktop layout is showing.
    readonly property bool touchHold: touchMode || Theme.touchInput
    // A handle on the right edge drags EditorState.preferences.trackLabelsWidth.
    property bool resizable: false
    // Resizing writes the phone's own width preference.
    property bool phone: false

    // Transform layer brackets. Desktop reserves a gutter on the left for them (one indent per
    // nesting level); the compact header draws them over the grip instead.
    readonly property int bracketLevels: {
        let levels = 0
        for (let i = 0; i < tracks.length; ++i) {
            if (tracks[i].isTransformLayer)
                levels = Math.max(levels, (tracks[i].spanDepth || 0) + 1)
        }
        return levels
    }
    readonly property real bracketIndent: compact ? 4 : 8
    property real bracketGutter: compact ? 0 : bracketLevels * bracketIndent
    Behavior on bracketGutter {
        NumberAnimation { duration: Theme.durationBase; easing.type: Theme.easing }
    }
    // A foot being dragged: the layer and the end it would snap to, for the header tint.
    property int spanPreviewLayer: -1
    property int spanPreviewEnd: -1
    signal scrollRequested(real dy)

    // Track-header reorder: source index and live drop target while dragging,
    // plus the insertion boundary the indicator line is drawn at.
    property int draggingTrackFrom: -1
    property int draggingTrackTo: -1
    property int draggingTrackSlot: -1

    onDraggingTrackToChanged: Haptics.lane(draggingTrackTo)

    // Pending delete confirmation — index kept until Accept/Reject so the menu
    // can close without wiping the track immediately.
    property int pendingDeleteTrack: -1
    // Same idea for the rename dialog.
    property int pendingRenameTrack: -1

    clip: true

    TrackHeaderRenameDialog {
        id: renameTrackDialog
        column: root
    }

    TrackHeaderDeleteDialog {
        id: confirmDeleteTrack
        column: root
    }

    // The one definition of a row's height, shared with TimelinePanel and AndroidTimeline. It
    // used to be duplicated in all three, which had to agree or the headers slid out of line
    // with their rows; the nested-lane rule made keeping three copies in step untenable.
    function trackHeight(index) {
        // Reading `tracks` is what gives callers' bindings a dependency to re-evaluate on —
        // EditorState.trackRowHeight is a plain call and carries none of its own.
        const dep = tracks.length
        return EditorState.trackRowHeight(index, {
            "video": Theme.trackHeightVideo,
            "audio": Theme.trackHeightAudio,
            "text": Theme.trackHeightText,
            "subtitle": Theme.trackHeightSubtitle,
            "shape": Theme.trackHeightShape,
            "adjustment": Theme.trackHeightAdjustment,
            "lane": Theme.adjustmentLaneHeight,
            "ribbon": compact ? Theme.adjustmentLaneHeight : Theme.adjustmentRibbonHeight
        })
    }

    // Raise a row so N channel lanes each get a workable height. Only ever grows, and only
    // on enable: silently shrinking a row the user has already tuned is worse than leaving it
    // tall, and on disable they may well want the height they now have.
    //
    // Computed here rather than in C++ because the row-height metrics are owned by Theme and
    // passed *into* trackRowHeight — C++ deliberately hardcodes none of them.
    function growRowForChannelLanes(index, channels) {
        if (channels < 2)
            return
        const base = tracks[index].type === "audio" ? Theme.trackHeightAudio
                                                    : Theme.trackHeightVideo
        // The waveform sits below the header band, inside the selection ring.
        const chrome = Theme.clipHeaderBandHeight + 2 * Theme.clipSelectionRingWidth
        const wanted = (16 * channels + chrome) / base
        if (wanted > (tracks[index].heightScale || 1))
            EditorState.setTrackHeightScale(index, wanted)
    }

    // A nested lane has no header of its own: it belongs to the row above it, whose header
    // already names it.
    function trackOccupiesARow(index) {
        return index >= 0 && index < tracks.length && !tracks[index].isAdjustmentLane
    }

    // Video-track clip display modes, matching Track::ClipDisplay.
    function clipDisplayGlyph(mode) {
        if (mode === 0) return Theme.icons.film
        if (mode === 2) return Theme.icons.audioLines
        return Theme.icons.panelBottomDashed
    }

    function clipDisplayLabel(mode) {
        if (mode === 0) return qsTr("Video")
        if (mode === 2) return qsTr("Waveform")
        return qsTr("Video + waveform")
    }

    function isHeightPreset(index, scale) {
        return Math.abs((tracks[index].heightScale || 1) - scale) < 0.01
    }

    // Header button order: Video -> Video + waveform -> Waveform -> Video.
    function nextClipDisplay(mode) {
        return mode === 0 ? 1 : (mode === 1 ? 2 : 0)
    }

    // Single-letter stand-in for the type glyph plus name band, which do not fit a 72px
    // compact header. "V1"/"A2" is the fallback identification a phone gets until the track
    // has a custom name — see trackCompactLabel, which prefers that name when it's set.
    function trackTypeShortLabel(type, isTransform, isCamera) {
        if (isCamera) return qsTr("CAM");
        if (isTransform) return qsTr("TF");
        if (type === "adjustment") return qsTr("FX");
        if (type === "audio") return qsTr("A");
        if (type === "text") return qsTr("T");
        if (type === "subtitle") return qsTr("S");
        if (type === "shape") return qsTr("G");
        return qsTr("V");
    }

    // Human label for a track type.
    function trackTypeLabel(type, isTransform, isCamera) {
        if (isCamera) return qsTr("Camera");
        if (isTransform) return qsTr("Transform");
        if (type === "adjustment") return qsTr("Adjustment");
        if (type === "audio") return qsTr("Audio");
        if (type === "text") return qsTr("Text");
        if (type === "subtitle") return qsTr("Subtitle");
        if (type === "shape") return qsTr("Graphic");
        return qsTr("Video");
    }

    // Track numbers are scoped to their media type rather than the absolute row.
    // A project with Video 1 followed by its first extracted audio lane should read
    // Audio 1, not Audio 2. Reordering mixed track types keeps each sequence natural.
    function trackTypeOrdinal(index) {
        if (index < 0 || index >= tracks.length)
            return 1
        // Transform layers are adjustment tracks by type but count on their own, so an
        // adjustment and a transform layer never share a number.
        const type = tracks[index].type
        const transform = tracks[index].isTransformLayer === true
        var ordinal = 0
        for (var i = 0; i <= index; i++) {
            if (tracks[i].type === type && (tracks[i].isTransformLayer === true) === transform)
                ordinal++
        }
        return Math.max(1, ordinal)
    }

    function trackRowTop(index) {
        var cursor = 0
        for (var i = 0; i < index && i < tracks.length; i++) {
            if (!trackOccupiesARow(i))
                continue
            cursor += trackHeight(i) + Theme.trackGap
        }
        return cursor
    }

    // Boundary the dragged row would be inserted at, 0..tracks.length. Note the
    // open upper end: the old version clamped to tracks.length - 1, so "after
    // the last track" was not expressible and a downward drag could not reach
    // the bottom slot.
    function trackInsertSlotAtY(y) {
        var cursor = 0
        for (var i = 0; i < tracks.length; i++) {
            if (!trackOccupiesARow(i))
                continue
            const th = trackHeight(i)
            if (y < cursor + th / 2)
                return i
            cursor += th + Theme.trackGap
        }
        return tracks.length
    }

    // QList::move() destination for dragging `from` into that slot. Removing the
    // row first shifts everything after it up by one, so a downward move lands a
    // slot earlier than the raw boundary — without this the drag committed a
    // move after a single pixel, because a row's own grip already sits past its
    // own midpoint.
    function trackMoveTargetForSlot(from, slot) {
        const to = slot > from ? slot - 1 : slot
        return Math.max(0, Math.min(tracks.length - 1, to))
    }

    // "Video 1 only", "Video 1 to Text 2", "Everything below" — built here so the track names
    // match the headers.
    function spanOptionLabel(option) {
        if (option.kind === "all")
            return qsTr("Everything below")
        const last = trackNameAt(option.endIndex)
        if (option.kind === "only")
            return qsTr("%1 only").arg(last)
        return qsTr("%1 to %2").arg(trackNameAt(option.firstIndex)).arg(last)
    }

    function trackNameAt(i) {
        if (i < 0 || i >= tracks.length)
            return ""
        if (tracks[i].name && tracks[i].name.length > 0)
            return tracks[i].name
        if (tracks[i].isCameraLayer === true)
            return trackTypeLabel(tracks[i].type, false, true)
        return trackTypeLabel(tracks[i].type, tracks[i].isTransformLayer) + " " + trackTypeOrdinal(i)
    }

    function selectCoveredClips(layerIndex) {
        const clips = tracks[layerIndex].clips || []
        const picked = []
        const seen = {}
        for (let c = 0; c < clips.length; ++c) {
            const covered = EditorState.transformLayerCoveredClips(layerIndex, c)
            for (let k = 0; k < covered.length; ++k) {
                const key = covered[k].track + ":" + covered[k].clip
                if (!seen[key]) {
                    seen[key] = true
                    picked.push(covered[k])
                }
            }
        }
        if (picked.length > 0)
            EditorState.setSelection(picked)
    }

    function clearTrackDrag() {
        draggingTrackFrom = -1
        draggingTrackTo = -1
        draggingTrackSlot = -1
    }

    Repeater {
        model: root.tracks.length
        delegate: TrackHeaderRow {
            column: root
            renameDialog: renameTrackDialog
            deleteDialog: confirmDeleteTrack
        }
    }

    // One bracket per transform layer, over the rows and in the gutter they leave.
    Repeater {
        model: root.tracks.length
        delegate: TransformSpanBracket {
            required property int index
            width: root.width
            height: root.height
            z: 20
            tracks: root.tracks
            layerIndex: root.tracks[index].isTransformLayer ? index : -1
            contentY: root.contentY
            indentStep: root.bracketIndent
            touchMode: root.touchMode
            rowTop: root.trackRowTop
            rowHeight: root.trackHeight
            onPreviewEndChanged: {
                if (previewEnd >= 0) {
                    root.spanPreviewLayer = index
                    root.spanPreviewEnd = previewEnd
                } else if (root.spanPreviewLayer === index) {
                    root.spanPreviewLayer = -1
                    root.spanPreviewEnd = -1
                }
            }
            onScrollRequested: (dy) => root.scrollRequested(dy)
        }
    }

    // Insertion line while reordering tracks.
    TrackHeaderInsertLine {
        column: root
    }

    // Drag to widen the header column; double-click restores the default.
    TrackHeaderResizeHandle {
        resizable: root.resizable
        phone: root.phone
    }
}
