import QtQuick
import QtQuick.Shapes
import Drift

// Draws a new rectangle or ellipse mask on the target clip's frame. The drag shows as a dashed
// outline in clip-frame space; the release commits one mask, then the tool drops back to select so
// the new mask's handles (MaskOverlay) take over.
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

    readonly property bool drawTool: EditorState.preview.maskingTool === "rect"
                                     || EditorState.preview.maskingTool === "ellipse"

    // Polygon: clip-frame pixel vertices, placed one click at a time until closed or cancelled.
    readonly property bool polyTool: EditorState.preview.maskingTool === "polygon"
    readonly property bool polyActive: polyTool && target.kind !== "none"
    property var polyPoints: []
    property point hoverPoint: Qt.point(0, 0)
    readonly property point firstVertex: polyPoints.length > 0 ? polyPoints[0] : Qt.point(0, 0)
    readonly property point lastVertex: polyPoints.length > 0 ? polyPoints[polyPoints.length - 1]
                                                              : Qt.point(0, 0)
    readonly property bool hoverNearFirst: polyPoints.length >= 3
                                           && distanceBetween(hoverPoint, firstVertex) <= 8

    // Set once a press has moved, so a plain click draws nothing.
    property bool drawing: false
    property point dragOrigin: Qt.point(0, 0)
    property rect dragBox: Qt.rect(0, 0, 0, 0)

    readonly property real canvasW: Math.max(1, editorState.canvasWidth || 1)
    readonly property real canvasH: Math.max(1, editorState.canvasHeight || 1)
    readonly property real sx: root.width / canvasW
    readonly property real sy: root.height / canvasH

    function refresh() {
        // Hidden: onVisibleChanged catches up when shown.
        if (!visible || interacting)
            return
        // Playhead ticks ~60 Hz; the frame and target are only needed when a drag starts.
        if (EditorState.playing)
            return
        editorState = EditorState.preview.maskingFrame()
        target = EditorState.masking.maskTarget()
    }

    // The box between the press and the pointer, in clip-frame pixels. Shift makes it a square in
    // canvas pixels; Alt takes the press point as its centre.
    function boxFrom(x0, y0, x1, y1, modifiers) {
        let dx = x1 - x0
        let dy = y1 - y0
        if (modifiers & Qt.ShiftModifier) {
            const side = Math.max(Math.abs(dx) / root.sx, Math.abs(dy) / root.sy)
            dx = (dx < 0 ? -side : side) * root.sx
            dy = (dy < 0 ? -side : side) * root.sy
        }
        if (modifiers & Qt.AltModifier) {
            return Qt.rect(x0 - Math.abs(dx), y0 - Math.abs(dy),
                           2 * Math.abs(dx), 2 * Math.abs(dy))
        }
        return Qt.rect(Math.min(x0, x0 + dx), Math.min(y0, y0 + dy), Math.abs(dx), Math.abs(dy))
    }

    function distanceBetween(a, b) {
        return Math.hypot(a.x - b.x, a.y - b.y)
    }

    // Shift holds the new segment to 45-degree steps from the previous vertex.
    function constrainedPoint(p, modifiers) {
        if (!(modifiers & Qt.ShiftModifier) || polyPoints.length === 0)
            return p
        const step = Math.PI / 4
        const angle = Math.round(Math.atan2(p.y - lastVertex.y, p.x - lastVertex.x) / step) * step
        const len = distanceBetween(p, lastVertex)
        return Qt.point(lastVertex.x + Math.cos(angle) * len, lastVertex.y + Math.sin(angle) * len)
    }

    function polyPress(p, modifiers) {
        if (polyPoints.length >= 3 && distanceBetween(p, firstVertex) <= 8) {
            finishPolygon()
            return
        }
        const q = constrainedPoint(p, modifiers)
        // A double-click's second press lands on the vertex just placed; it must not add a duplicate.
        if (polyPoints.length > 0 && distanceBetween(q, lastVertex) < 3)
            return
        polyPoints = polyPoints.concat([q])
    }

    function removeLastPolyPoint() {
        if (polyPoints.length > 0)
            polyPoints = polyPoints.slice(0, -1)
    }

    function finishPolygon() {
        if (polyPoints.length < 3 || target.kind === "none")
            return
        const fw = Math.max(1, clipFrame.width)
        const fh = Math.max(1, clipFrame.height)
        const pts = polyPoints.map(p => ({ "x": p.x / fw, "y": p.y / fh }))
        const t = EditorState.masking.maskTarget()
        if (t.kind === "none")
            return
        EditorState.masking.addPolygonMask(t.track, t.clip, pts)
        polyPoints = []
        EditorState.preview.maskingTool = ""
    }

    function cancelPolygon() {
        polyPoints = []
    }

    onVisibleChanged: if (visible) refresh()
    Component.onCompleted: refresh()
    Component.onDestruction: ShellBridge.pendingPolygonPoints = 0

    // Escape from the Masking panel's shortcut handler, which runs before this item sees the key.
    Connections {
        target: ShellBridge
        function onMaskingCancelRequested() { root.cancelPolygon() }
    }

    // Keys reach this item only while it has focus, which the polygon tool asks for.
    focus: root.polyTool
    onPolyToolChanged: {
        if (polyTool)
            forceActiveFocus()
        else
            cancelPolygon()
    }
    onPolyPointsChanged: ShellBridge.pendingPolygonPoints = polyPoints.length

    Keys.onPressed: (event) => {
        switch (event.key) {
        case Qt.Key_Return:
        case Qt.Key_Enter:
            root.finishPolygon()
            event.accepted = true
            break
        case Qt.Key_Escape:
            if (root.polyPoints.length === 0)
                return
            root.cancelPolygon()
            event.accepted = true
            break
        case Qt.Key_Backspace:
            root.removeLastPolyPoint()
            event.accepted = true
            break
        }
    }

    Connections {
        target: EditorState
        function onTracksChanged() { root.refresh() }
        function onClipPropertiesPreviewed(trackIndex, clipIndex, keys) {
            for (const k of keys) {
                if (k.startsWith("mask.") || k === "x" || k === "y" || k === "width"
                        || k === "height" || k === "rotation" || k === "rotationX"
                        || k === "rotationY" || k === "z" || k === "perspective") {
                    root.refresh()
                    return
                }
            }
        }
        function onSelectionChanged() { root.refresh() }
        function onSelectedClipDataChanged() { root.refresh() }
        // A scrub moves the playhead per scroll event; catch up once when it ends.
        function onPlayheadSecondsChanged() {
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

        Shape {
            anchors.fill: parent
            visible: root.drawing && EditorState.preview.maskingTool === "rect"

            ShapePath {
                strokeColor: Theme.c.maskDrawOverlay.stroke
                strokeWidth: Theme.c.maskDrawOverlay.borderWidth
                strokeStyle: ShapePath.DashLine
                dashPattern: [4, 3]
                fillColor: Qt.alpha(Theme.c.maskDrawOverlay.fill, 0.15)

                PathRectangle {
                    x: root.dragBox.x
                    y: root.dragBox.y
                    width: root.dragBox.width
                    height: root.dragBox.height
                }
            }
        }

        Shape {
            anchors.fill: parent
            visible: root.drawing && EditorState.preview.maskingTool === "ellipse"

            ShapePath {
                strokeColor: Theme.c.maskDrawOverlay.stroke
                strokeWidth: Theme.c.maskDrawOverlay.borderWidth
                strokeStyle: ShapePath.DashLine
                dashPattern: [4, 3]
                fillColor: Qt.alpha(Theme.c.maskDrawOverlay.fill, 0.15)

                PathMove {
                    x: root.dragBox.x + root.dragBox.width
                    y: root.dragBox.y + root.dragBox.height / 2
                }
                PathAngleArc {
                    centerX: root.dragBox.x + root.dragBox.width / 2
                    centerY: root.dragBox.y + root.dragBox.height / 2
                    radiusX: root.dragBox.width / 2
                    radiusY: root.dragBox.height / 2
                    startAngle: 0
                    sweepAngle: 360
                }
            }
        }

        Shape {
            anchors.fill: parent
            visible: root.polyActive && root.polyPoints.length > 0

            ShapePath {
                strokeColor: Theme.c.maskDrawOverlay.stroke
                strokeWidth: Theme.c.maskDrawOverlay.borderWidth
                fillColor: "transparent"

                PathPolyline {
                    path: root.polyPoints
                }
            }
        }

        Shape {
            anchors.fill: parent
            visible: root.polyActive && root.polyPoints.length > 0

            ShapePath {
                strokeColor: Theme.c.maskDrawOverlay.stroke
                strokeWidth: Theme.c.maskDrawOverlay.borderWidth
                strokeStyle: ShapePath.DashLine
                dashPattern: [4, 3]
                fillColor: "transparent"

                PathMove {
                    x: root.lastVertex.x
                    y: root.lastVertex.y
                }
                PathLine {
                    x: root.hoverPoint.x
                    y: root.hoverPoint.y
                }
            }

            ShapePath {
                strokeColor: Qt.alpha(Theme.c.maskDrawOverlay.stroke, 0.5)
                strokeWidth: Theme.c.maskDrawOverlay.borderWidth
                strokeStyle: ShapePath.DashLine
                dashPattern: [4, 3]
                fillColor: "transparent"

                PathMove {
                    x: root.hoverPoint.x
                    y: root.hoverPoint.y
                }
                PathLine {
                    x: root.firstVertex.x
                    y: root.firstVertex.y
                }
            }
        }

        Repeater {
            model: root.polyActive ? root.polyPoints : []

            delegate: Rectangle {
                id: vertexHandle
                required property var modelData
                required property int index
                readonly property bool closing: index === 0 && root.hoverNearFirst
                readonly property real side: index === 0 ? 11 : 7

                x: modelData.x - side / 2
                y: modelData.y - side / 2
                width: side
                height: side
                color: closing ? Theme.c.maskDrawOverlay.closeHint : Theme.c.maskDrawOverlay.stroke
                border.color: Theme.c.maskDrawOverlay.hintBackground
                border.width: Theme.c.maskDrawOverlay.borderWidth
            }
        }
    }

    // Nothing to draw on: the tool is up, but no clip is selected, or the selected one is off the playhead.
    // Both draw tools take input on the whole overlay rather than the clip frame, so a mask can be
    // started off the clip. Positions are mapped into clip-frame pixels, the frame mask coordinates
    // are normalized to.
    MouseArea {
        id: boxArea
        anchors.fill: parent
        enabled: root.drawTool && root.target.kind !== "none" && root.hasFrame
        cursorShape: Qt.CrossCursor
        preventStealing: true
        onWheel: (wheel) => { wheel.accepted = false }

        onPressed: (mouse) => {
            root.interacting = true
            root.dragOrigin = clipFrame.mapFromItem(boxArea, mouse.x, mouse.y)
        }
        onPositionChanged: (mouse) => {
            if (!root.interacting)
                return
            const p = clipFrame.mapFromItem(boxArea, mouse.x, mouse.y)
            root.drawing = true
            root.dragBox = root.boxFrom(root.dragOrigin.x, root.dragOrigin.y,
                                        p.x, p.y, mouse.modifiers)
        }
        onReleased: (mouse) => {
            if (!root.interacting)
                return
            const p = clipFrame.mapFromItem(boxArea, mouse.x, mouse.y)
            const box = root.boxFrom(root.dragOrigin.x, root.dragOrigin.y,
                                     p.x, p.y, mouse.modifiers)
            root.interacting = false
            root.drawing = false
            // Smaller than this is a stray click, not a mask.
            if (box.width < 4 || box.height < 4)
                return
            const t = EditorState.masking.maskTarget()
            if (t.kind === "none")
                return
            const fw = Math.max(1, clipFrame.width)
            const fh = Math.max(1, clipFrame.height)
            EditorState.masking.addShapeMask(t.track, t.clip,
                                             EditorState.preview.maskingTool === "rect"
                                                 ? "rectangle" : "ellipse",
                                             (box.x + box.width / 2) / fw,
                                             (box.y + box.height / 2) / fh,
                                             box.width / fw, box.height / fh)
            EditorState.preview.maskingTool = ""
        }
        onCanceled: {
            root.interacting = false
            root.drawing = false
        }
    }

    MouseArea {
        id: polyArea
        anchors.fill: parent
        enabled: root.polyActive && root.hasFrame
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        cursorShape: Qt.CrossCursor
        onWheel: (wheel) => { wheel.accepted = false }

        onPositionChanged: (mouse) => {
            const p = clipFrame.mapFromItem(polyArea, mouse.x, mouse.y)
            root.hoverPoint = root.constrainedPoint(Qt.point(p.x, p.y), mouse.modifiers)
        }
        onPressed: (mouse) => {
            if (mouse.button === Qt.RightButton) {
                root.removeLastPolyPoint()
                return
            }
            const p = clipFrame.mapFromItem(polyArea, mouse.x, mouse.y)
            root.polyPress(Qt.point(p.x, p.y), mouse.modifiers)
        }
        // The second press of a double-click is ignored by polyPress, so this only has to close.
        onDoubleClicked: (mouse) => {
            if (mouse.button === Qt.LeftButton)
                root.finishPolygon()
        }
    }

    Rectangle {
        visible: root.target.kind === "none" || !root.editorState.inside
        anchors.centerIn: parent
        width: hintText.width + Theme.spacingLg * 2
        height: hintText.height + Theme.spacingMd * 2
        radius: Theme.c.maskDrawOverlay.radius
        color: Theme.c.maskDrawOverlay.hintBackground

        Text {
            id: hintText
            anchors.centerIn: parent
            text: root.target.kind === "none"
                  ? qsTr("Select a clip on the timeline to draw a mask")
                  : qsTr("Move the playhead over the selected clip")
            color: Theme.c.maskDrawOverlay.hintForeground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeXs
        }
    }

    Rectangle {
        visible: root.polyActive && root.editorState.inside
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: Theme.spacingLg
        width: polyHintText.width + Theme.spacingLg * 2
        height: polyHintText.height + Theme.spacingMd * 2
        radius: Theme.c.maskDrawOverlay.radius
        color: Theme.c.maskDrawOverlay.hintBackground

        Text {
            id: polyHintText
            anchors.centerIn: parent
            text: root.polyPoints.length === 0
                  ? qsTr("Click to place points. Click the first point or press Enter to close.")
                  : qsTr("%1 points · Enter to close · Backspace to undo · Esc to cancel").arg(root.polyPoints.length)
            color: Theme.c.maskDrawOverlay.hintForeground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeXs
        }
    }

    Rectangle {
        readonly property bool replaces: root.target.kind === "adjustment"
                                         && EditorState.masking.maskStack(root.target.track, root.target.clip).length > 0
        visible: root.drawTool && root.target.kind !== "none" && root.editorState.inside && !root.drawing
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: Theme.spacingLg
        width: drawHintText.width + Theme.spacingLg * 2
        height: drawHintText.height + Theme.spacingMd * 2
        radius: Theme.c.maskDrawOverlay.radius
        color: Theme.c.maskDrawOverlay.hintBackground

        Text {
            id: drawHintText
            anchors.centerIn: parent
            text: parent.replaces ? qsTr("Drawing replaces this layer's mask")
                                  : qsTr("Drag to draw · Shift for square/circle · Alt from centre")
            color: Theme.c.maskDrawOverlay.hintForeground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeXs
        }
    }
}
