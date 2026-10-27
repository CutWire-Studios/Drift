pragma ComponentBehavior: Bound
import QtQuick
import Drift
import "CurveMath.js" as CurveMath

Item {
    id: root

    property var curves: ({
        master: [{x: 0, y: 0, ease: "spline"}, {x: 1, y: 1, ease: "spline"}],
        r: [{x: 0, y: 0, ease: "spline"}, {x: 1, y: 1, ease: "spline"}],
        g: [{x: 0, y: 0, ease: "spline"}, {x: 1, y: 1, ease: "spline"}],
        b: [{x: 0, y: 0, ease: "spline"}, {x: 1, y: 1, ease: "spline"}]
    })
    property string channel: hueMode ? "hue" : "master"
    property string histogram: ""
    property bool showTabs: true
    // Hue curves: x is hue around the wheel, the ends share one y so the curve wraps, and the
    // channels are hue/sat/lum instead of master/R/G/B.
    property bool hueMode: false

    signal began()
    signal changed(string channel, string json)
    signal ended()

    readonly property var channels: hueMode ? ["hue", "sat", "lum"] : ["master", "r", "g", "b"]
    readonly property var channelNames: hueMode
        ? ({ hue: qsTr("Hue"), sat: qsTr("Sat"), lum: qsTr("Lum") })
        : ({ master: qsTr("Master"), r: "R", g: "G", b: "B" })
    readonly property var channelColors: hueMode
        ? ({ hue: Theme.c.colorGradeCurveChannels.hue,
             sat: Theme.c.colorGradeCurveChannels.sat,
             lum: Theme.c.colorGradeCurveChannels.lum })
        : ({ master: Theme.c.colorGradeCurveChannels.master,
             r: Theme.c.colorGradeCurveChannels.r,
             g: Theme.c.colorGradeCurveChannels.g,
             b: Theme.c.colorGradeCurveChannels.b })
    readonly property real handleRadius: Theme.touchInput ? 8 : 5
    // Points live here while dragging so the curve tracks the pointer without waiting for the
    // owner to push `curves` back. Null the rest of the time.
    property var livePoints: null
    property int selected: -1
    // One undo step per gesture: began is raised by the first real change, not by the press.
    property bool gestureBegun: false
    property real clickAddedAt: 0
    property int clickAddedIndex: -1

    function openGesture() {
        if (gestureBegun)
            return
        gestureBegun = true
        began()
    }

    function recentlyAdded(idx) {
        return idx >= 0 && idx === clickAddedIndex
            && Date.now() - clickAddedAt < Qt.styleHints.mouseDoubleClickInterval
    }

    function commitPoints(pts) {
        began()
        emitPoints(pts)
        livePoints = null
        ended()
    }

    function nudgeSelected(dx, dy) {
        const src = pointsOf(channel)
        const idx = selected
        if (idx < 0 || idx >= src.length)
            return
        const pts = src.map(p => ({x: p.x, y: p.y, ease: p.ease || "spline"}))
        const last = pts.length - 1
        let x = pts[idx].x
        if (idx > 0 && idx < last)
            x = Math.min(pts[idx + 1].x - CurveMath.MIN_GAP,
                         Math.max(pts[idx - 1].x + CurveMath.MIN_GAP, CurveMath.clamp01(x + dx)))
        pts[idx] = {x: x, y: CurveMath.clamp01(pts[idx].y + dy), ease: pts[idx].ease || "spline"}
        commitPoints(tieHueEnds(pts, idx))
    }

    function removePoint(idx) {
        const pts = pointsOf(channel)
        if (idx <= 0 || idx >= pts.length - 1)
            return
        const next = pts.slice()
        next.splice(idx, 1)
        commitPoints(next)
        selected = -1
    }

    function defaultPoints() {
        const y = hueMode ? 0.5 : 1
        return [{x: 0, y: hueMode ? 0.5 : 0, ease: "spline"}, {x: 1, y: y, ease: "spline"}]
    }

    function pointsOf(ch) {
        if (ch === channel && livePoints)
            return livePoints
        const p = curves && curves[ch]
        return p && p.length >= 2 ? p : defaultPoints()
    }

    function tieHueEnds(pts, idx) {
        const last = pts.length - 1
        if (!hueMode || last < 1 || (idx !== 0 && idx !== last))
            return pts
        const other = idx === 0 ? last : 0
        pts[other] = {x: pts[other].x, y: pts[idx].y, ease: pts[other].ease || "spline"}
        return pts
    }

    function emitPoints(pts) {
        livePoints = pts
        changed(channel, JSON.stringify(pts))
        canvas.requestPaint()
    }

    function resetChannel() {
        began()
        emitPoints(defaultPoints())
        livePoints = null
        selected = -1
        ended()
    }

    implicitWidth: 240
    implicitHeight: tabs.height + (showTabs ? Theme.spacingMd : 0) + width
    activeFocusOnTab: true

    Keys.onPressed: event => {
        const step = ((event.modifiers & Qt.ShiftModifier) ? 10 : 1) / 255
        if (event.key === Qt.Key_Delete || event.key === Qt.Key_Backspace) {
            removePoint(selected)
        } else if (event.key === Qt.Key_Left) {
            nudgeSelected(-step, 0)
        } else if (event.key === Qt.Key_Right) {
            nudgeSelected(step, 0)
        } else if (event.key === Qt.Key_Up) {
            nudgeSelected(0, step)
        } else if (event.key === Qt.Key_Down) {
            nudgeSelected(0, -step)
        } else {
            return
        }
        event.accepted = true
    }

    onCurvesChanged: canvas.requestPaint()
    onChannelChanged: {
        livePoints = null
        selected = -1
        canvas.requestPaint()
    }
    onHistogramChanged: canvas.requestPaint()
    onWidthChanged: canvas.requestPaint()

    Item {
        id: tabs
        width: parent.width
        height: root.showTabs ? Theme.controlHeightSm : 0
        visible: root.showTabs

        Row {
            spacing: Theme.spacingSm
            anchors.verticalCenter: parent.verticalCenter
            Repeater {
                model: root.channels
                delegate: ThemedChip {
                    id: tab
                    required property string modelData
                    // The maps can still be unset when hueMode flips during creation and the
                    // Repeater builds tabs first; the bindings settle once they are.
                    text: root.channelNames?.[modelData] ?? ""
                    selected: root.channel === modelData
                    onClicked: root.channel = modelData
                    Rectangle {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        height: 2
                        radius: 1
                        color: root.channelColors?.[tab.modelData] ?? "transparent"
                        opacity: tab.selected ? 1 : 0.4
                    }
                }
            }
        }

        IconButton {
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            glyph: Theme.icons.reset
            tooltip: qsTr("Reset channel")
            buttonSize: Theme.controlHeightSm
            onClicked: root.resetChannel()
        }
    }

    Rectangle {
        id: frame
        y: tabs.height + (root.showTabs ? Theme.spacingMd : 0)
        width: parent.width
        height: width
        radius: Theme.c.fadeCurvePlotFrame.radius
        color: Theme.c.fadeCurvePlotFrame.background
        border.width: Theme.c.fadeCurvePlotFrame.borderWidth
        border.color: Theme.c.fadeCurvePlotFrame.border

        Image {
            anchors.fill: parent
            anchors.margins: parent.border.width
            source: root.histogram
            visible: root.histogram.length > 0
            opacity: 0.25
            fillMode: Image.Stretch
            asynchronous: true
        }

        Canvas {
            id: canvas
            anchors.fill: parent
            onWidthChanged: requestPaint()
            onHeightChanged: requestPaint()
            Connections {
                target: Theme
                function onDarkModeChanged() { canvas.requestPaint() }
            }
            onPaint: {
                const ctx = getContext("2d")
                ctx.reset()
                const w = width
                const h = height
                if (w < 2 || h < 2)
                    return

                if (root.hueMode) {
                    const hueGradient = ctx.createLinearGradient(0, 0, w, 0)
                    for (let i = 0; i <= 6; ++i)
                        hueGradient.addColorStop(i / 6, "hsl(" + (i * 60) + ", 80%, 55%)")
                    ctx.globalAlpha = 0.18
                    ctx.fillStyle = hueGradient
                    ctx.fillRect(0, 0, w, h)
                }

                ctx.strokeStyle = String(Theme.c.fadeCurveCanvas.grid)
                ctx.lineWidth = Theme.c.fadeCurveCanvas.gridLineWidth
                ctx.globalAlpha = Theme.c.fadeCurveCanvas.gridOpacity
                for (let i = 1; i < 4; ++i) {
                    ctx.beginPath()
                    ctx.moveTo(0, h * i / 4)
                    ctx.lineTo(w, h * i / 4)
                    ctx.moveTo(w * i / 4, 0)
                    ctx.lineTo(w * i / 4, h)
                    ctx.stroke()
                }

                if (!root.hueMode) {
                    ctx.globalAlpha = 0.5
                    ctx.beginPath()
                    ctx.moveTo(0, h)
                    ctx.lineTo(w, 0)
                    ctx.stroke()
                }

                const steps = Math.max(32, Math.floor(w / 2))
                const drawCurve = (ch, alpha, lineWidth) => {
                    const pts = root.pointsOf(ch)
                    const tan = CurveMath.tangents(pts)
                    ctx.globalAlpha = alpha
                    ctx.strokeStyle = String(root.channelColors[ch])
                    ctx.lineWidth = lineWidth
                    ctx.beginPath()
                    for (let s = 0; s <= steps; ++s) {
                        const x = s / steps
                        const y = CurveMath.evaluate(pts, x, tan)
                        if (s === 0)
                            ctx.moveTo(x * w, (1 - y) * h)
                        else
                            ctx.lineTo(x * w, (1 - y) * h)
                    }
                    ctx.stroke()
                }
                for (let c = 0; c < root.channels.length; ++c) {
                    if (root.channels[c] !== root.channel)
                        drawCurve(root.channels[c], 0.25, Theme.c.fadeCurveCanvas.handleLineWidth)
                }
                drawCurve(root.channel, 1, Theme.c.fadeCurveCanvas.curveLineWidth)
            }
        }

        Repeater {
            model: root.pointsOf(root.channel).length
            delegate: Rectangle {
                required property int index
                readonly property var pt: root.pointsOf(root.channel)[index] || ({x: 0, y: 0})
                width: root.handleRadius * 2
                height: width
                radius: width / 2
                x: pt.x * frame.width - width / 2
                y: (1 - pt.y) * frame.height - height / 2
                color: root.selected === index ? root.channelColors[root.channel]
                                               : Theme.c.fadeCurveWindow.pointBackground
                border.width: Theme.c.fadeCurveWindow.knobBorderWidth
                border.color: root.channelColors[root.channel]
            }
        }

        MouseArea {
            id: area
            anchors.fill: parent
            preventStealing: true
            cursorShape: Qt.CrossCursor

            property bool dragging: false
            property int dragIndex: -1
            property var lockedFlickable: null

            function lockFlickable() {
                if (lockedFlickable)
                    return
                let node = root.parent
                while (node) {
                    if (node.dragLocks !== undefined && node.contentHeight !== undefined) {
                        lockedFlickable = node
                        node.dragLocks++
                        return
                    }
                    node = node.parent
                }
            }
            function unlockFlickable() {
                if (lockedFlickable) {
                    lockedFlickable.dragLocks = Math.max(0, lockedFlickable.dragLocks - 1)
                    lockedFlickable = null
                }
            }
            Component.onDestruction: unlockFlickable()

            function hit(mx, my) {
                const pts = root.pointsOf(root.channel)
                const reach = Math.max(root.handleRadius + 4, 10)
                let best = -1
                let bestD = reach * reach
                for (let i = 0; i < pts.length; ++i) {
                    const dx = pts[i].x * width - mx
                    const dy = (1 - pts[i].y) * height - my
                    const d = dx * dx + dy * dy
                    if (d <= bestD) {
                        bestD = d
                        best = i
                    }
                }
                return best
            }

            function finish() {
                if (!dragging)
                    return
                dragging = false
                dragIndex = -1
                unlockFlickable()
                root.livePoints = null
                if (root.gestureBegun) {
                    root.gestureBegun = false
                    root.ended()
                }
            }

            onPressed: mouse => {
                let idx = hit(mouse.x, mouse.y)
                // The second press of a double-click on a point just added would drag it; skip it.
                if (root.recentlyAdded(idx))
                    return
                lockFlickable()
                root.forceActiveFocus()
                let pts = root.pointsOf(root.channel).map(p => ({x: p.x, y: p.y, ease: p.ease || "spline"}))
                dragging = true
                if (idx < 0) {
                    if (pts.length >= CurveMath.MAX_POINTS) {
                        dragIndex = -1
                        root.selected = -1
                        return
                    }
                    const x = CurveMath.clamp01(mouse.x / width)
                    const lo = x - CurveMath.MIN_GAP
                    const hi = x + CurveMath.MIN_GAP
                    if (pts.some(p => p.x > lo && p.x < hi)) {
                        dragIndex = -1
                        root.selected = -1
                        return
                    }
                    const y = CurveMath.clamp01(CurveMath.evaluate(pts, x, CurveMath.tangents(pts)))
                    pts.push({x: x, y: y, ease: "spline"})
                    pts = CurveMath.sortedPoints(pts)
                    idx = pts.findIndex(p => p.x === x)
                    root.openGesture()
                    root.emitPoints(pts)
                    root.clickAddedAt = Date.now()
                    root.clickAddedIndex = idx
                } else {
                    root.livePoints = pts
                }
                dragIndex = idx
                root.selected = idx
            }

            onPositionChanged: mouse => {
                if (!dragging || dragIndex < 0)
                    return
                root.openGesture()
                const pts = root.livePoints.slice()
                const last = pts.length - 1
                const outside = mouse.x < -width * 0.25 || mouse.x > width * 1.25
                              || mouse.y < -height * 0.25 || mouse.y > height * 1.25
                if (dragIndex > 0 && dragIndex < last && outside) {
                    pts.splice(dragIndex, 1)
                    dragIndex = -1
                    root.selected = -1
                    root.emitPoints(pts)
                    return
                }
                const y = CurveMath.clamp01(1 - mouse.y / height)
                let x = pts[dragIndex].x
                if (dragIndex > 0 && dragIndex < last) {
                    x = Math.min(pts[dragIndex + 1].x - CurveMath.MIN_GAP,
                                 Math.max(pts[dragIndex - 1].x + CurveMath.MIN_GAP, mouse.x / width))
                }
                pts[dragIndex] = {x: x, y: y, ease: pts[dragIndex].ease || "spline"}
                root.emitPoints(root.tieHueEnds(pts, dragIndex))
            }

            onReleased: finish()
            onCanceled: finish()

            onDoubleClicked: mouse => {
                const idx = hit(mouse.x, mouse.y)
                if (root.recentlyAdded(idx))
                    return
                dragging = false
                dragIndex = -1
                unlockFlickable()
                root.removePoint(idx)
            }
        }
    }
}
