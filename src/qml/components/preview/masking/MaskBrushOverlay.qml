import QtQuick
import QtQuick.Shapes
import Drift

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

    property bool strokeErase: false
    property bool hovering: false
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
        else
            EditorState.masking.clearStrokePreview()
    }
    Component.onCompleted: refresh()
    Component.onDestruction: {
        if (interacting)
            endStroke()
        ShellBridge.brushStrokeActive = false
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

        // The circle is the dab's full radius; the inner one marks where the falloff starts.
        Rectangle {
            visible: root.hovering && root.clipTarget
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
            visible: root.hovering && root.clipTarget
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
    MouseArea {
        id: paintArea
        anchors.fill: parent
        enabled: root.clipTarget && root.hasFrame
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        cursorShape: root.hovering ? Qt.BlankCursor : Qt.ArrowCursor
        // A touch stroke can be taken over by the viewport's pinch.
        preventStealing: !root.touchStroke
        onWheel: (wheel) => { wheel.accepted = false }
        onContainsMouseChanged: root.hovering = containsMouse || pressed
        onPressedChanged: root.hovering = containsMouse || pressed

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
        onReleased: {
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
        onCanceled: root.cancelStroke()
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
                  : root.editorState.inside
                    ? qsTr("Paint to add · Right-drag or %1 to erase · [ ] or Shift-drag to resize · Ctrl+scroll to zoom").arg(EditorState.shortcutFor("maskingBrushToggleErase"))
                    : qsTr("Move the playhead over the selected clip")
            color: Theme.c.maskDrawOverlay.hintForeground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeXs
        }
    }
}
