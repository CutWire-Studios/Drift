import QtQuick
import QtQuick.Shapes
import Drift

// Paints a raster matte on the target clip's frame. The Lasso, Intelligent Scissors and Magic wand tools
// share this overlay: they write their selection into the same matte as the brush.
//
// A stroke is collected by EditorState.masking (strokeBegin/Add/End) in clip-frame pixels, drawn live by
// BrushStrokeItem and committed on release as one undo step. The mode is the Include / Exclude chips
// (EditorState.masking.brushErase); Alt, right-drag and a stylus's eraser end exclude for one stroke.
// Resizing: Shift-drag sets the size, Alt+right-drag sets size (across) and hardness (up). Esc, via the
// masking cancel request, drops the stroke in flight (or restores the size mid-resize) without an undo step.
// Smoothing is a lazy string: StrokeSmoother trails the cursor and the preview line shows the string.
// On a finger the context bar carries the mode, Paint / Navigate and the presets, and the side sliders
// carry size and opacity.
ManipOverlay {
    id: root

    // Sizing and the finger-oriented chrome; each press's own device still decides how it paints.
    property bool touch: Theme.touchInput
    // The viewport a one-finger drag pans while the Navigate chip is on (phone). Two fingers always
    // pinch it, and a stylus always paints.
    property var navTarget: null
    property bool paintMode: true

    // A stroke, or a lasso outline, is being drawn. (ManipOverlay.interacting is the drag session's.)
    property bool painting: false

    refreshPolicy: "settled"
    previewKeys: ["mask.*", "positionX", "positionY", "scaleX", "scaleY", "rotation", "rotationX",
                  "rotationY", "z", "perspective"]
    refreshOnClipData: true
    onRefreshRequested: refresh()

    // Snapshot from PreviewController::maskingFrame(): the selection's frame, and whether it is under the
    // playhead. Nothing is placed or accepted while it is not.
    property var editorState: ({})
    readonly property bool hasFrame: !!editorState.hasFrame
    // The selection's mask target (MaskingController::maskTarget). Assigned, not bound, so it
    // refreshes on the same signals as editorState.
    property var target: ({ kind: "none" })
    readonly property bool clipTarget: target.kind === "clip"

    readonly property real canvasW: Math.max(1, editorState.canvasWidth || 1)
    readonly property real canvasH: Math.max(1, editorState.canvasHeight || 1)
    readonly property real sx: root.width / canvasW
    readonly property real sy: root.height / canvasH

    readonly property string tool: EditorState.preview.maskingTool
    readonly property bool brushTool: tool === "brush"
    readonly property bool lassoTool: tool === "lasso"
    readonly property bool scissorsTool: tool === "scissors"
    readonly property bool wandTool: tool === "wand"

    // Lasso outline in clip-frame pixels.
    property var lassoPoints: []
    property bool lassoErase: false
    // Intelligent Scissors: clicked seeds and the edge-following segment between each pair (all
    // normalized to the clip frame), plus the live segment from the last seed to the cursor.
    property var scissorsSeeds: []
    property var scissorsSegments: []
    property var scissorsLive: []
    property point scissorsCursor: Qt.point(0, 0)
    readonly property bool scissorsNearFirst: scissorsSeeds.length >= 2 && scissorsTool
        && Math.hypot(scissorsSeeds[0].x * clipFrame.width - scissorsCursor.x,
                      scissorsSeeds[0].y * clipFrame.height - scissorsCursor.y) < 10
    readonly property var scissorsOutline: {
        let points = []
        for (const seg of scissorsSegments)
            points = points.concat(seg)
        return points
    }

    property bool strokeErase: false
    // Alt held, or the stylus's eraser end over the canvas, while hovering.
    property bool hoverExclude: false
    // A passive handler keeps reporting the pointer whatever the paint area grabs or loses, so the ring
    // cannot stay behind where a stroke ended.
    readonly property bool hovering: (brushHover.hovered || paintArea.pressed) && paintArea.enabled
    // A touch stroke waits briefly (or for 6 px of movement) so the first finger of a pinch does not
    // leave a dab; this is where it started.
    property bool touchWaiting: false
    property bool touchStroke: false
    property point touchStart: Qt.point(0, 0)
    property point hoverPoint: Qt.point(0, 0)
    // Brush diameter in clip-frame pixels; the size is a fraction of the frame width.
    readonly property real brushDiameter: EditorState.masking.brushSize * clipFrame.width
    readonly property bool eraseNow: painting || resizing ? strokeErase : (EditorState.masking.brushErase || hoverExclude)
    readonly property color modeColor: eraseNow ? Theme.c.maskBrushOverlay.eraseStroke : Theme.c.maskBrushOverlay.stroke
    // "size" (Shift-drag) or "both" (Alt+right-drag) resizes the brush instead of painting.
    property string resizeMode: ""
    readonly property bool resizing: resizeMode !== ""
    property real resizeStartX: 0
    property real resizeStartY: 0
    property real resizeStartSize: 0
    property real resizeStartHardness: 0
    property point resizeOrigin: Qt.point(0, 0)
    property bool sizeLabelShown: false
    // Where the cursor and the string's head are, in clip-frame pixels, while a stroke is drawn.
    property point cursorFrame: Qt.point(0, 0)
    property real lastPressure: 1
    property bool navigating: false
    property point navLast: Qt.point(0, 0)

    focus: root.scissorsTool
    hudEditable: false
    nudgeEnabled: false
    hintVisible: !painting && !resizing
    hint: !clipTarget
          ? qsTr("Select a video or image clip to paint a mask")
          : !editorState.inside
            ? qsTr("Move the playhead over the selected clip")
          : lassoTool
            ? qsTr("Drag to draw an outline, release to fill it. Right-drag or Alt to subtract.")
          : scissorsTool
            ? (scissorsSeeds.length === 0
               ? qsTr("Click along an edge to place points. Right-click or Alt to subtract.")
               : qsTr("%1 points. Click the first point or Enter to close, Backspace to undo, Esc to cancel.").arg(scissorsSeeds.length))
          : wandTool
            ? qsTr("Click to select a region of similar colour. Shift adds, Alt or right-click subtracts.")
          : touch
            ? qsTr("Paint with one finger, or switch to Navigate to pan. Two fingers always zoom and pan.")
            : qsTr("Paint to add. Right-drag, Alt or %1 to subtract. [ ] sizes, Shift+[ ] sets hardness. Shift-drag or Alt+right-drag resizes.").arg(EditorState.shortcutFor("masking.brushToggleErase"))
    hud: Component {
        LiveReadout {
            visible: (root.resizing || (root.sizeLabelShown && root.hovering)) && root.hasFrame
            text: qsTr("Size %1 %   Hardness %2 %").arg((EditorState.masking.brushSize * 100).toFixed(1))
                      .arg(Math.round(EditorState.masking.brushHardness * 100))
            pointer: root.resizing ? root.resizeOrigin : root.pointer
            touch: false
        }
    }
    touchBar: touch ? touchBarComponent : null

    function refresh() {
        // Hidden: onVisibleChanged catches up when shown.
        if (!visible || painting)
            return
        // Playhead ticks ~60 Hz; the frame and target are only needed when a stroke starts.
        if (EditorState.playing)
            return
        editorState = EditorState.preview.maskingFrame()
        target = EditorState.masking.maskTarget()
    }

    function framePoints(points) {
        return points.map(q => Qt.point(q.x * clipFrame.width, q.y * clipFrame.height))
    }

    function normalizedPoints(points) {
        return points.map(q => ({ x: q.x / clipFrame.width, y: q.y / clipFrame.height }))
    }

    function targetClipId() {
        const t = EditorState.masking.maskTarget()
        return t.kind === "clip" ? EditorState.masking.clipIdAt(t.track, t.clip) : ""
    }

    // The right button, Alt and the stylus's eraser end subtract for one stroke; Shift adds; otherwise
    // the Include / Exclude chips decide.
    function eraseFor(button, modifiers, eraserEnd) {
        if (button === Qt.RightButton || eraserEnd || modifiersFor(modifiers).exclude)
            return true
        return EditorState.masking.brushErase && !(modifiers & Qt.ShiftModifier)
    }

    function resetDrafts() {
        if (painting && lassoTool)
            painting = false
        lassoPoints = []
        cancelScissors()
    }

    function cancelScissors() {
        scissorsSeeds = []
        scissorsSegments = []
        scissorsLive = []
        liveTimer.stop()
        EditorState.masking.scissorsReset()
        ShellBridge.pendingPolygonPoints = 0
    }

    function syncPendingPoints() {
        ShellBridge.pendingPolygonPoints = scissorsSeeds.length
    }

    function updateScissorsLive() {
        if (scissorsSeeds.length === 0) {
            scissorsLive = []
            return
        }
        const last = scissorsSeeds[scissorsSeeds.length - 1]
        const end = EditorState.masking.scissorsSnap(scissorsCursor.x / clipFrame.width,
                                                     scissorsCursor.y / clipFrame.height)
        scissorsLive = EditorState.masking.scissorsPath(last.x, last.y, end.x, end.y)
    }

    function scissorsClick(p, erase) {
        const id = targetClipId()
        if (id === "" || !EditorState.masking.scissorsPrepare(id))
            return
        if (scissorsNearFirst || (scissorsSeeds.length >= 2
                && Math.hypot(scissorsSeeds[0].x * clipFrame.width - p.x,
                              scissorsSeeds[0].y * clipFrame.height - p.y) < 10)) {
            closeScissors(erase)
            return
        }
        const seed = EditorState.masking.scissorsSnap(p.x / clipFrame.width, p.y / clipFrame.height)
        if (scissorsSeeds.length > 0) {
            const last = scissorsSeeds[scissorsSeeds.length - 1]
            scissorsSegments = scissorsSegments.concat([EditorState.masking.scissorsPath(last.x, last.y, seed.x, seed.y)])
        }
        scissorsSeeds = scissorsSeeds.concat([Qt.point(seed.x, seed.y)])
        scissorsLive = []
        syncPendingPoints()
    }

    function undoScissorsSeed() {
        if (scissorsSeeds.length === 0)
            return
        scissorsSeeds = scissorsSeeds.slice(0, -1)
        scissorsSegments = scissorsSegments.slice(0, -1)
        scissorsLive = []
        syncPendingPoints()
        if (scissorsSeeds.length === 0)
            cancelScissors()
    }

    function closeScissors(erase) {
        if (scissorsSeeds.length < 2)
            return
        const first = scissorsSeeds[0]
        const last = scissorsSeeds[scissorsSeeds.length - 1]
        const closing = EditorState.masking.scissorsPath(last.x, last.y, first.x, first.y)
        EditorState.masking.scissorsFill(targetClipId(), scissorsOutline.concat(closing), erase)
        cancelScissors()
    }

    function beginStroke(p, erase, pressure) {
        const t = EditorState.masking.maskTarget()
        const id = t.kind === "clip" ? EditorState.masking.clipIdAt(t.track, t.clip) : ""
        if (id === "" || !EditorState.masking.strokeBegin(id, p.x, p.y, clipFrame.width, clipFrame.height,
                                                          erase, pressure))
            return false
        root.strokeErase = erase
        root.painting = true
        smoother.begin(p.x, p.y)
        root.cursorFrame = Qt.point(p.x, p.y)
        root.lastPressure = pressure
        return true
    }

    // The stroke takes the string's head, not the cursor; with smoothing at 0 they are the same.
    function addStrokePoint(p, pressure) {
        root.cursorFrame = Qt.point(p.x, p.y)
        root.lastPressure = pressure
        const head = smoother.follow(p.x, p.y, EditorState.masking.brushSmoothing)
        EditorState.masking.strokeAdd(head.x, head.y, pressure)
    }

    function endStroke() {
        root.touchWaiting = false
        touchDelay.stop()
        if (!root.painting)
            return
        root.painting = false
        if (EditorState.masking.brushSmoothing > 0)
            EditorState.masking.strokeAdd(root.cursorFrame.x, root.cursorFrame.y, root.lastPressure)
        EditorState.masking.strokeEnd()
    }

    // Esc: the stroke in flight is dropped, an outline abandoned, a resize put back. No undo entry.
    function cancelStroke() {
        root.touchWaiting = false
        touchDelay.stop()
        if (root.resizing) {
            EditorState.masking.brushSize = root.resizeStartSize
            EditorState.masking.brushHardness = root.resizeStartHardness
            root.resizeMode = ""
        }
        if (root.painting) {
            if (!root.lassoTool)
                EditorState.masking.strokeCancel()
            root.painting = false
            root.lassoPoints = []
        }
    }

    function beginResize(mode, p, mouse) {
        root.resizeMode = mode
        root.resizeStartX = p.x
        root.resizeStartY = p.y
        root.resizeStartSize = EditorState.masking.brushSize
        root.resizeStartHardness = EditorState.masking.brushHardness
        root.resizeOrigin = Qt.point(mouse.x, mouse.y)
        root.strokeErase = root.eraseFor(Qt.LeftButton, mouse.modifiers, false)
    }

    function applyResize(p) {
        const size = root.resizeStartSize + (p.x - root.resizeStartX) / Math.max(1, clipFrame.width)
        EditorState.masking.brushSize = Math.max(0.002, Math.min(0.5, size))
        if (root.resizeMode === "both")
            EditorState.masking.brushHardness = Math.max(0, Math.min(1, root.resizeStartHardness - (p.y - root.resizeStartY) / 150))
    }

    // Keys reach this item only while it has focus, which the scissors tool asks for.
    onConfirmed: {
        if (root.scissorsTool)
            root.closeScissors(EditorState.masking.brushErase)
    }

    onUnhandledKey: (event) => {
        if (root.scissorsTool && event.key === Qt.Key_Backspace) {
            root.undoScissorsSeed()
            event.accepted = true
        }
    }

    onVisibleChanged: {
        if (visible)
            refresh()
        else {
            EditorState.masking.clearStrokePreview()
            resetDrafts()
        }
    }

    onToolChanged: resetDrafts()

    onScissorsToolChanged: {
        if (scissorsTool)
            forceActiveFocus()
    }

    Connections {
        target: ShellBridge
        function onMaskingCancelRequested() {
            root.cancelStroke()
            root.cancelScissors()
        }
    }

    Timer {
        id: liveTimer
        interval: 30
        onTriggered: root.updateScissorsLive()
    }
    Component.onCompleted: refresh()
    Component.onDestruction: {
        if (painting)
            endStroke()
        ShellBridge.brushStrokeActive = false
        ShellBridge.pendingPolygonPoints = 0
        EditorState.masking.scissorsReset()
        EditorState.masking.clearStrokePreview()
    }
    onPaintingChanged: ShellBridge.brushStrokeActive = painting

    Timer {
        id: touchDelay
        interval: 60
        onTriggered: {
            if (root.touchWaiting) {
                root.touchWaiting = false
                root.beginStroke(root.touchStart, root.strokeErase, 1)
            }
        }
    }

    Timer {
        id: sizeLabelTimer
        interval: 800
        onTriggered: root.sizeLabelShown = false
    }

    Connections {
        target: EditorState.masking
        function onBrushSettingsChanged() {
            root.sizeLabelShown = true
            sizeLabelTimer.restart()
        }
    }

    Connections {
        target: EditorState
        function onSelectionChanged() { root.cancelScissors() }
        function onPlayheadSecondsChanged() {
            if (root.scissorsSeeds.length > 0)
                root.cancelScissors()
        }
    }

    StrokeSmoother {
        id: smoother
    }

    HoverHandler {
        id: brushHover
        onPointChanged: {
            root.hoverExclude = brushHover.point.device.pointerType === PointerDevice.Eraser
                                || modifiersFor(brushHover.point.modifiers).exclude
            if (!paintArea.pressed) {
                const p = clipFrame.mapFromItem(root, point.position.x, point.position.y)
                root.hoverPoint = Qt.point(p.x, p.y)
            }
        }
    }

    // The clip's destination rect on the canvas, rotated with it, as MaskOverlay places it.
    Item {
        id: clipFrame
        visible: root.hasFrame
        x: (root.editorState.x || 0) * root.sx
        y: (root.editorState.y || 0) * root.sy
        width: Math.max(1, (root.editorState.width || root.canvasW) * root.sx)
        height: Math.max(1, (root.editorState.height || root.canvasH) * root.sy)
        transformOrigin: Item.Center
        // A tilted clip frame goes through the clip's perspective instead of the flat spin.
        readonly property bool is3d: (root.editorState.rotationX || 0) !== 0
                                     || (root.editorState.rotationY || 0) !== 0
                                     || (root.editorState.z || 0) !== 0
                                     || root.editorState.anchorX !== undefined || root.editorState.anchorY !== undefined
        // Moved by a transform layer: composed through that parent as well, flat or not.
        readonly property bool hasParent: root.editorState.parentActive === true
        rotation: is3d || hasParent ? 0 : (root.editorState.rotation || 0)
        transform: Matrix4x4 {
            matrix: clipFrame.is3d || clipFrame.hasParent
                    ? EditorState.preview.clipPoseMatrix(root.editorState, root.editorState.x || 0,
                                                        root.editorState.y || 0,
                                                        root.editorState.width || root.canvasW,
                                                        root.editorState.height || root.canvasH,
                                                        root.editorState.rotation || 0,
                                                        root.sx, root.sy)
                    : Qt.matrix4x4()
        }

        MaskOverlayClipBounds { }

        Repeater {
            model: EditorState.masking.pendingStrokeCount

            delegate: BrushStrokeItem {
                required property int index
                anchors.fill: parent
                masking: EditorState.masking
                strokeIndex: index
                color: erase ? Theme.c.maskBrushOverlay.eraseStroke : Theme.c.maskBrushOverlay.stroke
                opacity: strokeOpacity
                layer.enabled: true
                layer.samples: 4
            }
        }

        BrushStrokeItem {
            anchors.fill: parent
            masking: EditorState.masking
            strokeIndex: -1
            color: erase ? Theme.c.maskBrushOverlay.eraseStroke : Theme.c.maskBrushOverlay.stroke
            opacity: strokeOpacity
            layer.enabled: true
            layer.samples: 4
        }

        Shape {
            anchors.fill: parent
            visible: root.lassoTool && root.lassoPoints.length >= 2

            ShapePath {
                strokeColor: root.lassoErase ? Theme.c.maskBrushOverlay.eraseStroke : Theme.c.maskDrawOverlay.stroke
                strokeWidth: Theme.c.maskDrawOverlay.borderWidth
                fillColor: "transparent"
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                PathPolyline { path: root.lassoPoints.concat(root.lassoPoints.length > 0 ? [root.lassoPoints[0]] : []) }
            }
        }

        Shape {
            anchors.fill: parent
            visible: root.scissorsTool && (root.scissorsOutline.length > 1 || root.scissorsLive.length > 1)

            ShapePath {
                strokeColor: Theme.c.maskDrawOverlay.stroke
                strokeWidth: Theme.c.maskDrawOverlay.borderWidth
                fillColor: "transparent"
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                PathPolyline { path: root.framePoints(root.scissorsOutline) }
            }

            ShapePath {
                strokeColor: Qt.alpha(Theme.c.maskDrawOverlay.stroke, Theme.c.maskDrawOverlay.strokeOpacity)
                strokeWidth: Theme.c.maskDrawOverlay.borderWidth
                strokeStyle: ShapePath.DashLine
                dashPattern: [4, 3]
                fillColor: "transparent"
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                PathPolyline { path: root.framePoints(root.scissorsLive) }
            }
        }

        Repeater {
            model: root.scissorsTool ? root.scissorsSeeds : []

            delegate: Rectangle {
                id: seedHandle
                required property point modelData
                required property int index
                readonly property bool closing: index === 0 && root.scissorsNearFirst
                readonly property real side: index === 0 ? 11 : 7

                x: modelData.x * clipFrame.width - side / 2
                y: modelData.y * clipFrame.height - side / 2
                width: side
                height: side
                color: closing ? Theme.c.maskDrawOverlay.closeHint : Theme.c.maskDrawOverlay.stroke
                border.color: Theme.c.maskDrawOverlay.hintBackground
                border.width: Theme.c.maskDrawOverlay.borderWidth
            }
        }

        Shape {
            anchors.fill: parent
            visible: root.brushTool && root.painting && EditorState.masking.brushSmoothing > 0

            ShapePath {
                strokeColor: Qt.alpha(Theme.c.maskBrushOverlay.cursor, Theme.c.maskBrushOverlay.cursorOpacity)
                strokeWidth: Theme.c.maskBrushOverlay.borderWidth
                strokeStyle: ShapePath.DashLine
                dashPattern: [3, 3]
                fillColor: "transparent"
                startX: smoother.head.x
                startY: smoother.head.y
                PathLine {
                    x: root.cursorFrame.x
                    y: root.cursorFrame.y
                }
            }
        }

        // The circle is the dab's full radius; the inner one marks where the falloff starts. Its colour and
        // the sign on the badge say which way the next stroke goes.
        Rectangle {
            id: ring
            visible: root.brushTool && root.hovering && root.clipTarget && root.hasFrame
            width: root.brushDiameter
            height: width
            radius: width / 2
            x: root.hoverPoint.x - width / 2
            y: root.hoverPoint.y - height / 2
            color: "transparent"
            border.color: root.modeColor
            border.width: Theme.c.maskBrushOverlay.borderWidth

            Rectangle {
                readonly property real reach: Math.max(ring.radius, 8) * 0.7071
                width: 14
                height: 14
                radius: Theme.radiusPill
                x: ring.width / 2 + reach - width / 2
                y: ring.height / 2 + reach - height / 2
                color: root.modeColor

                Text {
                    anchors.centerIn: parent
                    text: root.eraseNow ? "\u2212" : "+"
                    color: Theme.c.maskAiOverlay.pointBorder
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeXs
                    font.bold: true
                }
            }
        }

        Rectangle {
            visible: ring.visible
            width: root.brushDiameter * EditorState.masking.brushHardness
            height: width
            radius: Theme.radiusPill
            x: root.hoverPoint.x - width / 2
            y: root.hoverPoint.y - height / 2
            color: "transparent"
            border.color: Qt.alpha(Theme.c.maskBrushOverlay.cursorInner, Theme.c.maskBrushOverlay.cursorInnerOpacity)
            border.width: Theme.c.maskBrushOverlay.borderWidth
        }
    }

    // Fills the whole overlay rather than the clip frame, so a stroke can start outside the frame
    // and the cursor ring survives the pointer leaving it mid-stroke. Positions are mapped into
    // clip-frame pixels, the frame mask coordinates are normalized to.
    ToolCursor {
        target: paintArea
        shape: root.scissorsTool ? (root.scissorsNearFirst ? "polygonClose" : "polygon") : "brush"
    }

    MouseArea {
        id: paintArea
        anchors.fill: parent
        enabled: root.clipTarget && root.hasFrame
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        // A touch stroke can be taken over by the viewport's pinch.
        preventStealing: !root.touchStroke
        onWheel: (wheel) => { wheel.accepted = false }

        // Only a pen reports pressure; a mouse or finger paints at full size.
        PointHandler {
            id: penHandler
            acceptedDevices: PointerDevice.Stylus | PointerDevice.Airbrush
            target: null
        }

        function pressure() {
            return penHandler.active ? penHandler.point.pressure : 1
        }

        function eraserEnd() {
            return penHandler.active && penHandler.point.device.pointerType === PointerDevice.Eraser
        }

        onPositionChanged: (mouse) => {
            const p = clipFrame.mapFromItem(paintArea, mouse.x, mouse.y)
            if (root.resizing) {
                root.applyResize(p)
                return
            }
            root.pointer = Qt.point(mouse.x, mouse.y)
            root.hoverPoint = Qt.point(p.x, p.y)
            if (root.navigating) {
                const s = paintArea.mapToItem(null, mouse.x, mouse.y)
                if (root.navTarget) {
                    root.navTarget.panX += s.x - root.navLast.x
                    root.navTarget.panY += s.y - root.navLast.y
                }
                root.navLast = s
                return
            }
            if (root.lassoTool) {
                if (root.painting && Math.hypot(p.x - root.lassoPoints[root.lassoPoints.length - 1].x,
                                                p.y - root.lassoPoints[root.lassoPoints.length - 1].y) >= 2)
                    root.lassoPoints = root.lassoPoints.concat([Qt.point(p.x, p.y)])
                return
            }
            if (root.scissorsTool) {
                root.scissorsCursor = Qt.point(p.x, p.y)
                if (root.scissorsSeeds.length > 0 && !liveTimer.running)
                    liveTimer.start()
                return
            }
            if (root.touchWaiting) {
                if (Math.hypot(p.x - root.touchStart.x, p.y - root.touchStart.y) < 6)
                    return
                root.touchWaiting = false
                touchDelay.stop()
                if (!root.beginStroke(root.touchStart, root.strokeErase, 1))
                    return
            }
            if (root.painting)
                root.addStrokePoint(p, pressure())
        }
        onPressed: (mouse) => {
            if (root.scissorsTool)
                root.forceActiveFocus()
            const p = clipFrame.mapFromItem(paintArea, mouse.x, mouse.y)
            root.pointer = Qt.point(mouse.x, mouse.y)
            root.touchStroke = !penHandler.active && mouse.source !== Qt.MouseEventNotSynthesized
            if (root.touchStroke && !root.paintMode) {
                root.navigating = true
                root.navLast = paintArea.mapToItem(null, mouse.x, mouse.y)
                return
            }
            const erase = root.eraseFor(mouse.button, mouse.modifiers, eraserEnd())
            if (!root.brushTool) {
                if (root.lassoTool) {
                    root.lassoErase = erase
                    root.lassoPoints = [Qt.point(p.x, p.y)]
                    root.painting = true
                } else if (root.scissorsTool) {
                    root.scissorsClick(p, erase)
                } else if (root.wandTool) {
                    const id = root.targetClipId()
                    if (id !== "")
                        EditorState.masking.wandSelect(id, p.x / clipFrame.width, p.y / clipFrame.height, erase)
                    Qt.callLater(root.refresh)
                }
                return
            }
            if (mouse.button === Qt.LeftButton && (mouse.modifiers & Qt.ShiftModifier) && !root.touchStroke) {
                root.beginResize("size", p, mouse)
                return
            }
            if (mouse.button === Qt.RightButton && (mouse.modifiers & Qt.AltModifier)) {
                root.beginResize("both", p, mouse)
                return
            }
            if (root.touchStroke) {
                root.strokeErase = erase
                root.touchStart = Qt.point(p.x, p.y)
                root.touchWaiting = true
                touchDelay.restart()
                return
            }
            root.beginStroke(Qt.point(p.x, p.y), erase, pressure())
        }
        onDoubleClicked: (mouse) => {
            if (root.scissorsTool)
                root.closeScissors(root.eraseFor(mouse.button, mouse.modifiers, false))
        }
        onReleased: {
            root.touchStroke = false
            if (root.navigating) {
                root.navigating = false
                return
            }
            if (root.resizing) {
                root.resizeMode = ""
                return
            }
            if (root.lassoTool) {
                if (root.painting && root.lassoPoints.length >= 3)
                    EditorState.masking.lassoFill(root.targetClipId(), root.normalizedPoints(root.lassoPoints), root.lassoErase)
                root.painting = false
                root.lassoPoints = []
                Qt.callLater(root.refresh)
                return
            }
            if (!root.brushTool)
                return
            if (root.touchWaiting) {
                root.touchWaiting = false
                touchDelay.stop()
                if (!root.beginStroke(root.touchStart, root.strokeErase, 1))
                    return
            }
            root.endStroke()
            Qt.callLater(root.refresh)
        }
        onCanceled: {
            root.touchStroke = false
            root.navigating = false
            if (root.lassoTool) {
                root.painting = false
                root.lassoPoints = []
            } else {
                root.cancelStroke()
            }
        }
    }

    Rectangle {
        visible: !root.touch && root.clipTarget && root.hasFrame && !root.painting && !root.resizing
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: Theme.spacingLg
        width: modeRow.implicitWidth + Theme.spacingMd * 2
        height: modeRow.implicitHeight + Theme.spacingSm * 2
        radius: Theme.c.manipContextBar.radius
        color: Theme.c.manipContextBar.background
        border.width: Theme.c.manipContextBar.borderWidth
        border.color: Theme.c.manipContextBar.border
        z: 95

        MouseArea {
            anchors.fill: parent
        }

        MaskModeChips {
            id: modeRow
            anchors.centerIn: parent
        }
    }

    BrushSideSliders {
        // Beside the canvas when the viewport has room there, so the picture stays clear.
        readonly property var cv: root.navTarget ? root.navTarget.canvas : null
        readonly property real canvasRight: cv ? cv.x + cv.width : root.width
        readonly property bool outside: root.navTarget && root.navTarget.width - canvasRight >= width + Theme.spacingSm * 2
        visible: root.touch && root.brushTool && root.clipTarget && root.hasFrame
        x: outside ? root.width + Theme.spacingSm : root.width - width - Theme.spacingSm
        anchors.verticalCenter: parent.verticalCenter
        z: 95
    }

    Component {
        id: touchBarComponent

        Column {
            id: bars
            visible: root.clipTarget && root.hasFrame
            width: root.width
            spacing: Theme.spacingSm

            TouchContextBar {
                x: (bars.width - width) / 2
                chips: []

                MaskModeChips {
                    touch: true
                }

                ThemedChip {
                    anchors.verticalCenter: parent.verticalCenter
                    chipHeight: Theme.androidMinTouchTarget
                    text: qsTr("Paint")
                    selected: root.paintMode
                    tooltip: qsTr("One finger paints. Two fingers always zoom and pan")
                    onClicked: root.paintMode = true
                }

                ThemedChip {
                    anchors.verticalCenter: parent.verticalCenter
                    chipHeight: Theme.androidMinTouchTarget
                    text: qsTr("Navigate")
                    selected: !root.paintMode
                    tooltip: qsTr("One finger pans the preview. A stylus still paints")
                    onClicked: root.paintMode = false
                }
            }

            TouchContextBar {
                x: (bars.width - width) / 2
                visible: root.brushTool
                chips: []

                Repeater {
                    model: EditorState.masking.brushPresets()

                    ThemedChip {
                        required property var modelData
                        anchors.verticalCenter: parent.verticalCenter
                        chipHeight: Theme.androidMinTouchTarget
                        text: modelData.name
                        variant: "outline"
                        selected: EditorState.masking.brushPreset === modelData.id
                        onClicked: EditorState.masking.applyBrushPreset(modelData.id)
                    }
                }
            }
        }
    }
}
