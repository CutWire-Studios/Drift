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
    property real contentWidth: 800
    property real labelsWidth: Theme.trackLabelsWidth

    readonly property real minCueDur: 0.1

    readonly property var clip: {
        void EditorState.selectedClipData
        return EditorState.selectedClipData
    }
    readonly property bool isSubtitle: !!clip && clip.kind === "subtitle"
    readonly property var cues: {
        void EditorState.tracks
        return (clip && clip.subtitleCues) ? clip.subtitleCues : []
    }
    readonly property real clipStart: clip ? (clip.start || 0) : 0
    readonly property real clipDuration: clip ? (clip.duration || 0) : 0

    // Voice waveform is locked to video/audio clips on the timeline — never to the
    // selected subtitle clip's start/duration (resizing subtitles must not stretch it).
    readonly property var mediaWaveformRange: {
        void EditorState.tracks
        var start = Number.POSITIVE_INFINITY
        var end = 0
        var tracks = EditorState.tracks
        for (var t = 0; t < tracks.length; t++) {
            var type = tracks[t].type
            if (type !== "video" && type !== "audio")
                continue
            var clips = tracks[t].clips || []
            for (var c = 0; c < clips.length; c++) {
                var s = clips[c].start || 0
                var d = clips[c].duration || 0
                if (d <= 0)
                    continue
                start = Math.min(start, s)
                end = Math.max(end, s + d)
            }
        }
        if (!isFinite(start) || end <= start)
            return { start: 0, duration: 0 }
        return { start: start, duration: end - start }
    }

    // The Repeater always renders `displayCues` (a stable snapshot). We refresh it from the
    // live cues only while NOT dragging, so live preview updates during a drag don't swap
    // the model reference and rebuild the delegates (which would interrupt the drag).
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
            s.push({ start: cues[i].start, end: cues[i].end, text: cues[i].text })
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
    function clampVal(v, lo, hi) {
        return Math.max(lo, Math.min(hi, v))
    }
    function snapTargets(excludeIndex) {
        const arr = []
        const ph = EditorState.playheadSeconds - clipStart
        if (ph >= 0 && (clipDuration <= 0 || ph <= clipDuration))
            arr.push(ph)
        arr.push(0)
        if (clipDuration > 0)
            arr.push(clipDuration)
        const b = baseCues()
        for (let i = 0; i < b.length; i++) {
            if (i === excludeIndex)
                continue
            arr.push(b[i].start)
            arr.push(b[i].end)
        }
        return arr
    }
    function snap(v, targets) {
        const thr = 6 / Math.max(1e-6, pxPerSecond)
        let best = v
        let bestD = thr
        for (let i = 0; i < targets.length; i++) {
            const d = Math.abs(v - targets[i])
            if (d < bestD) {
                bestD = d
                best = targets[i]
            }
        }
        return best
    }
    function commitCue(index, newStart, newEnd) {
        const b = baseCues()
        const list = []
        for (let i = 0; i < b.length; i++) {
            if (i === index)
                list.push({ start: newStart, end: newEnd, text: b[i].text })
            else
                list.push({ start: b[i].start, end: b[i].end, text: b[i].text })
        }
        EditorState.previewSetSubtitleCues(EditorState.selectedTrack, EditorState.selectedClip, list)
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.panelBackground
        border.width: 1
        border.color: Theme.panelBorder
    }

    Row {
        anchors.fill: parent

        // Left gutter — matches track-label width so the lane lines up with the tracks.
        Item {
            width: root.labelsWidth
            height: parent.height

            Column {
                anchors.fill: parent
                anchors.margins: 6
                spacing: 2

                Text {
                    text: qsTr("Subtitles")
                    color: Theme.mutedForeground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeXs
                    font.weight: Font.Medium
                }
                Text {
                    text: qsTr("%n captions", "", root.cues.length)
                    color: Theme.mutedForeground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeXs
                    opacity: 0.8
                }
            }

            Rectangle {
                anchors.right: parent.right
                width: 1
                height: parent.height
                color: Theme.panelBorder
            }
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
                Rectangle {
                    x: root.xForSeconds(0)
                    width: Math.max(2, root.clipDuration * root.pxPerSecond)
                    height: parent.height
                    color: Theme.panelAccent
                    opacity: 0.4
                }

                // Voice waveform of mixed video/audio timeline audio — positioned and
                // scaled from A/V clips only, so subtitle trim/move does not reshape it.
                Canvas {
                    id: voiceWaveform
                    x: root.mediaWaveformRange.start * root.pxPerSecond
                    width: Math.max(2, root.mediaWaveformRange.duration * root.pxPerSecond)
                    height: parent.height
                    visible: root.mediaWaveformRange.duration > 0
                    opacity: 0.75
                    z: 1

                    readonly property real waveStart: root.mediaWaveformRange.start
                    readonly property real waveDuration: root.mediaWaveformRange.duration
                    // One peak bucket per screen pixel (rounded to 64) so zoom keeps
                    // column resolution instead of stretching a fixed 240-peak list.
                    readonly property int peakCount: {
                        var w = Math.max(1, Math.ceil(width))
                        return Math.min(8192, Math.max(64, Math.ceil(w / 64) * 64))
                    }

                    // Asking for these mixes every A/V clip on the timeline, so it must not
                    // happen until the lane is actually open — bindings evaluate while the
                    // lane is hidden too, which meant dropping a long clip paid for the whole
                    // mix with nothing on screen to show for it.
                    property var peaks: {
                        void EditorState.tracks
                        void root.waveformRevision
                        return (root.visible && waveDuration > 0)
                            ? EditorState.subtitleWaveformPeaks(waveStart, waveDuration, peakCount)
                            : []
                    }
                    onPeaksChanged: requestPaint()
                    onWidthChanged: requestPaint()
                    onHeightChanged: requestPaint()
                    onWaveStartChanged: requestPaint()
                    onWaveDurationChanged: requestPaint()
                    onPeakCountChanged: requestPaint()

                    Connections {
                        target: EditorState
                        function onSubtitleWaveformReady(s, d, n) {
                            if (Math.abs(s - voiceWaveform.waveStart) < 1e-6
                                && Math.abs(d - voiceWaveform.waveDuration) < 1e-6
                                && n === voiceWaveform.peakCount)
                                root.waveformRevision++
                        }
                    }

                    onPaint: {
                        var ctx = getContext("2d");
                        ctx.clearRect(0, 0, width, height);
                        if (!peaks || peaks.length === 0)
                            return;
                        // One filled column per screen pixel so bars stay contiguous at any zoom.
                        ctx.fillStyle = Theme.panelWaveformColor;
                        var mid = height / 2;
                        var w = Math.max(1, Math.floor(width));
                        var n = peaks.length;
                        for (var x = 0; x < w; x++) {
                            var i0 = Math.floor(x * n / w);
                            var i1 = Math.floor((x + 1) * n / w);
                            if (i1 <= i0)
                                i1 = Math.min(n, i0 + 1);
                            var peak = 0;
                            for (var i = i0; i < i1; i++) {
                                if (peaks[i] > peak)
                                    peak = peaks[i];
                            }
                            var amp = peak * mid * 0.9;
                            if (amp > 0.5)
                                ctx.fillRect(x, mid - amp, 1, amp * 2);
                        }
                    }
                }

                // Playhead line, aligned with the timeline playhead.
                Rectangle {
                    x: root.xForSeconds(EditorState.playheadSeconds - root.clipStart) - Theme.playheadLineWidth / 2
                    width: Theme.playheadLineWidth
                    height: parent.height
                    color: Theme.primary
                    z: 50
                }

                Repeater {
                    model: root.displayCues

                    delegate: Rectangle {
                        id: block
                        required property int index
                        required property var modelData

                        readonly property bool isSelected: index === EditorState.selectedSubtitleCue
                        readonly property bool isActive: {
                            const ph = EditorState.playheadSeconds - root.clipStart
                            return ph >= modelData.start && ph < modelData.end
                        }
                        readonly property real gripW: Math.min(8, Math.max(3, width / 4))

                        property bool dragging: false
                        property real dragX: 0
                        property real dragW: 0
                        property real editStart: 0
                        property real editEnd: 0

                        height: parent.height - 16
                        anchors.verticalCenter: parent.verticalCenter
                        x: dragging ? dragX : root.xForSeconds(modelData.start)
                        width: Math.max(3, dragging ? dragW
                                                     : (modelData.end - modelData.start) * root.pxPerSecond)
                        radius: Theme.radiusSm
                        color: isSelected ? Theme.clipSubtitle
                                          : (isActive ? Qt.lighter(Theme.clipSubtitle, 1.15) : Theme.clipSubtitle)
                        opacity: isSelected ? 1.0 : (isActive ? 0.85 : 0.5)
                        border.width: isSelected ? 2 : 0
                        border.color: Theme.primary
                        z: isSelected ? 20 : 10

                        Behavior on opacity { NumberAnimation { duration: 120 } }

                        function startEdit() {
                            editStart = modelData.start
                            editEnd = modelData.end
                            dragX = root.xForSeconds(editStart)
                            dragW = (editEnd - editStart) * root.pxPerSecond
                            dragging = true
                            root.beginDrag()
                        }
                        function finishEdit() {
                            dragging = false
                            root.endDrag()
                            EditorState.commitPreviewDrag()
                        }

                        Text {
                            anchors.fill: parent
                            anchors.leftMargin: 6
                            anchors.rightMargin: 6
                            verticalAlignment: Text.AlignVCenter
                            elide: Text.ElideRight
                            text: (block.modelData.text && block.modelData.text.length)
                                  ? block.modelData.text : qsTr("(empty)")
                            color: Theme.onMedia
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSizeXs
                            font.italic: !(block.modelData.text && block.modelData.text.length)
                        }

                        // Body: click to select, drag to move (keeps duration).
                        Item {
                            anchors.fill: parent
                            anchors.leftMargin: block.gripW
                            anchors.rightMargin: block.gripW

                            HoverHandler { cursorShape: Qt.OpenHandCursor }
                            TapHandler {
                                onTapped: EditorState.selectedSubtitleCue = block.index
                            }
                            DragHandler {
                                target: null
                                xAxis.enabled: true
                                yAxis.enabled: false
                                onActiveChanged: {
                                    if (active)
                                        block.startEdit()
                                    else
                                        block.finishEdit()
                                }
                                onTranslationChanged: {
                                    const d = block.editEnd - block.editStart
                                    const raw = block.editStart + translation.x / root.pxPerSecond
                                    const targets = root.snapTargets(block.index)
                                    const snS = root.snap(raw, targets)
                                    const snE = root.snap(raw + d, targets) - d
                                    let ns = Math.abs(snS - raw) <= Math.abs(snE - raw) ? snS : snE
                                    ns = root.clampVal(ns, root.loBound(block.index),
                                                       root.hiBound(block.index) - d)
                                    block.dragX = root.xForSeconds(ns)
                                    root.commitCue(block.index, ns, ns + d)
                                }
                            }
                        }

                        // Left edge: resize start.
                        Rectangle {
                            anchors.left: parent.left
                            width: block.gripW
                            height: parent.height
                            color: block.isSelected ? Theme.primary : "transparent"
                            opacity: 0.6
                            HoverHandler { cursorShape: Qt.SizeHorCursor }
                            DragHandler {
                                target: null
                                xAxis.enabled: true
                                yAxis.enabled: false
                                onActiveChanged: {
                                    if (active)
                                        block.startEdit()
                                    else
                                        block.finishEdit()
                                }
                                onTranslationChanged: {
                                    let ns = block.editStart + translation.x / root.pxPerSecond
                                    ns = root.snap(ns, root.snapTargets(block.index))
                                    ns = root.clampVal(ns, root.loBound(block.index),
                                                       block.editEnd - root.minCueDur)
                                    block.dragX = root.xForSeconds(ns)
                                    block.dragW = (block.editEnd - ns) * root.pxPerSecond
                                    root.commitCue(block.index, ns, block.editEnd)
                                }
                            }
                        }

                        // Right edge: resize end.
                        Rectangle {
                            anchors.right: parent.right
                            width: block.gripW
                            height: parent.height
                            color: block.isSelected ? Theme.primary : "transparent"
                            opacity: 0.6
                            HoverHandler { cursorShape: Qt.SizeHorCursor }
                            DragHandler {
                                target: null
                                xAxis.enabled: true
                                yAxis.enabled: false
                                onActiveChanged: {
                                    if (active)
                                        block.startEdit()
                                    else
                                        block.finishEdit()
                                }
                                onTranslationChanged: {
                                    let ne = block.editEnd + translation.x / root.pxPerSecond
                                    ne = root.snap(ne, root.snapTargets(block.index))
                                    ne = root.clampVal(ne, block.editStart + root.minCueDur,
                                                       root.hiBound(block.index))
                                    block.dragW = (ne - block.editStart) * root.pxPerSecond
                                    root.commitCue(block.index, block.editStart, ne)
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
