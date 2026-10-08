import QtQuick
import QtQuick.Window
import Drift 1.0
import "components"

// Speed-ramp editor for one clip. The clip is auditioned on its own — raw decode, no effects,
// no other tracks — so what the graph does to it is the only thing you are looking at.
//
// The graph's x axis is position through the *source*: the filmstrip above it is evenly spaced
// original frames, and the playhead sweeps them at a variable rate. A high point means the
// playhead races through that stretch; a low one means it crawls.
//
// A window rather than a dialog so the timeline stays visible, matching SegmentationWindow and
// DenoiseWindow.
Window {
    id: root

    property int trackIndex: -1
    property int clipIndex: -1

    // Local mirror of the curve, and the source of truth while the window is open: reading it
    // back out of the controller mid-drag would fight the drag for the same values.
    property var points: []
    property int selectedPoint: -1

    readonly property real minSpeed: 0.1
    readonly property real maxSpeed: 8.0
    readonly property real logSpan: Math.log(maxSpeed / minSpeed)

    width: 900
    height: 720
    minimumWidth: 680
    minimumHeight: 560
    title: qsTr("Custom speed")
    color: Theme.appBackground

    function openFor(track, clip) {
        root.trackIndex = track
        root.clipIndex = clip
        root.selectedPoint = -1
        EditorState.speedCurve.begin(track, clip)
        root.points = EditorState.speedCurve.points
        root.show()
        root.raise()
        root.requestActivate()
    }

    onClosing: EditorState.speedCurve.end()

    Connections {
        target: EditorState.speedCurve
        function onApplied() { root.close() }
    }

    // ----- Curve maths ----------------------------------------------------------------------
    // Speed is drawn on a log axis so 1× sits centred between 0.1× and 8×.
    function yForSpeed(speed, h) {
        const clamped = Math.max(root.minSpeed, Math.min(root.maxSpeed, speed))
        return h - (Math.log(clamped / root.minSpeed) / root.logSpan) * h
    }
    function speedForY(y, h) {
        const t = Math.max(0, Math.min(1, (h - y) / Math.max(1, h)))
        return root.minSpeed * Math.exp(t * root.logSpan)
    }

    // Only ever called on drag release: the controller sorts, clamps and pins the ends, so
    // reading its result straight back is what keeps the local mirror honest — but doing it
    // mid-drag would fight the drag for the same values.
    function commit() {
        EditorState.speedCurve.setPoints(root.points)
        root.points = EditorState.speedCurve.points
        curveCanvas.requestPaint()
    }

    // Deep copy — the array is handed to C++ and re-read, so the delegates must not alias it.
    function clonePoints() {
        return root.points.map(function (p) {
            return { pos: p.pos, speed: p.speed, inDx: p.inDx, inDy: p.inDy,
                     outDx: p.outDx, outDy: p.outDy, corner: p.corner }
        })
    }

    // Cubic through the segment's two points and their facing handles.
    function speedAtPos(pos) {
        const pts = root.points
        if (pts.length < 2)
            return 1
        for (let i = 0; i + 1 < pts.length; ++i) {
            const a = pts[i]
            const b = pts[i + 1]
            if (pos < a.pos || pos > b.pos)
                continue
            if (b.pos - a.pos < 1e-9)
                return b.speed
            // Same bisection the C++ side uses: the curve is single-valued in pos.
            let lo = 0, hi = 1
            const x0 = a.pos, x1 = a.pos + a.outDx, x2 = b.pos + b.inDx, x3 = b.pos
            for (let k = 0; k < 20; ++k) {
                const mid = (lo + hi) / 2
                const mt = 1 - mid
                const x = mt * mt * mt * x0 + 3 * mt * mt * mid * x1
                        + 3 * mt * mid * mid * x2 + mid * mid * mid * x3
                if (x < pos) lo = mid; else hi = mid
            }
            const t = (lo + hi) / 2
            const mt = 1 - t
            return mt * mt * mt * a.speed + 3 * mt * mt * t * (a.speed + a.outDy)
                 + 3 * mt * t * t * (b.speed + b.inDy) + t * t * t * b.speed
        }
        return pts[pts.length - 1].speed
    }

    function addPointAt(pos) {
        pos = Math.max(0.001, Math.min(0.999, pos))
        const next = root.clonePoints()
        const point = { pos: pos, speed: root.speedAtPos(pos), inDx: 0, inDy: 0,
                        outDx: 0, outDy: 0, corner: true }
        let index = next.length - 1
        for (let i = 0; i < next.length; ++i) {
            if (next[i].pos > pos) { index = i; break }
        }
        next.splice(index, 0, point)
        root.points = next
        root.selectedPoint = index
        root.commit()
    }

    function deleteSelected() {
        // The ends define the ramp's span; without them there is no curve.
        if (root.selectedPoint <= 0 || root.selectedPoint >= root.points.length - 1)
            return
        const next = root.clonePoints()
        next.splice(root.selectedPoint, 1)
        root.points = next
        root.selectedPoint = -1
        root.commit()
    }

    // Corner points join with straight segments; smooth ones get symmetric tangents derived
    // from their neighbours, which is what makes a ramp ease rather than kink.
    function setSelectedCorner(corner) {
        if (root.selectedPoint < 0)
            return
        const next = root.clonePoints()
        const i = root.selectedPoint
        const point = next[i]
        point.corner = corner
        if (corner) {
            point.inDx = 0; point.inDy = 0; point.outDx = 0; point.outDy = 0
        } else {
            const prev = next[Math.max(0, i - 1)]
            const after = next[Math.min(next.length - 1, i + 1)]
            const spanX = Math.max(1e-6, after.pos - prev.pos)
            const slope = (after.speed - prev.speed) / spanX
            const outX = (after.pos - point.pos) / 3
            const inX = (point.pos - prev.pos) / 3
            point.outDx = outX; point.outDy = slope * outX
            point.inDx = -inX; point.inDy = -slope * inX
        }
        root.points = next
        root.commit()
    }

    function resetCurve() {
        root.points = [
            { pos: 0, speed: 1, inDx: 0, inDy: 0, outDx: 0, outDy: 0, corner: true },
            { pos: 1, speed: 1, inDx: 0, inDy: 0, outDx: 0, outDy: 0, corner: true }
        ]
        root.selectedPoint = -1
        root.commit()
    }

    readonly property var selected: root.selectedPoint >= 0 && root.selectedPoint < root.points.length
                                    ? root.points[root.selectedPoint] : null

    Column {
        id: content
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: footer.top
        anchors.margins: Theme.spacingLg
        spacing: Theme.spacingMd

        // ----- Preview ----------------------------------------------------------------------
        Rectangle {
            id: stage
            width: parent.width
            height: parent.height - transport.height - strip.height - toolbar.height - graph.height
                    - Theme.spacingMd * 4
            radius: Theme.radiusMd
            color: Theme.panelBackground

            Image {
                anchors.centerIn: parent
                width: Math.min(parent.width, parent.height * stage.aspect)
                height: width / stage.aspect
                fillMode: Image.PreserveAspectFit
                cache: false
                // The revision defeats QML's URL-keyed cache; the pixels behind this URL change
                // on every pump tick.
                source: EditorState.speedCurve.active
                        ? "image://clippreview/frame?rev=" + EditorState.speedCurve.revision
                        : ""
            }

            readonly property real aspect: EditorState.speedCurve.frameSize.height > 0
                ? EditorState.speedCurve.frameSize.width / EditorState.speedCurve.frameSize.height
                : 16 / 9

            ThemedLabel {
                anchors.centerIn: parent
                tone: "muted"
                visible: EditorState.speedCurve.frameSize.width <= 0
                text: qsTr("Audio only")
            }
        }

        // ----- Transport --------------------------------------------------------------------
        Row {
            id: transport
            width: parent.width
            spacing: Theme.spacingMd

            IconButton {
                anchors.verticalCenter: parent.verticalCenter
                glyph: EditorState.speedCurve.playing ? Theme.icons.pause : Theme.icons.play
                tooltip: EditorState.speedCurve.playing ? qsTr("Pause") : qsTr("Play")
                onClicked: EditorState.speedCurve.playing
                           ? EditorState.speedCurve.pause()
                           : EditorState.speedCurve.play()
            }

            ThemedLabel {
                anchors.verticalCenter: parent.verticalCenter
                text: EditorState.speedCurve.position.toFixed(2) + qsTr("s / ")
                      + EditorState.speedCurve.retimedDuration.toFixed(2) + qsTr("s")
            }

            ThemedLabel {
                anchors.verticalCenter: parent.verticalCenter
                tone: "muted"
                text: EditorState.speedCurve.sourceDuration.toFixed(2) + qsTr("s → ")
                      + EditorState.speedCurve.retimedDuration.toFixed(2) + qsTr("s")
            }
        }

        // ----- Clip strip -------------------------------------------------------------------
        Rectangle {
            id: strip
            width: parent.width
            height: 56
            radius: Theme.radiusSm
            color: Theme.panelBackground
            border.width: Theme.borderWidth
            border.color: Theme.panelBorder
            clip: true

            ClipFilmstrip {
                anchors.fill: parent
                anchors.margins: Theme.borderWidth
                filmstripPath: EditorState.speedCurve.filmstripPath
                // One pass of the strip's eight frames across the clip, rather than the repeating
                // tile the timeline draws — here the x axis *is* the source.
                frameWidth: width / frameCount
                // The strip's frames are sampled across the whole media file, but this axis covers
                // only the clip's trimmed window, which is what the curve is plotted over. Without
                // the mapping a trimmed clip shows the wrong frames under the ramp.
                inPoint: EditorState.speedCurve.sourceStart
                outPoint: EditorState.speedCurve.sourceStart + EditorState.speedCurve.sourceDuration
                sourceDuration: EditorState.speedCurve.mediaDuration
            }

            Canvas {
                id: waveCanvas
                anchors.fill: parent
                anchors.margins: Theme.borderWidth
                visible: !EditorState.speedCurve.filmstripPath

                // The x axis here is the clip's trimmed source window, the same one the filmstrip
                // above spans and the same one the curve is plotted over — not the whole file.
                function sourcePeaks(path) {
                    return EditorState.waveformPeaksForSourceRange(
                               path, EditorState.speedCurve.sourceStart,
                               EditorState.speedCurve.sourceDuration)
                }

                property var peaks: EditorState.speedCurve.clipPath
                                    ? sourcePeaks(EditorState.speedCurve.clipPath) : []

                onPeaksChanged: requestPaint()
                onWidthChanged: requestPaint()

                Connections {
                    target: EditorState
                    function onWaveformReady(path) {
                        if (path === EditorState.speedCurve.clipPath)
                            waveCanvas.peaks = waveCanvas.sourcePeaks(path)
                    }
                }

                onPaint: {
                    const ctx = getContext("2d")
                    ctx.clearRect(0, 0, width, height)
                    if (!peaks || peaks.length === 0)
                        return
                    ctx.fillStyle = Theme.panelWaveformColor
                    const mid = height / 2
                    const w = Math.max(1, Math.floor(width))
                    for (let x = 0; x < w; x++) {
                        const i0 = Math.floor(x * peaks.length / w)
                        const i1 = Math.max(i0 + 1, Math.floor((x + 1) * peaks.length / w))
                        let peak = 0
                        for (let i = i0; i < i1 && i < peaks.length; i++)
                            peak = Math.max(peak, peaks[i])
                        const amp = peak * mid * 0.9
                        if (amp > 0.5)
                            ctx.fillRect(x, mid - amp, 1, amp * 2)
                    }
                }
            }

            Rectangle {
                width: Theme.playheadLineWidth
                height: parent.height
                color: Theme.primary
                x: EditorState.speedCurve.sourcePosition * parent.width
            }

            MouseArea {
                anchors.fill: parent
                onPressed: function (mouse) {
                    EditorState.speedCurve.seekAtSource(mouse.x / width)
                }
                onPositionChanged: function (mouse) {
                    if (pressed)
                        EditorState.speedCurve.seekAtSource(mouse.x / width)
                }
            }
        }

        // ----- Toolbar ----------------------------------------------------------------------
        Row {
            id: toolbar
            width: parent.width
            spacing: Theme.spacingSm

            ThemedButton {
                variant: "secondary"
                text: qsTr("Add point")
                onClicked: root.addPointAt(EditorState.speedCurve.sourcePosition)
            }

            ThemedToggleButton {
                text: qsTr("Sharp")
                enabled: root.selected !== null
                checked: root.selected !== null && root.selected.corner
                onClicked: root.setSelectedCorner(true)
            }

            ThemedToggleButton {
                text: qsTr("Smooth")
                enabled: root.selected !== null
                checked: root.selected !== null && !root.selected.corner
                onClicked: root.setSelectedCorner(false)
            }

            ThemedButton {
                variant: "ghost"
                text: qsTr("Delete point")
                enabled: root.selectedPoint > 0 && root.selectedPoint < root.points.length - 1
                onClicked: root.deleteSelected()
            }

            ThemedButton {
                variant: "ghost"
                text: qsTr("Reset")
                onClicked: root.resetCurve()
            }

            ThemedLabel {
                anchors.verticalCenter: parent.verticalCenter
                tone: "muted"
                visible: root.selected !== null
                text: root.selected ? root.selected.speed.toFixed(2) + "×" : ""
            }
        }

        // ----- Graph ------------------------------------------------------------------------
        Rectangle {
            id: graph
            width: parent.width
            height: 220
            radius: Theme.radiusSm
            color: Theme.panelBackground
            border.width: Theme.borderWidth
            border.color: Theme.panelBorder
            clip: true

            Item {
                id: plot
                anchors.fill: parent
                // Inset vertically only: the plot's x axis has to be the same axis the strip
                // above draws on, or a point on the curve would not sit over its own frame.
                anchors.topMargin: Theme.spacingSm
                anchors.bottomMargin: Theme.spacingSm

                // Gridlines at the speeds worth aiming for.
                Repeater {
                    model: [0.1, 0.25, 0.5, 1, 2, 4, 8]
                    delegate: Item {
                        required property var modelData
                        anchors.fill: parent

                        Rectangle {
                            width: parent.width
                            height: 1
                            y: root.yForSpeed(modelData, plot.height)
                            color: modelData === 1 ? Theme.panelSecondaryForeground : Theme.panelBorder
                            opacity: modelData === 1 ? 0.6 : 0.35
                        }

                        Text {
                            x: 2
                            y: root.yForSpeed(modelData, plot.height) - height - 1
                            text: modelData + "×"
                            color: Theme.mutedForeground
                            font.family: Theme.monoFontFamily
                            font.pixelSize: Theme.fontSizeXs
                        }
                    }
                }

                Canvas {
                    id: curveCanvas
                    anchors.fill: parent

                    onPaint: {
                        const ctx = getContext("2d")
                        ctx.clearRect(0, 0, width, height)
                        const pts = root.points
                        if (pts.length < 2)
                            return

                        ctx.strokeStyle = String(Theme.primary)
                        ctx.lineWidth = 2
                        ctx.beginPath()
                        ctx.moveTo(pts[0].pos * width, root.yForSpeed(pts[0].speed, height))
                        for (let i = 0; i + 1 < pts.length; ++i) {
                            const a = pts[i]
                            const b = pts[i + 1]
                            ctx.bezierCurveTo(
                                (a.pos + a.outDx) * width, root.yForSpeed(a.speed + a.outDy, height),
                                (b.pos + b.inDx) * width, root.yForSpeed(b.speed + b.inDy, height),
                                b.pos * width, root.yForSpeed(b.speed, height))
                        }
                        ctx.stroke()

                        // Tangents, for the selected point only — drawing every handle turns the
                        // graph into a thicket.
                        const sel = root.selected
                        if (sel) {
                            ctx.strokeStyle = String(Theme.mutedForeground)
                            ctx.lineWidth = 1
                            const px = sel.pos * width
                            const py = root.yForSpeed(sel.speed, height)
                            ctx.beginPath()
                            ctx.moveTo((sel.pos + sel.inDx) * width,
                                       root.yForSpeed(sel.speed + sel.inDy, height))
                            ctx.lineTo(px, py)
                            ctx.lineTo((sel.pos + sel.outDx) * width,
                                       root.yForSpeed(sel.speed + sel.outDy, height))
                            ctx.stroke()
                        }
                    }

                    Connections {
                        target: root
                        function onPointsChanged() { curveCanvas.requestPaint() }
                        function onSelectedPointChanged() { curveCanvas.requestPaint() }
                    }
                }

                // Playhead, sweeping the source axis at whatever rate the curve dictates — and
                // draggable, following the same Item + Binding + grab-strip shape the timeline's
                // own playhead uses.
                Item {
                    id: graphPlayhead
                    y: 0
                    // Over the plot's click area, which would otherwise swallow the press
                    // before the drag could start. Still under the knobs (z 4) and their
                    // handles (z 5), so editing the curve always wins over scrubbing.
                    z: 3
                    width: Theme.playheadLineWidth
                    height: plot.height

                    Binding {
                        target: graphPlayhead
                        property: "x"
                        value: EditorState.speedCurve.sourcePosition * plot.width
                        when: !graphPlayheadDrag.drag.active
                    }

                    // Scrub live rather than only on release, so the preview follows the drag.
                    onXChanged: {
                        if (graphPlayheadDrag.drag.active)
                            EditorState.speedCurve.seekAtSource(graphPlayhead.x / plot.width)
                    }

                    Rectangle {
                        anchors.left: parent.left
                        width: Theme.playheadLineWidth
                        height: parent.height
                        color: Theme.primary
                    }

                    Rectangle {
                        width: Theme.playheadHandleSize
                        height: Theme.playheadHandleSize
                        radius: Theme.playheadHandleSize / 2
                        x: -width / 2 + Theme.playheadLineWidth / 2
                        color: Theme.primary
                    }

                    MouseArea {
                        id: graphPlayheadDrag
                        width: 9
                        height: parent.height
                        x: -(width - Theme.playheadLineWidth) / 2
                        cursorShape: Qt.SizeHorCursor
                        preventStealing: true
                        drag.target: graphPlayhead
                        drag.axis: Drag.XAxis
                        drag.threshold: 0
                        drag.minimumX: 0
                        drag.maximumX: plot.width - Theme.playheadLineWidth
                    }
                }

                MouseArea {
                    anchors.fill: parent
                    onClicked: root.selectedPoint = -1
                    onDoubleClicked: function (mouse) { root.addPointAt(mouse.x / width) }
                }

                // ----- Control points -------------------------------------------------------
                Repeater {
                    model: root.points.length

                    delegate: Rectangle {
                        id: knob
                        required property int index
                        readonly property var point: root.points[index]
                        readonly property bool isEnd: index === 0 || index === root.points.length - 1

                        width: 11
                        height: 11
                        radius: 2
                        z: 4
                        color: root.selectedPoint === index ? Theme.primary : Theme.panelBackground
                        border.width: 2
                        border.color: Theme.primary
                        x: point.pos * plot.width - width / 2
                        y: root.yForSpeed(point.speed, plot.height) - height / 2

                        // DragHandler reports translation from where the drag began, so the
                        // values it is applied to have to be frozen there too — reading the live
                        // point would compound each move into the next and make the knob bolt.
                        property real basePos: 0
                        property real baseSpeed: 1

                        TapHandler {
                            onTapped: root.selectedPoint = knob.index
                        }

                        DragHandler {
                            target: null
                            onActiveChanged: {
                                if (active) {
                                    knob.basePos = knob.point.pos
                                    knob.baseSpeed = knob.point.speed
                                    root.selectedPoint = knob.index
                                } else {
                                    root.commit()
                                }
                            }
                            onTranslationChanged: {
                                if (!active)
                                    return
                                const next = root.clonePoints()
                                const point = next[knob.index]
                                // Ends anchor the ramp's span, so they only move in speed.
                                if (!knob.isEnd) {
                                    const lo = next[knob.index - 1].pos + 0.002
                                    const hi = next[knob.index + 1].pos - 0.002
                                    point.pos = Math.max(lo, Math.min(hi,
                                        knob.basePos + translation.x / plot.width))
                                }
                                point.speed = root.speedForY(
                                    root.yForSpeed(knob.baseSpeed, plot.height) + translation.y,
                                    plot.height)
                                root.points = next
                            }
                        }
                    }
                }

                // ----- Tangent handles ------------------------------------------------------
                // An int model, not a list: a fresh array on every curve edit would tear down
                // the delegate owning the live DragHandler and drop the grab on first move.
                Repeater {
                    model: root.selected ? 2 : 0

                    delegate: Rectangle {
                        id: handle
                        required property int index
                        readonly property bool isOut: index === 1
                        readonly property real dx: isOut ? root.selected.outDx : root.selected.inDx
                        readonly property real dy: isOut ? root.selected.outDy : root.selected.inDy

                        // Frozen at drag start, for the same reason the knobs freeze theirs.
                        property real baseDx: 0
                        property real baseDy: 0

                        width: 9
                        height: 9
                        radius: 4.5
                        z: 5
                        color: Theme.panelBackground
                        border.width: 2
                        border.color: Theme.mutedForeground
                        x: (root.selected.pos + dx) * plot.width - width / 2
                        y: root.yForSpeed(root.selected.speed + dy, plot.height) - height / 2

                        DragHandler {
                            target: null
                            onActiveChanged: {
                                if (active) {
                                    handle.baseDx = handle.dx
                                    handle.baseDy = handle.dy
                                } else {
                                    root.commit()
                                }
                            }
                            onTranslationChanged: {
                                if (!active)
                                    return
                                const next = root.clonePoints()
                                const point = next[root.selectedPoint]
                                const baseX = (point.pos + handle.baseDx) * plot.width
                                const baseY = root.yForSpeed(point.speed + handle.baseDy, plot.height)
                                const newDx = (baseX + translation.x) / plot.width - point.pos
                                const newDy = root.speedForY(baseY + translation.y, plot.height)
                                              - point.speed

                                if (handle.isOut) {
                                    point.outDx = Math.max(0, newDx)
                                    point.outDy = newDy
                                } else {
                                    point.inDx = Math.min(0, newDx)
                                    point.inDy = newDy
                                }
                                // A continuous point keeps its two tangents mirrored, so the
                                // ramp passes through without a kink.
                                if (!point.corner) {
                                    if (handle.isOut) {
                                        point.inDx = -point.outDx
                                        point.inDy = -point.outDy
                                    } else {
                                        point.outDx = -point.inDx
                                        point.outDy = -point.inDy
                                    }
                                }
                                root.points = next
                            }
                        }
                    }
                }
            }
        }
    }

    // ----- Footer ---------------------------------------------------------------------------
    Row {
        id: footer
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: Theme.spacingLg
        spacing: Theme.spacingMd

        ThemedLabel {
            anchors.verticalCenter: parent.verticalCenter
            tone: "muted"
            text: qsTr("Applied as a copy on a new track — the original clip is left alone.")
        }

        ThemedButton {
            variant: "ghost"
            text: qsTr("Cancel")
            onClicked: root.close()
        }

        ThemedButton {
            variant: "primary"
            text: qsTr("Apply")
            onClicked: EditorState.speedCurve.apply()
        }
    }
}
