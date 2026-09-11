import QtQuick
import QtQuick.Window
import Drift 1.0
import "components"

// Unit curve editor. X is always position through the effect; Y is what that position maps to.
// Two modes share it: "fade" edits a clip's edge-fade gain (silent at the bottom, full at the
// top) and "transition" edits a transition's progress curve. The geometry is identical, only the
// session it drives differs, so the editor is parameterised rather than duplicated.
Window {
    id: root

    property int trackIndex: -1
    property int clipIndex: -1
    property string mode: "fade" // "fade" | "transition"
    property string transitionId: ""
    readonly property bool isTransition: root.mode === "transition"
    // "points" = polyline through any number of knots; "bezier" = one cubic with pinned ends and
    // two handles, i.e. CSS cubic-bezier(). Two different shapes, not two views of one.
    property string shapeMode: "points"
    readonly property bool isBezier: root.shapeMode === "bezier"
    property var handles: [0.42, 0.0, 0.58, 1.0]
    property int selectedHandle: -1
    property var points: []
    property int selectedPoint: -1
    property bool closingAfterApply: false

    width: 520
    height: 420
    minimumWidth: 420
    minimumHeight: 340
    title: qsTr("Custom curve")
    color: Theme.appBackground

    // Which session the editor is driving. Everything below goes through these four.
    function sessionPoints() {
        return root.isTransition ? EditorState.transitionCurvePoints : EditorState.fadeCurvePoints
    }

    function sessionHandles() {
        return root.isTransition ? EditorState.transitionCurveHandles : EditorState.fadeCurveHandles
    }

    function sessionMode() {
        return root.isTransition ? EditorState.transitionCurveMode : EditorState.fadeCurveMode
    }

    function commitHandles(resync) {
        const h = root.handles
        if (root.isTransition)
            EditorState.setTransitionCurveHandles(h[0], h[1], h[2], h[3])
        else
            EditorState.setFadeCurveHandles(h[0], h[1], h[2], h[3])
        if (resync !== false) {
            root.handles = root.sessionHandles()
            curveCanvas.requestPaint()
        }
    }

    // Switching mode is itself an edit: the session commits whichever shape is live, so moving to
    // bezier pushes the current handles and moving back pushes the current points.
    function setShapeMode(next) {
        if (root.shapeMode === next)
            return
        root.shapeMode = next
        root.selectedPoint = -1
        root.selectedHandle = -1
        if (next === "bezier")
            root.commitHandles()
        else
            root.commit()
        curveCanvas.requestPaint()
    }

    // Cubic with anchors pinned at (0,0) and (1,1); h is [c1x, c1y, c2x, c2y].
    function bezierYAt(t) {
        const h = root.handles
        t = Math.max(0, Math.min(1, t))
        let lo = 0, hi = 1
        for (let i = 0; i < 24; ++i) {
            const mid = (lo + hi) / 2
            const mt = 1 - mid
            const x = 3 * mt * mt * mid * h[0] + 3 * mt * mid * mid * h[2] + mid * mid * mid
            if (x < t) lo = mid; else hi = mid
        }
        const u = (lo + hi) / 2
        const mu = 1 - u
        return 3 * mu * mu * u * h[1] + 3 * mu * u * u * h[3] + u * u * u
    }

    function sessionPreset(preset) {
        if (root.isTransition)
            EditorState.resetTransitionCurvePreset(preset)
        else
            EditorState.resetFadeCurvePreset(preset)
    }

    function sessionApply() {
        if (root.isTransition)
            EditorState.applyTransitionCurve()
        else
            EditorState.applyFadeCurve()
    }

    function sessionEnd() {
        if (root.isTransition)
            EditorState.endTransitionCurveSession()
        else
            EditorState.endFadeCurveSession()
    }

    function applyPreset(preset) {
        root.sessionPreset(root.isBezier ? "bezier:" + preset : preset)
        root.points = root.sessionPoints()
        root.handles = root.sessionHandles()
        root.selectedPoint = -1
        root.selectedHandle = -1
        root.ensureEditable()
        curveCanvas.requestPaint()
    }

    function syncFromSession() {
        root.points = root.sessionPoints()
        root.handles = root.sessionHandles()
        root.shapeMode = root.sessionMode()
        root.selectedPoint = -1
        root.selectedHandle = -1
    }

    function openFor(track, clip) {
        root.mode = "fade"
        root.transitionId = ""
        root.trackIndex = track
        root.clipIndex = clip
        root.closingAfterApply = false
        EditorState.beginFadeCurveSession(track, clip)
        root.syncFromSession()
        root.ensureEditable()
        root.show()
        root.raise()
        root.requestActivate()
        curveCanvas.requestPaint()
    }

    function openForTransition(track, transitionId) {
        root.mode = "transition"
        root.transitionId = transitionId
        root.trackIndex = track
        root.clipIndex = -1
        root.closingAfterApply = false
        EditorState.beginTransitionCurveSession(track, transitionId)
        root.syncFromSession()
        root.ensureEditable()
        root.show()
        root.raise()
        root.requestActivate()
        curveCanvas.requestPaint()
    }

    onClosing: {
        if (!root.closingAfterApply)
            root.sessionEnd()
    }

    Connections {
        target: EditorState
        function onFadeCurveApplied() {
            if (root.isTransition)
                return
            root.closingAfterApply = true
            root.close()
        }
        function onTransitionCurveApplied() {
            if (!root.isTransition)
                return
            root.closingAfterApply = true
            root.close()
        }
    }

    function gainAt(t) {
        const pts = root.points
        if (!pts || pts.length < 2)
            return t
        t = Math.max(0, Math.min(1, t))
        if (t <= pts[0].t)
            return pts[0].g
        if (t >= pts[pts.length - 1].t)
            return pts[pts.length - 1].g
        for (let i = 0; i + 1 < pts.length; ++i) {
            const a = pts[i]
            const b = pts[i + 1]
            if (t < a.t || t > b.t)
                continue
            const span = b.t - a.t
            if (Math.abs(span) < 1e-9)
                return b.g
            const u = (t - a.t) / span
            return a.g + (b.g - a.g) * u
        }
        return pts[pts.length - 1].g
    }

    function clonePoints() {
        return root.points.map(function (p) {
            return { t: p.t, g: p.g }
        })
    }

    // Push the local mirror into the session. `resync` re-reads the clamped/sorted
    // points back from C++ — only safe on drag release, or the knob will fight the
    // controller for the same values mid-drag.
    function commit(resync) {
        if (root.isTransition)
            EditorState.setTransitionCurvePoints(root.points)
        else
            EditorState.setFadeCurvePoints(root.points)
        if (resync !== false) {
            root.points = root.sessionPoints()
            curveCanvas.requestPaint()
        }
    }

    function addPointAt(t) {
        t = Math.max(0.02, Math.min(0.98, t))
        const next = root.clonePoints()
        // Don't stack on top of an existing knot.
        for (let i = 0; i < next.length; ++i) {
            if (Math.abs(next[i].t - t) < 0.02)
                return
        }
        const point = { t: t, g: root.gainAt(t) }
        let index = next.length - 1
        for (let i = 0; i < next.length; ++i) {
            if (next[i].t > t) {
                index = i
                break
            }
        }
        next.splice(index, 0, point)
        root.points = next
        root.selectedPoint = index
        root.commit()
    }

    // If a preset left only the two rails, drop a middle handle so the curve is editable.
    function ensureEditable() {
        if (root.points.length >= 3)
            return
        root.addPointAt(0.5)
    }

    function removeSelected() {
        if (root.selectedPoint <= 0 || root.selectedPoint >= root.points.length - 1)
            return
        const next = root.clonePoints()
        next.splice(root.selectedPoint, 1)
        root.points = next
        root.selectedPoint = -1
        root.commit()
    }

    Column {
        anchors.fill: parent
        anchors.margins: Theme.spacingXl
        spacing: Theme.spacingLg

        Row {
            width: parent.width
            spacing: Theme.spacingMd

            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: root.isTransition
                      ? (EditorState.transitionCurveName.length
                         ? qsTr("Progress curve — %1").arg(EditorState.transitionCurveName)
                         : qsTr("Progress curve"))
                      : (EditorState.fadeCurveClipName.length
                         ? qsTr("Fade shape — %1").arg(EditorState.fadeCurveClipName)
                         : qsTr("Fade shape"))
                color: Theme.panelForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeBase
                font.weight: Font.Medium
                elide: Text.ElideRight
                width: Math.max(80, parent.width - presetRow.width - parent.spacing)
            }

            Row {
                id: presetRow
                spacing: 6
                anchors.verticalCenter: parent.verticalCenter

                ThemedChip {
                    text: qsTr("Points")
                    selected: !root.isBezier
                    onClicked: root.setShapeMode("points")
                }
                ThemedChip {
                    text: qsTr("Bezier")
                    selected: root.isBezier
                    onClicked: root.setShapeMode("bezier")
                }

                Rectangle {
                    width: Theme.borderWidth
                    height: 18
                    color: Theme.panelBorder
                    anchors.verticalCenter: parent.verticalCenter
                }

                ThemedChip {
                    text: qsTr("Linear")
                    onClicked: root.applyPreset("linear")
                }
                ThemedChip {
                    text: root.isBezier ? qsTr("Ease") : qsTr("Smooth")
                    onClicked: root.applyPreset(root.isBezier ? "ease" : "smooth")
                }
                ThemedChip {
                    text: root.isBezier ? qsTr("Ease In") : qsTr("Natural")
                    onClicked: root.applyPreset(root.isBezier ? "easeIn" : "equalPower")
                }
                ThemedChip {
                    visible: root.isBezier
                    text: qsTr("Ease Out")
                    onClicked: root.applyPreset("easeOut")
                }
            }
        }

        Text {
            width: parent.width
            text: root.isBezier
                  ? qsTr("Drag the two handles to shape the cubic. The ends stay pinned, and handles are held inside the box so the curve cannot fold back on itself.")
                  : qsTr("Drag the middle points to shape the ramp (ends stay silent→full). Double-click to add a point; Delete removes the selection.")
            color: Theme.mutedForeground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeXs
            wrapMode: Text.WordWrap
        }

        Item {
            id: plot
            width: parent.width
            height: Math.max(180, root.height - 200)

            readonly property real inset: 16
            readonly property real plotW: Math.max(1, width - inset * 2)
            readonly property real plotH: Math.max(1, height - inset * 2)

            function xForT(t) { return inset + t * plotW }
            function yForG(g) { return inset + (1.0 - g) * plotH }
            function tForX(x) { return (x - inset) / plotW }
            function gForY(y) { return 1.0 - (y - inset) / plotH }

            Rectangle {
                anchors.fill: parent
                radius: Theme.radiusMd
                color: Theme.panelAccent
                border.width: Theme.borderWidth
                border.color: Theme.panelBorder
            }

            Canvas {
                id: curveCanvas
                anchors.fill: parent
                anchors.margins: plot.inset
                onWidthChanged: requestPaint()
                onHeightChanged: requestPaint()
                onPaint: {
                    const ctx = getContext("2d")
                    ctx.reset()
                    const w = width
                    const h = height
                    if (w < 2 || h < 2)
                        return

                    ctx.strokeStyle = String(Theme.panelBorder)
                    ctx.lineWidth = 1
                    ctx.globalAlpha = 0.45
                    for (let i = 1; i < 4; ++i) {
                        const y = h * i / 4
                        ctx.beginPath()
                        ctx.moveTo(0, y)
                        ctx.lineTo(w, y)
                        ctx.stroke()
                        const x = w * i / 4
                        ctx.beginPath()
                        ctx.moveTo(x, 0)
                        ctx.lineTo(x, h)
                        ctx.stroke()
                    }
                    ctx.globalAlpha = 1

                    if (root.isBezier) {
                        const hd = root.handles
                        const x1 = hd[0] * w, y1 = h * (1.0 - hd[1])
                        const x2 = hd[2] * w, y2 = h * (1.0 - hd[3])

                        const bfill = Theme.primary
                        ctx.fillStyle = Qt.rgba(bfill.r, bfill.g, bfill.b, 0.18)
                        ctx.beginPath()
                        ctx.moveTo(0, h)
                        ctx.lineTo(0, h)
                        ctx.bezierCurveTo(x1, y1, x2, y2, w, 0)
                        ctx.lineTo(w, h)
                        ctx.closePath()
                        ctx.fill()

                        // Handle arms first, so the curve reads on top of them.
                        ctx.strokeStyle = String(Theme.mutedForeground)
                        ctx.lineWidth = 1
                        ctx.beginPath()
                        ctx.moveTo(0, h)
                        ctx.lineTo(x1, y1)
                        ctx.moveTo(w, 0)
                        ctx.lineTo(x2, y2)
                        ctx.stroke()

                        ctx.strokeStyle = String(Theme.primary)
                        ctx.lineWidth = 2
                        ctx.beginPath()
                        ctx.moveTo(0, h)
                        ctx.bezierCurveTo(x1, y1, x2, y2, w, 0)
                        ctx.stroke()
                        return
                    }

                    const pts = root.points
                    if (!pts || pts.length < 2)
                        return

                    const fill = Theme.primary
                    ctx.fillStyle = Qt.rgba(fill.r, fill.g, fill.b, 0.18)
                    ctx.beginPath()
                    ctx.moveTo(0, h)
                    for (let i = 0; i < pts.length; ++i)
                        ctx.lineTo(pts[i].t * w, h * (1.0 - pts[i].g))
                    ctx.lineTo(w, h)
                    ctx.closePath()
                    ctx.fill()

                    ctx.strokeStyle = String(Theme.primary)
                    ctx.lineWidth = 2
                    ctx.beginPath()
                    for (let i = 0; i < pts.length; ++i) {
                        const x = pts[i].t * w
                        const y = h * (1.0 - pts[i].g)
                        if (i === 0)
                            ctx.moveTo(x, y)
                        else
                            ctx.lineTo(x, y)
                    }
                    ctx.stroke()
                }

                Connections {
                    target: root
                    function onPointsChanged() { curveCanvas.requestPaint() }
                    function onSelectedPointChanged() { curveCanvas.requestPaint() }
                    function onHandlesChanged() { curveCanvas.requestPaint() }
                    function onShapeModeChanged() { curveCanvas.requestPaint() }
                    function onSelectedHandleChanged() { curveCanvas.requestPaint() }
                }
            }

            // Background click/double-click under the knobs. Knobs use z:4 so they win the grab.
            MouseArea {
                anchors.fill: parent
                anchors.margins: plot.inset
                z: 1
                acceptedButtons: Qt.LeftButton
                onDoubleClicked: (mouse) => {
                    if (!root.isBezier)
                        root.addPointAt(mouse.x / plot.plotW)
                }
                onClicked: {
                    root.selectedPoint = -1
                    root.selectedHandle = -1
                }
            }

            // model is the count, not the array — replacing points mid-drag must not destroy the
            // knob that owns the active DragHandler (that was making manual edits jump/stick).
            Repeater {
                model: root.isBezier ? 0 : root.points.length

                delegate: Rectangle {
                    id: knob
                    required property int index
                    readonly property var point: root.points[index]
                    readonly property bool isEnd: index === 0 || index === root.points.length - 1

                    width: 14
                    height: 14
                    radius: 7
                    z: 4
                    color: root.selectedPoint === index ? Theme.primary : Theme.panelBackground
                    border.width: 2
                    border.color: Theme.primary
                    x: plot.xForT(point.t) - width / 2
                    y: plot.yForG(point.g) - height / 2

                    // DragHandler reports translation from press; freeze the starting t/g so each
                    // move is absolute from that origin instead of compounding.
                    property real baseT: 0
                    property real baseG: 0

                    TapHandler {
                        onTapped: root.selectedPoint = knob.index
                    }

                    DragHandler {
                        // Corner rails stay at (0,0) and (1,1); only interior knobs move.
                        enabled: !knob.isEnd
                        target: null
                        cursorShape: Qt.PointingHandCursor
                        onActiveChanged: {
                            if (active) {
                                knob.baseT = knob.point.t
                                knob.baseG = knob.point.g
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
                            const lo = next[knob.index - 1].t + 0.002
                            const hi = next[knob.index + 1].t - 0.002
                            point.t = Math.max(lo, Math.min(hi,
                                knob.baseT + translation.x / plot.plotW))
                            point.g = Math.max(0, Math.min(1,
                                knob.baseG - translation.y / plot.plotH))
                            root.points = next
                            // Live audition without round-tripping points (keeps the drag smooth).
                            root.commit(false)
                        }
                    }
                }
            }

            Repeater {
                model: root.isBezier ? 2 : 0

                delegate: Rectangle {
                    id: handleKnob
                    required property int index
                    readonly property real hx: root.handles[index * 2]
                    readonly property real hy: root.handles[index * 2 + 1]

                    width: 14
                    height: 14
                    radius: 7
                    z: 4
                    color: root.selectedHandle === index ? Theme.primary : Theme.panelBackground
                    border.width: 2
                    border.color: Theme.primary
                    x: plot.xForT(hx) - width / 2
                    y: plot.yForG(hy) - height / 2

                    property real baseX: 0
                    property real baseY: 0

                    TapHandler {
                        onTapped: root.selectedHandle = handleKnob.index
                    }

                    DragHandler {
                        target: null
                        cursorShape: Qt.PointingHandCursor
                        onActiveChanged: {
                            if (active) {
                                handleKnob.baseX = handleKnob.hx
                                handleKnob.baseY = handleKnob.hy
                                root.selectedHandle = handleKnob.index
                            } else {
                                root.commitHandles()
                            }
                        }
                        onTranslationChanged: {
                            if (!active)
                                return
                            // x stays inside [0,1] so the cubic remains single-valued; y likewise,
                            // so a fade cannot exceed unity gain. C++ clamps again on receipt.
                            const next = root.handles.slice()
                            next[handleKnob.index * 2] = Math.max(0, Math.min(1,
                                handleKnob.baseX + translation.x / plot.plotW))
                            next[handleKnob.index * 2 + 1] = Math.max(0, Math.min(1,
                                handleKnob.baseY - translation.y / plot.plotH))
                            root.handles = next
                            root.commitHandles(false)
                        }
                    }
                }
            }

            // The pinned cubic anchors. Not draggable — shown so the handle arms have a visible
            // origin rather than appearing to float.
            Repeater {
                model: root.isBezier ? 2 : 0
                delegate: Rectangle {
                    required property int index
                    width: 9
                    height: 9
                    radius: 4.5
                    z: 3
                    color: Theme.primary
                    x: plot.xForT(index === 0 ? 0.0 : 1.0) - width / 2
                    y: plot.yForG(index === 0 ? 0.0 : 1.0) - height / 2
                }
            }

            Keys.onPressed: (event) => {
                if (event.key === Qt.Key_Delete || event.key === Qt.Key_Backspace) {
                    root.removeSelected()
                    event.accepted = true
                }
            }
            focus: true
        }

        Row {
            width: parent.width
            spacing: Theme.spacingMd
            layoutDirection: Qt.RightToLeft

            ThemedButton {
                text: qsTr("Apply")
                onClicked: root.sessionApply()
            }
            ThemedButton {
                text: qsTr("Cancel")
                variant: "ghost"
                onClicked: root.close()
            }
            Item { width: parent.width; height: 1 }
        }
    }
}
