import QtQuick
import Drift

// Subtitle-cue lane aligned with the timeline (like KeyframeGraph): left gutter matches
// the track labels, the scrolling viewport shares the timeline's contentX / pxPerSecond.
// Each cue is a draggable block — drag the body to move, the edges to resize. Times are
// clip-local; blocks are placed at (clipStart + cue.start).
Item {
    id: root

    property real pxPerSecond: 50
    property real contentX: 0
    // The voice waveform covers the viewport plus a chunk either side, parked on a chunk boundary,
    // rather than the whole media range: at high zoom that range is wider than any texture, and
    // Canvas silently downscales past GL_MAX_TEXTURE_SIZE. It repaints when a boundary is crossed.
    readonly property real paintChunk: 256
    readonly property real paintOriginX: Math.floor(contentX / paintChunk) * paintChunk - paintChunk
    property real contentWidth: 800
    property real labelsWidth: Theme.trackLabelsWidth

    readonly property real minCueDur: 0.1

    // A tap on a cue block, after it has been selected. The phone editor opens its text
    // composer from it.
    signal cueTapped(int index)

    readonly property var clipData: {
        void EditorState.selectedClipData
        return EditorState.selectedClipData
    }
    readonly property bool isSubtitle: !!clipData && clipData.kind === "subtitle"
    readonly property var cues: {
        void EditorState.tracksRevision
        return (clipData && clipData.subtitleCues) ? clipData.subtitleCues : []
    }
    readonly property real clipStart: clipData ? (clipData.start || 0) : 0
    readonly property real clipDuration: clipData ? (clipData.duration || 0) : 0

    // Voice waveform is locked to video/audio clips on the timeline — never to the
    // selected subtitle clip's start/duration (resizing subtitles must not stretch it).
    // Computed in C++: reading EditorState.tracks here made Qt deep-convert every clip map in
    // the project into JS on every edit, to arrive at two numbers.
    readonly property var mediaWaveformRange: {
        void EditorState.tracksRevision
        return EditorState.mediaExtentSeconds()
    }

    // The Repeater renders EditorState.subtitles.cueModel, which only changes rows in place, so a
    // drag never rebuilds the delegates. `displayCues` is the snapshot the drag maths (bounds,
    // snapping, commit) works from; it is refreshed from the live cues only while NOT dragging.
    property bool anyDragging: false
    property var displayCues: []

    // Bumped when a mix lands, to re-run the voice-waveform binding. Assigning the result
    // straight onto `peaks` would replace the binding, so the lane would never ask again
    // after the first one — reopening it, or dropping another clip, would keep the old shape.
    property int waveformRevision: 0

    height: visible ? 46 : 0
    visible: EditorState.subtitleEditing && isSubtitle

    function refreshDisplay() {
        const s = []
        for (let i = 0; i < cues.length; i++)
            s.push({ start: cues[i].start, end: cues[i].end, text: cues[i].text, source: cues[i].source })
        displayCues = s
    }
    onCuesChanged: if (!anyDragging) refreshDisplay()
    Component.onCompleted: refreshDisplay()

    function xForSeconds(localSeconds) {
        return (clipStart + localSeconds) * pxPerSecond
    }

    function beginDrag() {
        anyDragging = true
    }
    function endDrag() {
        anyDragging = false
        refreshDisplay()
    }
    function baseCues() {
        return displayCues
    }

    function loBound(index) {
        const b = baseCues()
        return index > 0 ? b[index - 1].end : 0
    }
    function hiBound(index) {
        const b = baseCues()
        if (index < b.length - 1)
            return b[index + 1].start
        return clipDuration > 0 ? clipDuration : b[index].end + 3600
    }
    // Resolved in C++ so playback does not walk the cues from QML on every frame.
    readonly property int activeCueIndex: EditorState.subtitles.activeCue
    function clampVal(v, lo, hi) {
        return Math.max(lo, Math.min(hi, v))
    }
    function snap(v, excludeIndex) {
        const ph = EditorState.playheadSeconds - clipStart
        const inRange = ph >= 0 && (clipDuration <= 0 || ph <= clipDuration)
        const thr = 6 / Math.max(1e-6, pxPerSecond)
        return EditorState.subtitles.snapCueTime(v, thr, inRange ? ph : -1, clipDuration, [excludeIndex])
    }
    function commitCue(index, newStart, newEnd) {
        const b = baseCues()
        const list = []
        for (let i = 0; i < b.length; i++) {
            if (i === index)
                list.push({ start: newStart, end: newEnd, text: b[i].text, source: b[i].source })
            else
                list.push({ start: b[i].start, end: b[i].end, text: b[i].text, source: b[i].source })
        }
        EditorState.previewSetSubtitleCues(EditorState.selectedTrack, EditorState.selectedClip, list)
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.c.subtitleCueLane.background
        border.width: Theme.c.subtitleCueLane.borderWidth
        border.color: Theme.c.subtitleCueLane.border
    }

    Row {
        anchors.fill: parent

        // Left gutter — matches track-label width so the lane lines up with the tracks.
        SubtitleCueGutter {
            lane: root
        }

        // Timeline-aligned viewport (scrolls with the tracks below).
        Item {
            id: viewport
            width: parent.width - root.labelsWidth
            height: parent.height
            clip: true

            Item {
                id: content
                x: -root.contentX
                width: Math.max(viewport.width + root.contentX, root.contentWidth)
                height: parent.height

                // Clip span backdrop.
                SubtitleCueSpan {
                    lane: root
                }

                // Voice waveform of mixed video/audio timeline audio — positioned and
                // scaled from A/V clips only, so subtitle trim/move does not reshape it.
                SubtitleCueWaveform {
                    lane: root
                    viewItem: viewport
                }

                // Playhead line, aligned with the timeline playhead.
                SubtitleCuePlayhead {
                    lane: root
                }

                Repeater {
                    model: EditorState.subtitles.cueModel

                    delegate: SubtitleCueBlock {
                        lane: root
                    }
                }
            }
        }
    }
}
