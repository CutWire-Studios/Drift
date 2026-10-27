import QtQuick
import QtQuick.Shapes
import Drift

// Paints a raster matte on the target clip's frame. A stroke is drawn live in clip-frame pixels and
// committed on release as one undo step: left paints (or erases when the Paint/Erase toggle says so),
// right erases.
Item {
    id: root

    property bool interacting: false

    // Snapshot from AppController::maskEditorState(), the same frame MaskOverlay places against.
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

    // Clip-frame pixels, in the order the pointer passed them.
    property var stroke: []
    property bool strokeErase: false
    property bool hovering: false
    property point hoverPoint: Qt.point(0, 0)
    // Brush diameter in clip-frame pixels; the size is a fraction of the frame width.
    readonly property real brushDiameter: EditorState.masking.brushSize * clipFrame.width

    function refresh() {
        // Hidden: onVisibleChanged catches up when shown.
        if (!visible || interacting)
            return
        // Playhead ticks ~60 Hz; the frame and target are only needed when a stroke starts.
        if (EditorState.playing)
            return
        editorState = EditorState.preview.maskEditorState()
        target = EditorState.masking.maskTarget()
    }

    function commitStroke(erase) {
        const pts = root.stroke
        root.stroke = []
        if (pts.length === 0 || !root.clipTarget)
            return
        const fw = Math.max(1, clipFrame.width)
        const fh = Math.max(1, clipFrame.height)
        EditorState.masking.brushStroke(root.target.track, root.target.clip,
                                        pts.map(p => ({ "x": p.x / fw, "y": p.y / fh })), erase)
    }

    onVisibleChanged: if (visible) refresh()
    Component.onCompleted: refresh()

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

        // Coordinates here are clip-frame pixels, the frame mask coordinates are normalized to.
        MouseArea {
            anchors.fill: parent
            enabled: root.clipTarget
            hoverEnabled: true
            acceptedButtons: Qt.LeftButton | Qt.RightButton
            cursorShape: containsMouse ? Qt.BlankCursor : Qt.ArrowCursor
            preventStealing: true
            onContainsMouseChanged: root.hovering = containsMouse

            onPositionChanged: (mouse) => {
                root.hoverPoint = Qt.point(mouse.x, mouse.y)
                if (!root.interacting)
                    return
                const last = root.stroke[root.stroke.length - 1]
                if (Math.hypot(mouse.x - last.x, mouse.y - last.y) >= 2)
                    root.stroke = root.stroke.concat([Qt.point(mouse.x, mouse.y)])
            }
            onPressed: (mouse) => {
                root.interacting = true
                root.strokeErase = mouse.button === Qt.RightButton || EditorState.masking.brushErase
                root.stroke = [Qt.point(mouse.x, mouse.y)]
            }
            onReleased: {
                if (!root.interacting)
                    return
                root.interacting = false
                root.commitStroke(root.strokeErase)
                Qt.callLater(root.refresh)
            }
            onCanceled: {
                root.interacting = false
                root.stroke = []
            }
        }

        Shape {
            anchors.fill: parent
            visible: root.interacting && root.stroke.length > 1

            ShapePath {
                strokeColor: Qt.alpha(root.strokeErase ? Theme.c.maskBrushOverlay.eraseStroke
                                                       : Theme.c.maskBrushOverlay.stroke, 0.5)
                strokeWidth: root.brushDiameter
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin
                fillColor: "transparent"

                PathPolyline {
                    path: root.stroke
                }
            }
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
            border.color: Theme.c.maskBrushOverlay.cursor
            border.width: Theme.c.maskBrushOverlay.borderWidth
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
            text: root.clipTarget
                  ? qsTr("Paint to add · Right-drag to erase · [ ] size")
                  : qsTr("Select a video or image clip to paint a mask")
            color: Theme.c.maskDrawOverlay.hintForeground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeXs
        }
    }
}
