import QtQuick
import QtQuick.Window
import Drift
// Speed-ramp editor for one clip. The clip is auditioned on its own — raw decode, no effects,
// no other tracks — so what the graph does to it is the only thing you are looking at.
//
// The graph's x axis is position through the *source*: the filmstrip above it is evenly spaced
// original frames, and the playhead sweeps them at a variable rate. A high point means the
// playhead races through that stretch; a low one means it crawls.
//
// A window rather than a dialog so the timeline stays visible, matching SegmentationWindow and
// DenoiseWindow.
ToolWindow {
    id: root

    property int trackIndex: -1
    property int clipIndex: -1

    // Local mirror of the curve, and the source of truth while the window is open: reading it
    // back out of the controller mid-drag would fight the drag for the same values.
    property var points: []
    property int selectedPoint: -1

    readonly property real minSpeed: 0.1
    readonly property real maxSpeed: 100.0
    readonly property real logSpan: Math.log(maxSpeed / minSpeed)

    width: 900
    height: 720
    minimumWidth: 680
    minimumHeight: 560
    title: qsTr("Speed ramp")
    color: Theme.c.speedCurveWindow.background

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
    // Speed is drawn on a log axis so 1× sits centred between 0.1× and 100×.
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
        SpeedCurvePreviewStage {
            transportItem: transport
            stripItem: strip
            toolbarItem: toolbar
            graphItem: graph
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
        SpeedCurveFilmstrip { id: strip }

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
            radius: Theme.c.speedCurveWindow.graphRadius
            color: Theme.c.speedCurveWindow.graphBackground
            border.width: Theme.c.speedCurveWindow.graphBorderWidth
            border.color: Theme.c.speedCurveWindow.graphBorder
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
                    model: [0.1, 0.5, 1, 2, 5, 10, 25, 100]
                    delegate: SpeedCurveGridLine {
                        host: root
                        plotItem: plot
                    }
                }

                SpeedCurveCanvas { host: root }

                // Playhead, sweeping the source axis at whatever rate the curve dictates — and
                // draggable, following the same Item + Binding + grab-strip shape the timeline's
                // own playhead uses.
                SpeedCurveGraphPlayhead { plotItem: plot }

                MouseArea {
                    anchors.fill: parent
                    onClicked: root.selectedPoint = -1
                    onDoubleClicked: function (mouse) { root.addPointAt(mouse.x / width) }
                }

                // ----- Control points -------------------------------------------------------
                Repeater {
                    model: root.points.length

                    delegate: SpeedCurveKnob {
                        host: root
                        plotItem: plot
                    }
                }

                // ----- Tangent handles ------------------------------------------------------
                // An int model, not a list: a fresh array on every curve edit would tear down
                // the delegate owning the live DragHandler and drop the grab on first move.
                Repeater {
                    model: root.selected ? 2 : 0

                    delegate: SpeedCurveTangentHandle {
                        host: root
                        plotItem: plot
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
