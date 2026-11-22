pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Shapes
import Drift

// The pen and path editor for Shape Path clips and Freeform masks. The geometry, hit-testing, undo
// and keyframe writes all live in EditorState.masking.pathEdit (PathEditSession); this draws the
// outline, points and handles from it and forwards the pointer.
//
// Backends: "shape" edits the selected Path clip, "mask" the Freeform mask adjustment `clipId`
// (hosted by MaskOverlay, which passes the frame it already holds), and "draft" lays out an outline
// that becomes a new Path clip (draftTarget "shape") or polygon mask (draftTarget "mask").
//
// Gestures (Figma / Adobe convention):
//   pen      click = corner point, click-drag = smooth point with symmetric handles; click the first
//            point to close; Enter or Esc end an open path; Backspace removes the last point
//   edit     hover an edge for a + ghost and click to add a point (the shape does not change), drag an
//            edge to bend it; drag points and handles; Alt-click a point to switch corner / smooth;
//            Alt-drag a handle to break it; Shift holds handles to 45 degrees and points to an axis;
//            Ctrl suspends snapping; Shift / Ctrl-click and the marquee multi-select
//   keys     Delete heals the gap, Shift+Delete does not; Ctrl+A selects every point; arrows nudge 1 px
//            (Shift 10), a burst is one undo step; Esc cancels the drag in flight, then leaves the tool
// On a finger the context bar's chips stand in for Shift / Alt / Ctrl, a tap on an edge adds a point,
// holding a point opens its action bar, and a loupe follows the point being dragged.
ManipOverlay {
    id: root

    // The preview canvas this overlay mirrors; unset, the overlay itself is the canvas rect.
    property Item canvas
    // Sizing and the finger-oriented chrome; each press's own device still decides the behaviour.
    property bool touch: Theme.touchInput

    property string backend: "shape"
    // For "mask" and a mask draft: the frame the host resolved (maskEditorState / maskingFrame).
    property var frameData: ({})
    property string clipId: ""
    property string draftTarget: "shape"
    property bool animated: backend === "shape" && !!((EditorState.selectedClipData || ({})).shapeStyle || ({})).pathAnimated
    property bool loupeEnabled: true
    property Item loupeSource: canvas ? canvas : Window.contentItem

    readonly property var session: EditorState.masking.pathEdit
    readonly property bool drafting: backend === "draft"
    readonly property int trackIdx: EditorState.selectedTrack
    readonly property int clipIdx: EditorState.selectedClip

    property var ed: ({})
    readonly property bool hasFrame: !!ed.hasFrame
    readonly property real canvasW: Math.max(1, ed.canvasWidth || 1)
    readonly property real canvasH: Math.max(1, ed.canvasHeight || 1)

    property var hoverInfo: ({ "kind": "none", "cursor": "", "c": -1, "v": -1, "x": 0, "y": 0, "nearFirst": false })
    property bool pointBarOpen: false
    property point pointBarAt: Qt.point(0, 0)

    readonly property var vtx: { void session.revision; return session.vertexList() }
    readonly property var hdl: { void session.revision; return session.handleList() }
    readonly property string outlineD: { void session.revision; return session.outlineSvg() }
    readonly property string handleD: { void session.revision; return session.handleSvg() }
    readonly property string rubberD: { void session.revision; void hoverInfo; return session.rubberSvg() }

    // The tool is done (Enter / Esc with nothing left to cancel, or Done): the host closes it.
    signal exited()

    refreshPolicy: "settled"
    refreshOnClipData: true
    onRefreshRequested: refresh()

    nudgeEnabled: hasFrame && session.attached && !drafting && session.vertexCount > 0
    nudgeText: qsTr("Nudge points")
    chipModifiers: touchBarItem ? touchBarItem.modifiers : null
    touchBar: touch ? touchBarComponent : null
    hint: drafting ? (touch ? qsTr("Tap for a corner point, drag for a smooth one. Tap the first point to close.")
                            : qsTr("Click for a corner point, drag for a smooth one. Shift snaps to 45 degrees. Click the first point to close, Enter to finish, Esc to cancel."))
          : session.penMode ? qsTr("Click or drag to extend the path. Enter or Esc ends it, Backspace removes the last point.")
          : (touch ? qsTr("Drag points and handles. Tap an edge to add a point, hold a point for more.")
                   : qsTr("Drag points and handles. Click an edge to add a point, Alt-click a point to switch corner and smooth, Alt-drag a handle to break it, Delete heals."))
          + (animated ? "  " + qsTr("This path is keyframed: edits are stored as a key at the playhead.") : "")
    hintVisible: hasFrame && !pointBarOpen

    snapLinesX: session.snapGuideX >= 0 ? [session.toViewport(Qt.point(session.snapGuideX, 0.5)).x] : []
    snapLinesY: session.snapGuideY >= 0 ? [session.toViewport(Qt.point(0.5, session.snapGuideY)).y] : []

    focus: true

    // ---- frame and session ----

    property bool owned: false

    function readFrame() {
        if (backend === "shape") {
            ed = EditorState.preview.trackerFrameOf(trackIdx, clipIdx)
        } else if (backend === "mask" || draftTarget === "mask") {
            ed = frameData || ({})
        } else {
            const w = EditorState.projectFile.projectWidth()
            const h = EditorState.projectFile.projectHeight()
            ed = { "hasFrame": w > 0, "x": 0, "y": 0, "width": w, "height": h, "canvasWidth": w, "canvasHeight": h }
        }
    }

    function syncView() {
        if (!hasFrame || !visible)
            return
        plane.frame = ed
        const c = plane.corners(root)
        session.setView(c[0], c[1], c[2], c[3], Math.max(1, ed.width || canvasW), Math.max(1, ed.height || canvasH))
    }

    function ensureAttached() {
        if (backend === "shape") {
            owned = true
            session.attachShape(trackIdx, clipIdx)
        } else if (backend === "mask") {
            if (clipId !== "") {
                owned = true
                session.attachMask(clipId)
            } else if (owned) {
                release()
            }
        } else {
            owned = true
            session.attachDraft(draftTarget === "mask")
        }
    }

    function release() {
        if (!owned)
            return
        owned = false
        session.detach()
    }

    function refresh() {
        if (!visible || interacting || EditorState.playing)
            return
        readFrame()
        ensureAttached()
        syncView()
    }

    function scheduleSync() {
        Qt.callLater(syncView)
    }

    onBackendChanged: refresh()
    onClipIdChanged: refresh()
    onFrameDataChanged: refresh()
    onVisibleChanged: {
        if (visible)
            refresh()
        else
            release()
    }
    onWidthChanged: scheduleSync()
    onHeightChanged: scheduleSync()

    // ---- keys and actions ----

    function textFieldFocused() {
        return Window.activeFocusItem && Window.activeFocusItem.cursorPosition !== undefined
    }

    function finishDraft() {
        const n = session.vertexCount
        if (draftTarget === "mask") {
            if (n < 3)
                return
            const t = EditorState.masking.maskTarget()
            if (t.kind === "none")
                return
            EditorState.masking.addPolygonMask(t.track, t.clip, session.draftVertices())
            session.clearDraft()
            EditorState.preview.maskingTool = ""
            return
        }
        if (n < 2)
            return
        const made = EditorState.addPathShapeClip(session.draftContoursScaled(ed.width || canvasW, ed.height || canvasH), -1, -1)
        session.clearDraft()
        EditorState.preview.shapePathMode = made && made.track !== undefined ? "edit" : ""
    }

    // Done: commits a draft with enough points, ends an open pen path, else leaves the tool.
    function finish() {
        if (drafting) {
            if (session.vertexCount >= (draftTarget === "mask" ? 3 : 2))
                finishDraft()
            else
                exited()
        } else if (session.penActive) {
            session.penEnd()
        } else {
            exited()
        }
    }

    // One Esc press: the drag in flight, else the pen path or draft, else the selection, else the
    // tool. False when there was nothing of this tool's to cancel (a mask hosted elsewhere).
    function escapeStep() {
        if (EditorState.drag.interacting) {
            EditorState.drag.cancel()
            return true
        }
        if (pointBarOpen) {
            pointBarOpen = false
            return true
        }
        if (drafting) {
            if (session.vertexCount > 0)
                session.clearDraft()
            else
                exited()
            return true
        }
        if (session.penActive) {
            session.penEnd()
            return true
        }
        if (session.penMode) {
            session.penMode = false
            return true
        }
        if (session.selectionCount > 0) {
            session.clearSelection()
            return true
        }
        if (backend === "shape") {
            exited()
            return true
        }
        return false
    }

    function deleteKey(heal) {
        if (drafting || session.penActive)
            return session.penUndo()
        return session.deleteSelected(heal)
    }

    onEscaped: escapeStep()
    // Enter on a drag commits it (ManipOverlay does that before it emits confirmed); it does not also finish the tool.
    property bool justCommitted: false
    onConfirmed: {
        if (!justCommitted)
            finish()
    }
    onNudged: (dx, dy) => session.nudge(dx, dy)

    onUnhandledKey: (event) => {
        if (event.key === Qt.Key_Backspace) {
            event.accepted = deleteKey(session.healOnDelete)
        } else if (event.key === Qt.Key_Delete) {
            event.accepted = deleteKey(session.healOnDelete && !(event.modifiers & Qt.ShiftModifier))
        } else if (event.key === Qt.Key_A && (event.modifiers & Qt.ControlModifier) && !drafting) {
            session.selectAll()
            event.accepted = true
        }
    }

    readonly property string scope: "path"
    readonly property int priority: 1

    function handleAction(id) {
        if (!visible || !hasFrame || !session.attached || textFieldFocused())
            return false
        if (id === "clearSelection")
            return escapeStep()
        const engaged = ShellBridge.previewHovered || activeFocus
        if (!engaged)
            return false
        if (id === "delete")
            return deleteKey(session.healOnDelete)
        if (id === "path.deleteNoHeal")
            return deleteKey(false)
        if (id === "path.penTool") {
            if (!session.canPen)
                return false
            session.penMode = !session.penMode
            return true
        }
        if (id === "path.convertPoint")
            return session.toggleSmoothSelected()
        if (id === "selectAll" && !drafting) {
            session.selectAll()
            return true
        }
        return false
    }

    function flagsFor(mods) {
        let f = mods
        const c = chipModifiers
        if (c) {
            if (c.constrain)
                f |= Qt.ShiftModifier
            if (c.breakTangent)
                f |= Qt.AltModifier
            if (c.noSnap)
                f |= Qt.ControlModifier
        }
        return f
    }

    Component.onCompleted: {
        ShellBridge.registerActionHandler(root)
        refresh()
        forceActiveFocus()
    }
    Component.onDestruction: {
        ShellBridge.unregisterActionHandler(root)
        release()
        if (draftTarget === "mask")
            ShellBridge.pendingPolygonPoints = 0
    }

    Connections {
        target: root.session
        function onFrameInvalidated() {
            if (root.backend !== "shape" || !root.visible)
                return
            root.readFrame()
            root.syncView()
        }
        function onDraftClosed() {
            root.finishDraft()
        }
        function onChanged() {
            if (root.drafting && root.draftTarget === "mask")
                ShellBridge.pendingPolygonPoints = root.session.vertexCount
        }
    }

    Connections {
        target: ShellBridge
        function onMaskingCancelRequested() {
            if (root.drafting && root.draftTarget === "mask")
                root.session.clearDraft()
        }
    }

    Connections {
        target: EditorState.drag
        function onCommitted() {
            root.pointBarOpen = false
            root.justCommitted = true
            Qt.callLater(() => root.justCommitted = false)
        }
    }

    Component {
        id: touchBarComponent

        TouchContextBar {
            id: contextBar
            visible: root.hasFrame
            width: Math.min(implicitWidth, root.width - Theme.spacingLg * 2)
            chips: ["constrain", "breakHandle", "snap"]

            PathActionChips {
                editor: root
            }
        }
    }

    // ---- drawing ----

    Item {
        id: stage
        x: root.canvas ? root.canvas.x : 0
        y: root.canvas ? root.canvas.y : 0
        width: root.canvas ? root.canvas.width : root.width
        height: root.canvas ? root.canvas.height : root.height
        onXChanged: root.scheduleSync()
        onYChanged: root.scheduleSync()
        onWidthChanged: root.scheduleSync()
        onHeightChanged: root.scheduleSync()

        PathPlane {
            id: plane
            sx: stage.width / plane.canvasW
            sy: stage.height / plane.canvasH
        }
    }

    Shape {
        anchors.fill: parent
        visible: root.hasFrame
        preferredRendererType: Shape.CurveRenderer

        ShapePath {
            strokeColor: Theme.c.pathEditor.handleLine
            strokeWidth: 1
            fillColor: "transparent"
            PathSvg { path: root.handleD }
        }
        ShapePath {
            strokeColor: Theme.c.pathEditor.outline
            strokeWidth: Theme.c.pathEditor.outlineWidth
            fillColor: "transparent"
            PathSvg { path: root.outlineD }
        }
        ShapePath {
            strokeColor: Theme.c.pathEditor.rubberBand
            strokeWidth: Theme.c.pathEditor.outlineWidth
            strokeStyle: ShapePath.DashLine
            dashPattern: [4, 3]
            fillColor: "transparent"
            PathSvg { path: root.rubberD }
        }
    }

    // The point a click on this edge would add.
    Rectangle {
        visible: root.hasFrame && root.hoverInfo.kind === "segment" && !root.interacting && !root.session.penMode
        x: root.hoverInfo.x - width / 2
        y: root.hoverInfo.y - height / 2
        width: 14
        height: 14
        radius: width / 2
        color: Theme.c.pathEditor.ghostBackground
        border.width: 1
        border.color: Theme.c.pathEditor.ghostBorder

        Rectangle {
            anchors.centerIn: parent
            width: 8
            height: 1.5
            color: Theme.c.pathEditor.ghostForeground
        }
        Rectangle {
            anchors.centerIn: parent
            width: 1.5
            height: 8
            color: Theme.c.pathEditor.ghostForeground
        }
    }

    Repeater {
        model: root.hasFrame ? root.hdl.length : 0

        delegate: HandleKnob {
            id: handleKnob
            required property int index
            readonly property var info: root.hdl[index] || ({ "x": 0, "y": 0 })
            enabled: false
            shape: "dot"
            visualSize: root.touch ? 11 : 7
            x: info.x - width / 2
            y: info.y - height / 2
        }
    }

    Repeater {
        model: root.hasFrame ? root.vtx.length : 0

        delegate: HandleKnob {
            id: vertexKnob
            required property int index
            readonly property var info: root.vtx[index] || ({ "x": 0, "y": 0, "smooth": false, "selected": false, "first": false,
                                                               "c": -1, "v": -1 })
            readonly property bool closing: root.session.penActive && info.first && root.hoverInfo.nearFirst
            enabled: false
            shape: info.smooth ? "dot" : "square"
            selected: info.selected
            highlighted: closing || (root.hoverInfo.kind === "vertex" && root.hoverInfo.c === info.c
                                     && root.hoverInfo.v === info.v)
            visualSize: root.touch ? 14 : 9
            x: info.x - width / 2
            y: info.y - height / 2
        }
    }

    Rectangle {
        visible: root.session.marqueeActive
        x: root.session.marquee.x
        y: root.session.marquee.y
        width: root.session.marquee.width
        height: root.session.marquee.height
        color: Qt.rgba(Theme.c.timelineMarqueeRect.background.r, Theme.c.timelineMarqueeRect.background.g,
                       Theme.c.timelineMarqueeRect.background.b, 0.12)
        border.width: Theme.c.timelineMarqueeRect.borderWidth
        border.color: Theme.c.timelineMarqueeRect.border
    }

    Rectangle {
        visible: !root.hasFrame && !root.drafting
        anchors.centerIn: parent
        width: outsideText.implicitWidth + Theme.spacingLg * 2
        height: outsideText.implicitHeight + Theme.spacingMd * 2
        radius: Theme.c.manipHint.radius
        color: Theme.c.manipHint.background

        Text {
            id: outsideText
            anchors.centerIn: parent
            text: qsTr("Move the playhead over the selected clip")
            color: Theme.c.manipHint.foreground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeXs
        }
    }

    Loupe {
        source: root.loupeSource
        at: root.pointer
        sourcePoint: root.loupeSource ? root.mapToItem(root.loupeSource, root.pointer.x, root.pointer.y) : root.pointer
        active: root.loupeEnabled && root.touchDrag && root.interacting && EditorState.drag.kind !== "nudge"
    }

    PathPointBar {
        session: root.session
        anchorPoint: root.pointBarAt
        open: root.pointBarOpen && root.touch
    }

    Rectangle {
        visible: !root.touch && root.hasFrame && !root.interacting && root.backend !== "mask"
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: Theme.spacingLg
        width: actions.implicitWidth + Theme.spacingMd * 2
        height: actions.implicitHeight + Theme.spacingSm * 2
        radius: Theme.c.manipContextBar.radius
        color: Theme.c.manipContextBar.background
        border.width: Theme.c.manipContextBar.borderWidth
        border.color: Theme.c.manipContextBar.border
        z: 95

        MouseArea {
            anchors.fill: parent
        }

        PathActionChips {
            id: actions
            anchors.centerIn: parent
            editor: root
        }
    }

    // ---- pointer ----

    function updateHover(pos, mods) {
        if (!hasFrame || !session.attached)
            return
        hoverInfo = session.hover(pos, flagsFor(mods), DragSession.Mouse)
    }

    Item {
        id: cursorHost
        anchors.fill: parent
    }

    ToolCursor {
        target: cursorHost
        shape: root.hasFrame ? (root.hoverInfo.cursor || "") : ""
    }

    HoverHandler {
        id: hover
        acceptedDevices: PointerDevice.Mouse | PointerDevice.Stylus
        enabled: root.hasFrame
        onPointChanged: root.updateHover(hover.point.position, hover.point.modifiers)
        onHoveredChanged: {
            if (!hover.hovered)
                root.hoverInfo = { "kind": "none", "cursor": "", "c": -1, "v": -1, "x": 0, "y": 0, "nearFirst": false }
        }
    }

    // A press on empty space in the edit tool starts the marquee instead (see below), so the area
    // lets it fall through to the handlers beneath and to the host's own items.
    MouseArea {
        id: area
        anchors.fill: parent
        enabled: root.hasFrame && root.session.attached
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        preventStealing: !pressedByTouch

        property bool pressedByTouch: false
        property bool noneTaken: false
        property point lastPos: Qt.point(0, 0)
        property int lastMods: 0

        function device() {
            return pressedByTouch ? DragSession.Touch : DragSession.Mouse
        }

        onPressed: (mouse) => {
            root.forceActiveFocus()
            pressedByTouch = mouse.source !== Qt.MouseEventNotSynthesized
            noneTaken = false
            root.pointBarOpen = false
            lastPos = Qt.point(mouse.x, mouse.y)
            lastMods = mouse.modifiers
            const kind = root.session.press(lastPos, root.flagsFor(mouse.modifiers), device(), mouse.button)
            if (kind === "none") {
                noneTaken = true
                mouse.accepted = false
                return
            }
            root.pointer = lastPos
            if (pressedByTouch)
                Haptics.pickUp()
        }

        onPositionChanged: (mouse) => {
            lastPos = Qt.point(mouse.x, mouse.y)
            lastMods = mouse.modifiers
            root.pointer = lastPos
            root.session.move(lastPos, root.flagsFor(mouse.modifiers))
        }

        function finish() {
            root.session.release(lastPos, root.flagsFor(lastMods))
        }

        onReleased: (mouse) => {
            lastPos = Qt.point(mouse.x, mouse.y)
            lastMods = mouse.modifiers
            finish()
        }
        onCanceled: finish()

        onPressAndHold: (mouse) => {
            if (!pressedByTouch || root.interacting || mouse.button !== Qt.LeftButton)
                return
            const hit = root.session.hover(lastPos, 0, DragSession.Touch)
            if (hit.kind !== "vertex")
                return
            root.session.cancelPress()
            root.session.selectOnly(hit.c, hit.v)
            root.pointBarAt = Qt.point(hit.x, hit.y)
            root.pointBarOpen = true
            Haptics.pickUp()
        }

        onWheel: (wheel) => { wheel.accepted = false }
    }

    DragHandler {
        id: marqueeDrag
        target: null
        enabled: root.hasFrame && root.session.attached && !root.session.penMode && !root.drafting
                 && !area.pressed && !root.interacting

        onActiveChanged: {
            if (active && area.noneTaken) {
                const p = root.mapFromItem(null, marqueeDrag.centroid.scenePressPosition.x,
                                           marqueeDrag.centroid.scenePressPosition.y)
                root.session.marqueeBegin(p, root.flagsFor(marqueeDrag.centroid.modifiers))
            } else if (!active) {
                root.session.marqueeEnd()
            }
        }

        onCentroidChanged: {
            if (active && root.session.marqueeActive) {
                root.session.marqueeTo(root.mapFromItem(null, marqueeDrag.centroid.scenePosition.x,
                                                        marqueeDrag.centroid.scenePosition.y))
            }
        }
    }

    TapHandler {
        id: emptyTap
        enabled: root.hasFrame && root.session.attached && !root.drafting
        onTapped: {
            if (area.noneTaken && !(emptyTap.point.modifiers & (Qt.ShiftModifier | Qt.ControlModifier)))
                root.session.clearSelection()
        }
    }
}
