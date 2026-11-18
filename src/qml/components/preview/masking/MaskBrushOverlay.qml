import QtQuick
import QtQuick.Shapes
import Drift

// Paints a raster matte on the target clip's frame. The Lasso, Intelligent Scissors and Magic wand tools
// share this overlay: they write their selection into the same matte as the brush.
//
// Paints a raster matte on the target clip's frame. A stroke is collected by EditorState.masking
// (strokeBegin/Add/End) in clip-frame pixels, drawn live by BrushStrokeItem and committed on release
// as one undo step: left paints (or erases when the Paint/Erase toggle says so), right erases.
Item {
    id: root

    property bool interacting: false

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
    readonly property bool eraseNow: interacting ? strokeErase : EditorState.masking.brushErase
    // Shift-drag resizes the brush instead of painting.
    property bool resizing: false
    property real resizeStartX: 0
    property real resizeStartSize: 0
    property bool sizeLabelShown: false

    function refresh() {
        // Hidden: onVisibleChanged catches up when shown.
        if (!visible || interacting)
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

    // Right button and Alt subtract, Shift adds, otherwise the Paint/Erase toggle decides.
    function eraseFor(button, modifiers) {
        if (button === Qt.RightButton || (modifiers & Qt.AltModifier))
            return true
        return EditorState.masking.brushErase && !(modifiers & Qt.ShiftModifier)
    }

    function resetDrafts() {
        if (interacting && lassoTool)
            interacting = false
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
        root.interacting = true
        return true
    }

    function endStroke() {
        root.touchWaiting = false
        touchDelay.stop()
        if (!root.interacting)
            return
        root.interacting = false
        EditorState.masking.strokeEnd()
    }

    function cancelStroke() {
        root.touchWaiting = false
        touchDelay.stop()
        root.resizing = false
        if (root.interacting)
            EditorState.masking.strokeCancel()
        root.interacting = false
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

    // Keys reach this item only while it has focus, which the scissors tool asks for.
    focus: root.scissorsTool
    onScissorsToolChanged: {
        if (scissorsTool)
            forceActiveFocus()
    }

    Keys.onPressed: (event) => {
        if (!root.scissorsTool)
            return
        switch (event.key) {
        case Qt.Key_Return:
        case Qt.Key_Enter:
            root.closeScissors(EditorState.masking.brushErase)
            event.accepted = true
            break
        case Qt.Key_Backspace:
            root.undoScissorsSeed()
            event.accepted = true
            break
        }
    }

    Connections {
        target: ShellBridge
        function onMaskingCancelRequested() { root.cancelScissors() }
    }

    Timer {
        id: liveTimer
        interval: 30
        onTriggered: root.updateScissorsLive()
    }
    Component.onCompleted: refresh()
    Component.onDestruction: {
        if (interacting)
            endStroke()
        ShellBridge.brushStrokeActive = false
        ShellBridge.pendingPolygonPoints = 0
        EditorState.masking.scissorsReset()
        EditorState.masking.clearStrokePreview()
    }
    onInteractingChanged: ShellBridge.brushStrokeActive = interacting

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
        function onTracksChanged() { root.refresh() }
        function onClipPropertiesPreviewed(trackIndex, clipIndex, keys) {
            for (const k of keys) {
                if (k.startsWith("mask.") || k === "positionX" || k === "positionY" || k === "scaleX"
                        || k === "scaleY" || k === "rotation" || k === "rotationX"
                        || k === "rotationY" || k === "z" || k === "perspective") {
                    root.refresh()
                    return
                }
            }
        }
        function onSelectionChanged() {
            root.cancelScissors()
            root.refresh()
        }
        function onSelectedClipDataChanged() { root.refresh() }
        // A scrub moves the playhead per scroll event; catch up once when it ends.
        function onPlayheadSecondsChanged() {
            if (root.scissorsSeeds.length > 0)
                root.cancelScissors()
            if (!EditorState.scrubbing)
                root.refresh()
        }
        function onScrubbingChanged() {
            if (!EditorState.scrubbing)
                root.refresh()
        }
        function onPlayingChanged() {
            if (!EditorState.playing)
                root.refresh()
        }
    }

    HoverHandler {
        id: brushHover
        onPointChanged: {
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
                strokeColor: Qt.alpha(Theme.c.maskDrawOverlay.stroke, 0.7)
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

        // The circle is the dab's full radius; the inner one marks where the falloff starts.
        Rectangle {
            visible: root.brushTool && root.hovering && root.clipTarget && root.hasFrame
            width: root.brushDiameter
            height: width
            radius: width / 2
            x: root.hoverPoint.x - width / 2
            y: root.hoverPoint.y - height / 2
            color: "transparent"
            border.color: root.eraseNow ? Theme.c.maskBrushOverlay.eraseStroke : Theme.c.maskBrushOverlay.cursor
            border.width: Theme.c.maskBrushOverlay.borderWidth

            Text {
                visible: root.eraseNow
                anchors.centerIn: parent
                text: "−"
                color: Theme.c.maskBrushOverlay.eraseStroke
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeSm
                font.bold: true
            }

            Text {
                visible: root.sizeLabelShown
                anchors.left: parent.right
                anchors.leftMargin: Theme.spacingXs
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("%1 %").arg((EditorState.masking.brushSize * 100).toFixed(1))
                color: Theme.c.maskBrushOverlay.cursor
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
            }
        }

        Rectangle {
            visible: root.brushTool && root.hovering && root.clipTarget && root.hasFrame
            width: root.brushDiameter * EditorState.masking.brushHardness
            height: width
            radius: width / 2
            x: root.hoverPoint.x - width / 2
            y: root.hoverPoint.y - height / 2
            color: "transparent"
            border.color: Qt.alpha(Theme.c.maskBrushOverlay.cursorInner, 0.5)
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

        onPositionChanged: (mouse) => {
            const p = clipFrame.mapFromItem(paintArea, mouse.x, mouse.y)
            root.hoverPoint = Qt.point(p.x, p.y)
            if (root.lassoTool) {
                if (root.interacting && Math.hypot(p.x - root.lassoPoints[root.lassoPoints.length - 1].x,
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
            if (root.resizing) {
                const size = root.resizeStartSize + (p.x - root.resizeStartX) / Math.max(1, clipFrame.width)
                EditorState.masking.brushSize = Math.max(0.002, Math.min(0.5, size))
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
            if (root.interacting)
                EditorState.masking.strokeAdd(p.x, p.y, pressure())
        }
        onPressed: (mouse) => {
            const p = clipFrame.mapFromItem(paintArea, mouse.x, mouse.y)
            if (!root.brushTool) {
                const erase = root.eraseFor(mouse.button, mouse.modifiers)
                if (root.lassoTool) {
                    root.lassoErase = erase
                    root.lassoPoints = [Qt.point(p.x, p.y)]
                    root.interacting = true
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
            if (mouse.button === Qt.LeftButton && (mouse.modifiers & Qt.ShiftModifier)) {
                root.resizing = true
                root.resizeStartX = p.x
                root.resizeStartSize = EditorState.masking.brushSize
                return
            }
            const erase = mouse.button === Qt.RightButton || EditorState.masking.brushErase
            root.touchStroke = !penHandler.active && mouse.source !== Qt.MouseEventNotSynthesized
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
                root.closeScissors(root.eraseFor(mouse.button, mouse.modifiers))
        }
        onReleased: {
            if (root.lassoTool) {
                if (root.interacting && root.lassoPoints.length >= 3)
                    EditorState.masking.lassoFill(root.targetClipId(), root.normalizedPoints(root.lassoPoints), root.lassoErase)
                root.interacting = false
                root.lassoPoints = []
                Qt.callLater(root.refresh)
                return
            }
            if (!root.brushTool)
                return
            if (root.resizing) {
                root.resizing = false
                return
            }
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
            if (root.lassoTool) {
                root.interacting = false
                root.lassoPoints = []
            } else {
                root.cancelStroke()
            }
        }
    }

    Rectangle {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: Theme.spacingLg
        width: hintText.width + Theme.spacingLg * 2
        height: hintText.height + Theme.spacingMd * 2
        radius: Theme.c.maskDrawOverlay.radius
        color: Theme.c.maskDrawOverlay.hintBackground

        Text {
            id: hintText
            anchors.centerIn: parent
            text: !root.clipTarget
                  ? qsTr("Select a video or image clip to paint a mask")
                  : !root.editorState.inside
                    ? qsTr("Move the playhead over the selected clip")
                  : root.lassoTool
                    ? qsTr("Drag to draw an outline, release to fill it · Right-drag or Alt to subtract")
                  : root.scissorsTool
                    ? (root.scissorsSeeds.length === 0
                       ? qsTr("Click along an edge to place points · Right-click or Alt to subtract")
                       : qsTr("%1 points · Click the first point or Enter to close · Backspace to undo · Esc to cancel").arg(root.scissorsSeeds.length))
                  : root.wandTool
                    ? qsTr("Click to select a region of similar colour · Shift adds, Alt or right-click subtracts")
                  : root.editorState.inside
                    ? qsTr("Paint to add · Right-drag or %1 to erase · [ ] or Shift-drag to resize · Ctrl+scroll to zoom").arg(EditorState.shortcutFor("maskingBrushToggleErase"))
                    : qsTr("Move the playhead over the selected clip")
            color: Theme.c.maskDrawOverlay.hintForeground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeXs
        }
    }
}
