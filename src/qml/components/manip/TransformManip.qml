pragma ComponentBehavior: Bound
import QtQuick
import Drift

// Transform tool: move, resize, rotate and anchor grips for the clips visible at the playhead,
// the group frame for a multi-selection, the marquee, and the 3D gizmo. One implementation for the
// desktop and the phone; `touch` only changes sizing, the context bar and haptics, while what a
// press does depends on the device that made it.
//
// Sits outside the (clipped) canvas rect, mirroring its geometry, so grips on a clip that runs past
// a canvas edge stay drawn and grabbable instead of being cut away with the frame. Geometry and
// visibility are driven by PreviewToolHost.
//
// Gestures (Figma / Adobe convention):
//   move     drag the body; Shift locks to an axis, Ctrl/Cmd suspends snapping, arrows nudge 1 px
//            (Shift 10), a burst of nudges is one undo step
//   resize   corners and edges; Alt scales about the pivot, Ctrl suspends snapping. Footage corners
//            hold the ratio and Shift frees them, text and shape corners are free and Shift holds it
//   rotate   the stalk handle or just outside a corner; free, Shift steps 15 degrees
//   anchor   the crosshair; Shift lands it on one of the nine box points
//   Esc reverts the drag with no undo entry; a typed number replaces the value being dragged
// On a finger there is no Shift: the context bar's chips stand in (Lock ratio, From centre, Snap),
// rotation and the anchor are pulled in by detents with a tick, and Add selects more clips.
ManipOverlay {
    id: root

    // The preview canvas this overlay mirrors; the overlay itself spans the whole viewport.
    property Item canvas
    readonly property alias stage: stage

    // Sizing and the finger-oriented chrome; the device of each press still decides the behaviour.
    property bool touch: Theme.touchInput

    property var overlayClips: []

    // A text clip was double-clicked or double-tapped: the panel opens its inline editor.
    signal textEditRequested(int track, int clip)

    // In 3D mode the scene camera, as a gizmo box at its eye (see previewCameraBox).
    property var cameraBox: null

    // The two views edit differently: the camera output takes the flat grips (move, resize,
    // rotate) on every clip, the 3D scene only the 3D gizmo, and only on 3D layers and the camera.
    // The host says which viewport this overlay is on: 0 the camera's, 1 the editor's.
    property int viewport: 0
    readonly property bool mode3d: viewport === 1

    // Boxes that exist to be reshaped; every other kind has an aspect its corners hold.
    readonly property var freeKinds: ["text", "subtitle", "shape", "transform", "adjustment"]

    refreshPolicy: "settled"
    previewKeys: ["positionX", "positionY", "scaleX", "scaleY", "rotation", "rotationX", "rotationY",
                  "z", "perspective", "anchorX", "anchorY", "frameW", "frameH"]
    onRefreshRequested: refreshOverlay()

    nudgeEnabled: !mode3d && (groupMode || selectedBox !== null)
    nudgeText: qsTr("Nudge clip")
    hudEditable: activeBox !== null ? !touchDrag : gizmo.dragging !== ""
    hudUnit: activeBox ? activeBox.typedUnit : (gizmo.dragging !== "" ? gizmoUnit : "")
    chipModifiers: touchBarItem && !mode3d ? touchBarItem.modifiers : null
    touchBar: touch ? (mode3d ? touchBar3dComponent : touchBarComponent) : null

    // What a number typed into the HUD of a gizmo drag means.
    readonly property string gizmoUnit: {
        const tool = EditorState.preview.gizmoTool
        return tool === "move" ? "px" : tool === "rotate" ? "°" : "×"
    }
    // The phone's axis chips: the axis the gizmo's next drag is constrained to, empty for free.
    property string touchAxisLock: ""

    // The clip box whose gesture is running, for the HUD's typed value.
    property var activeBox: null
    // The phone's stand-in for Shift/Ctrl+click: taps and the marquee add to the selection.
    property bool addToSelection: false

    // The selected clip's gizmo, when it is a 3D layer, and its pose while a handle is dragged.
    readonly property var gizmoBox: {
        if (!mode3d)
            return null
        if (group3dBoxes.length >= 2)
            return groupPivot
        if (cameraBox && cameraBox.track === EditorState.selectedTrack
                && cameraBox.clip === EditorState.selectedClip)
            return cameraBox
        for (const b of overlayClips) {
            if (b.layer3d
                    && b.track === EditorState.selectedTrack
                    && b.clip === EditorState.selectedClip)
                return b
        }
        return null
    }
    readonly property var gizmoLive: gizmo.livePose

    // Several 3D layers selected: one gizmo at their centroid moves, turns and scales them together.
    // Global axes only, since a group has no frame of its own to be "local" to. Cameras and lights
    // are not members.
    readonly property var group3dBoxes: {
        void EditorState.selectionRevision
        if (!mode3d)
            return []
        const out = []
        for (const b of overlayClips) {
            if (b.layer3d && b.kind !== "transform" && b.kind !== "camera"
                    && EditorState.selectionContains(b.track, b.clip))
                out.push(b)
        }
        return out
    }
    readonly property var groupPivot: group3dBoxes.length >= 2
        ? EditorState.preview.groupGizmoPivot(group3dBoxes, viewport) : null
    readonly property bool groupDragging: !!gizmoLive && !!gizmoBox && gizmoBox.kind === "group"

    // A transform layer being moved: the clips under it are drawn from a model that is not
    // rebuilt mid-drag, so their outlines would hang in the old place and are hidden meanwhile.
    property int movingLayerTrack: -1
    readonly property int hiddenChildrenOf: movingLayerTrack >= 0 ? movingLayerTrack
        : (gizmoLive && gizmoBox && gizmoBox.kind === "transform" ? gizmoBox.track : -1)

    // Several clips selected in the flat view: one frame moves, scales and turns them together.
    // The transform layers' own boxes are not members; their children are picked individually.
    readonly property var groupBoxes: {
        void EditorState.selectionRevision
        if (mode3d)
            return []
        const out = []
        for (const b of overlayClips) {
            if (b.kind !== "transform" && EditorState.selectionContains(b.track, b.clip))
                out.push(b)
        }
        return out
    }
    readonly property bool groupMode: groupBoxes.length >= 2

    // The selected clip's box, and the transform clip over it (innermost) when there is one.
    readonly property var selectedBox: {
        for (const b of overlayClips) {
            if (b.track === EditorState.selectedTrack && b.clip === EditorState.selectedClip)
                return b
        }
        return null
    }
    readonly property var parentFrameBox: {
        const sel = selectedBox
        if (!sel || sel.kind === "transform" || !sel.parents || sel.parents.length === 0)
            return null
        const p = sel.parents[0]
        for (const b of overlayClips) {
            if (b.kind === "transform" && b.track === p.track && b.clip === p.clip)
                return b
        }
        return null
    }
    // Whether Lock ratio starts on: footage holds its ratio, text, shapes and groups do not.
    readonly property bool selectionLocksRatio: !groupMode && selectedBox !== null
                                                && freeKinds.indexOf(selectedBox.kind) < 0

    // Marquee on empty canvas, in stage px.
    property bool marqueeActive: false
    property rect marqueeRect: Qt.rect(0, 0, 0, 0)

    property real lastGuideX: -1
    property real lastGuideY: -1

    // True when two overlay models describe the same boxes. Text/name updates still reach delegates
    // through the live style, so rebuilding for every keystroke is unnecessary, and recreating a
    // selected handle would steal focus from the properties panel.
    function clipsOverlayEqual(a, b) {
        if (!a || !b || a.length !== b.length)
            return false
        for (let i = 0; i < a.length; ++i) {
            const x = a[i]
            const y = b[i]
            if (x.track !== y.track || x.clip !== y.clip || x.kind !== y.kind
                    || x.x !== y.x || x.y !== y.y
                    || x.width !== y.width || x.height !== y.height
                    || x.rotation !== y.rotation
                    || x.layer3d !== y.layer3d
                    || x.cameraActive !== y.cameraActive
                    || x.rotationX !== y.rotationX || x.rotationY !== y.rotationY
                    || x.z !== y.z || x.perspective !== y.perspective
                    || x.anchorX !== y.anchorX || x.anchorY !== y.anchorY || x.anchorZ !== y.anchorZ
                    || x.canvasWidth !== y.canvasWidth
                    || x.canvasHeight !== y.canvasHeight
                    || x.parentSig !== y.parentSig)
                return false
        }
        return true
    }

    function sameOverlayClips(a, b) {
        if (!a || !b || a.length !== b.length)
            return false
        for (let i = 0; i < a.length; ++i) {
            if (a[i].track !== b[i].track || a[i].clip !== b[i].clip || a[i].kind !== b[i].kind)
                return false
        }
        return true
    }

    function refreshOverlay() {
        // Rebuilding the model mid-drag would destroy the delegate that owns the grab. A burst of
        // nudges is no drag: each key's result has to show for the next one to build on.
        if (EditorState.drag.interacting && EditorState.drag.kind !== "nudge")
            return
        // Playhead ticks ~60 Hz; rebuilding grips every tick during playback is wasted work and
        // stalls the UI on long timelines.
        if (EditorState.playing)
            return
        const camera = EditorState.preview.cameraBox(viewport)
        cameraBox = camera && camera.kind === "camera" ? camera : null
        const next = EditorState.preview.clipsAtPlayhead(viewport)
        if (clipsOverlayEqual(overlayClips, next))
            return
        const sameClips = sameOverlayClips(overlayClips, next)
        overlayClips = next
        // Only geometry moved (a slider or another handle dragging this clip): the delegates follow
        // through `box`, and recreating them would drop a selected handle's focus.
        if (sameClips)
            return
        // Set model imperatively. Binding `model: overlayClips` re-enters when tracksChanged fires
        // during delegate setup (binding loop on model).
        clipRepeater.model = next
    }

    function pointInQuad(q, x, y) {
        let sign = 0
        for (let i = 0; i < 4; ++i) {
            const a = q[i]
            const b = q[(i + 1) % 4]
            const cross = (b.x - a.x) * (y - a.y) - (b.y - a.y) * (x - a.x)
            if (cross !== 0) {
                if (sign !== 0 && (cross > 0) !== (sign > 0))
                    return false
                sign = cross
            }
        }
        return true
    }

    // True when a press at this canvas point lands on a clip box or the group frame, which own it.
    function pressHitsBox(cx, cy) {
        if (groupMode) {
            for (const b of groupBoxes) {
                if (b.quad && b.quad.length === 4 && pointInQuad(b.quad, cx, cy))
                    return true
            }
        }
        for (const b of overlayClips) {
            if (b.kind === "transform" && !(b.track === EditorState.selectedTrack && b.clip === EditorState.selectedClip))
                continue
            if (b.quad && b.quad.length === 4 && pointInQuad(b.quad, cx, cy))
                return true
        }
        return false
    }

    function quadHitsRect(q, r) {
        let l = Infinity, t = Infinity, rr = -Infinity, bb = -Infinity
        for (const p of q) {
            l = Math.min(l, p.x); rr = Math.max(rr, p.x)
            t = Math.min(t, p.y); bb = Math.max(bb, p.y)
        }
        if (rr < r.x || l > r.x + r.width || bb < r.y || t > r.y + r.height)
            return false
        // Bounding boxes meet: a corner of either shape inside the other, or crossing edges.
        const rc = [{x: r.x, y: r.y}, {x: r.x + r.width, y: r.y},
                    {x: r.x + r.width, y: r.y + r.height}, {x: r.x, y: r.y + r.height}]
        for (const p of q) {
            if (p.x >= r.x && p.x <= r.x + r.width && p.y >= r.y && p.y <= r.y + r.height)
                return true
        }
        for (const p of rc) {
            if (pointInQuad(q, p.x, p.y))
                return true
        }
        const cross = (a, b, c) => (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x)
        for (let i = 0; i < 4; ++i) {
            const a = q[i], b = q[(i + 1) % 4]
            for (let j = 0; j < 4; ++j) {
                const c = rc[j], d = rc[(j + 1) % 4]
                if (cross(a, b, c) * cross(a, b, d) < 0 && cross(c, d, a) * cross(c, d, b) < 0)
                    return true
            }
        }
        return false
    }

    function finishMarquee(additive) {
        const pairs = []
        const r = Qt.rect(marqueeRect.x, marqueeRect.y, marqueeRect.width, marqueeRect.height)
        for (const b of overlayClips) {
            if (b.kind === "transform" || !b.quad || b.quad.length !== 4)
                continue
            const sx = stage.width / Math.max(1, b.canvasWidth)
            const sy = stage.height / Math.max(1, b.canvasHeight)
            const cr = Qt.rect(r.x / sx, r.y / sy, r.width / sx, r.height / sy)
            if (quadHitsRect(b.quad, cr))
                pairs.push({ track: b.track, clip: b.clip })
        }
        if (additive) {
            for (const p of EditorState.selection) {
                if (!pairs.some(q => q.track === p.track && q.clip === p.clip))
                    pairs.unshift(p)
            }
        }
        if (pairs.length > 0)
            EditorState.setSelection(pairs)
        else if (!additive)
            EditorState.setSelection([])
    }

    // Shift or Ctrl+click, or a tap with Add on: the clip joins the selection, or leaves it when it
    // is already in.
    function toggleSelected(track, clip) {
        if (!EditorState.selectionContains(track, clip)) {
            EditorState.addToSelection(track, clip)
            return
        }
        const rest = EditorState.selection.filter(p => !(p.track === track && p.clip === clip))
        EditorState.setSelection(rest)
    }

    // The edges and centres of the other clips, in canvas px, which a moved or resized clip snaps to
    // beside the canvas and the guides. A transform layer's own children move with it, so they are
    // not targets.
    function otherTargets(self) {
        const xs = []
        const ys = []
        for (const b of overlayClips) {
            if (b.kind === "transform" || (b.track === self.track && b.clip === self.clip))
                continue
            if ((b.parents || []).some(p => p.track === self.track && p.clip === self.clip))
                continue
            const q = b.quad
            if (!q || q.length !== 4)
                continue
            let l = Infinity, t = Infinity, r = -Infinity, bt = -Infinity
            for (const p of q) {
                l = Math.min(l, p.x); r = Math.max(r, p.x)
                t = Math.min(t, p.y); bt = Math.max(bt, p.y)
            }
            xs.push(l, (l + r) / 2, r)
            ys.push(t, (t + bt) / 2, bt)
        }
        return { "x": xs, "y": ys }
    }

    // Snap lines are drawn by the base, in overlay px; the engine reports them in canvas px.
    function publishGuides(gx, gy, sx, sy) {
        snapLinesX = gx >= 0 ? [stage.x + gx * sx] : []
        snapLinesY = gy >= 0 ? [stage.y + gy * sy] : []
        if (touchDrag && ((gx >= 0 && gx !== lastGuideX) || (gy >= 0 && gy !== lastGuideY)))
            Haptics.detent()
        lastGuideX = gx
        lastGuideY = gy
    }

    // Where the pointer is, for the HUD; `p` is in stage px.
    function pointerAt(p) {
        root.pointer = Qt.point(p.x + stage.x, p.y + stage.y)
    }

    function beginGesture(kind, device, text, owner) {
        EditorState.drag.begin(kind, device, text)
        activeBox = owner
        if (owner && owner.isTransform)
            movingLayerTrack = owner.box.track
        forceActiveFocus()
        if (device !== DragSession.Mouse)
            Haptics.pickUp()
    }

    function endGesture() {
        EditorState.drag.commit()
    }

    // The boxes' own live state follows the session: whichever way it closed, nothing may keep
    // writing to the project from a pointer that is still down.
    function resetGestures() {
        for (let i = 0; i < clipRepeater.count; ++i) {
            const item = clipRepeater.itemAt(i)
            if (item)
                item.clearGesture()
        }
        groupBox.clearGesture()
        activeBox = null
        movingLayerTrack = -1
        snapLinesX = []
        snapLinesY = []
        lastGuideX = -1
        lastGuideY = -1
        Qt.callLater(refreshOverlay)
    }

    function freshBoxes() {
        return EditorState.preview.clipsAtPlayhead(viewport)
    }

    onNudged: (dx, dy) => {
        if (mode3d)
            return
        const boxes = freshBoxes()
        if (groupMode) {
            const members = boxes.filter(b => b.kind !== "transform"
                                              && EditorState.selectionContains(b.track, b.clip))
            EditorState.preview.applyGroupTransform(members, 0, 0, 1, 1, 0, dx, dy)
            return
        }
        for (const b of boxes) {
            if (b.track === EditorState.selectedTrack && b.clip === EditorState.selectedClip) {
                EditorState.previewSetClipPosition(b.track, b.clip, b.positionX + dx, b.positionY + dy)
                return
            }
        }
    }

    onHudApplied: (text, value) => {
        if (gizmo.dragging !== "") {
            gizmo.solve(value)
            endGesture()
            return
        }
        if (!activeBox)
            return
        activeBox.applyTyped(value)
        endGesture()
    }

    // X, Y and Z constrain a gizmo drag (Shift: to the plane without it), and Ctrl snaps it.
    onUnhandledKey: (event) => {
        if (gizmo.dragging === "")
            return
        const plain = !(event.modifiers & (Qt.ControlModifier | Qt.AltModifier | Qt.MetaModifier))
        const axis = event.key === Qt.Key_X ? "x" : event.key === Qt.Key_Y ? "y" : event.key === Qt.Key_Z ? "z" : ""
        if (axis !== "" && plain && !event.isAutoRepeat) {
            event.accepted = gizmo.lockKey(axis, (event.modifiers & Qt.ShiftModifier) !== 0)
        } else if (event.key === Qt.Key_Control) {
            gizmo.setModifiers(event.modifiers | Qt.ControlModifier)
        }
    }

    Keys.onReleased: (event) => {
        if (gizmo.dragging !== "" && event.key === Qt.Key_Control)
            gizmo.setModifiers(event.modifiers & ~Qt.ControlModifier)
    }

    Keys.onShortcutOverride: (event) => {
        if (gizmo.dragging === "")
            return
        if (event.key === Qt.Key_X || event.key === Qt.Key_Y || event.key === Qt.Key_Z
                || (event.text.length === 1 && "0123456789-.".indexOf(event.text) >= 0))
            event.accepted = true
    }

    onEscaped: {
        if (marqueeActive) {
            marqueeActive = false
            marqueeDrag.started = false
        }
    }

    Component.onCompleted: refreshOverlay()

    Connections {
        target: EditorState.drag
        function onCommitted() {
            gizmo.abort()
            root.resetGestures()
        }
        function onCancelled() {
            gizmo.abort()
            root.resetGestures()
        }
    }

    Connections {
        target: EditorState.preview
        function onViewChanged() { root.refreshOverlay() }
    }

    Component {
        id: touchBarComponent

        TouchContextBar {
            id: contextBar
            width: Math.min(implicitWidth, root.width - Theme.spacingLg * 2)
            visible: root.selectedBox !== null || root.groupMode
            chips: ["lockRatio", "fromCenter", "snap"]

            Binding {
                target: contextBar
                property: "lockRatio"
                value: root.selectionLocksRatio
                restoreMode: Binding.RestoreNone
            }

            ThemedChip {
                chipHeight: Theme.androidMinTouchTarget
                text: qsTr("Add")
                selected: root.addToSelection
                onClicked: root.addToSelection = !root.addToSelection
            }
        }
    }

    Component {
        id: touchBar3dComponent

        Column {
            id: bar3d
            spacing: Theme.spacingSm
            visible: root.gizmoBox !== null

            TouchContextBar {
                width: Math.min(implicitWidth, root.width - Theme.spacingLg * 2)
                anchors.horizontalCenter: parent.horizontalCenter
                chips: []

                Repeater {
                    model: [
                        { id: "move", label: qsTr("Move") },
                        { id: "rotate", label: qsTr("Rotate") },
                        { id: "scale", label: qsTr("Scale") }
                    ]

                    ThemedChip {
                        required property var modelData
                        chipHeight: Theme.androidMinTouchTarget
                        text: modelData.label
                        selected: EditorState.preview.gizmoTool === modelData.id
                        onClicked: EditorState.preview.gizmoTool = modelData.id
                    }
                }

                ThemedChip {
                    chipHeight: Theme.androidMinTouchTarget
                    text: qsTr("Local")
                    selected: EditorState.preview.gizmoOrientation === "local"
                    onClicked: EditorState.preview.gizmoOrientation =
                               EditorState.preview.gizmoOrientation === "local" ? "world" : "local"
                }
            }

            TouchContextBar {
                id: lockBar
                width: Math.min(implicitWidth, root.width - Theme.spacingLg * 2)
                anchors.horizontalCenter: parent.horizontalCenter
                chips: ["snap"]
                snap: false

                Binding {
                    target: gizmo
                    property: "snapOn"
                    value: lockBar.snap
                }

                Repeater {
                    model: [
                        { id: "x", label: qsTr("X") },
                        { id: "y", label: qsTr("Y") },
                        { id: "z", label: qsTr("Z") },
                        { id: "", label: qsTr("Free") }
                    ]

                    ThemedChip {
                        required property var modelData
                        chipHeight: Theme.androidMinTouchTarget
                        text: modelData.label
                        selected: root.touchAxisLock === modelData.id
                        onClicked: root.touchAxisLock = modelData.id
                    }
                }
            }
        }
    }

    // The canvas rect, inside a viewport-sized root: grips and handles of a clip that runs past an
    // edge stay inside the root, and so inside the area that takes the pointer.
    Item {
        id: stage
        x: root.canvas ? root.canvas.x : 0
        y: root.canvas ? root.canvas.y : 0
        width: root.canvas ? root.canvas.width : root.width
        height: root.canvas ? root.canvas.height : root.height

        // Presses that no box takes: a drag across the canvas box-selects. Below every box handler.
        // On a phone there is no one-finger pan (two fingers navigate), so a one-finger drag on empty
        // canvas is free to be the marquee; it stands down when a second finger lands.
        DragHandler {
            id: marqueeDrag
            target: null
            maximumPointCount: 1
            enabled: !root.mode3d && !root.interacting
            property point press: Qt.point(0, 0)
            property bool started: false

            onActiveChanged: {
                if (active) {
                    const p = stage.mapFromItem(null, marqueeDrag.centroid.scenePressPosition.x,
                                                marqueeDrag.centroid.scenePressPosition.y)
                    const c = root.overlayClips.length > 0 ? root.overlayClips[0] : null
                    const sx = c ? stage.width / Math.max(1, c.canvasWidth) : 1
                    const sy = c ? stage.height / Math.max(1, c.canvasHeight) : 1
                    marqueeDrag.press = p
                    marqueeDrag.started = !root.pressHitsBox(p.x / sx, p.y / sy)
                    root.marqueeActive = marqueeDrag.started
                    root.marqueeRect = Qt.rect(p.x, p.y, 0, 0)
                } else if (marqueeDrag.started) {
                    marqueeDrag.started = false
                    root.marqueeActive = false
                    const keys = marqueeDrag.centroid.modifiers & (Qt.ShiftModifier | Qt.ControlModifier)
                    root.finishMarquee(keys !== 0 || root.addToSelection)
                }
            }
            onCentroidChanged: {
                if (!active || !marqueeDrag.started)
                    return
                const p = stage.mapFromItem(null, marqueeDrag.centroid.scenePosition.x,
                                            marqueeDrag.centroid.scenePosition.y)
                root.marqueeRect = Qt.rect(Math.min(p.x, marqueeDrag.press.x), Math.min(p.y, marqueeDrag.press.y),
                                           Math.abs(p.x - marqueeDrag.press.x), Math.abs(p.y - marqueeDrag.press.y))
            }
        }

        Repeater {
            id: clipRepeater

            delegate: TransformClipBox {
                manip: root
            }
        }

        TransformGroupBox {
            id: groupBox
            visible: root.groupMode
            enabled: root.groupMode
            z: 890
            manip: root
            boxes: root.groupBoxes
        }

        Rectangle {
            visible: root.marqueeActive && (root.marqueeRect.width > 2 || root.marqueeRect.height > 2)
            x: root.marqueeRect.x
            y: root.marqueeRect.y
            width: root.marqueeRect.width
            height: root.marqueeRect.height
            z: 890
            color: Qt.rgba(Theme.c.timelineMarqueeRect.background.r, Theme.c.timelineMarqueeRect.background.g, Theme.c.timelineMarqueeRect.background.b, 0.12)
            border.width: Theme.c.timelineMarqueeRect.borderWidth
            border.color: Theme.c.timelineMarqueeRect.border
        }

        TransformParentFrame {
            manip: root
        }

        TransformGizmo {
            id: gizmo
            viewport: root.viewport
            touch: root.touch
            anchors.fill: parent
            z: 950
            sx: stage.width / Math.max(1, root.gizmoBox ? root.gizmoBox.canvasWidth : 1)
            // Hidden while the box itself is dragged, which it would not follow.
            box: root.interacting && dragging === "" ? null : root.gizmoBox
            groupBoxes: root.group3dBoxes
            lockPreset: root.touchAxisLock
            onPointerMoved: (position) => root.pointerAt(position)
            onDragStarted: (byTouch) => {
                const tool = EditorState.preview.gizmoTool
                root.beginGesture("gizmo", byTouch ? DragSession.Touch : DragSession.Mouse,
                                  tool === "move" ? qsTr("Move clip in 3D") : tool === "rotate" ? qsTr("Rotate clip in 3D")
                                                                                              : qsTr("Scale clip"), null)
                // A group keeps its selection; picking one member would collapse it.
                if (box.kind !== "group")
                    EditorState.selectClip(box.track, box.clip)
            }
            onDragFinished: root.endGesture()
        }
    }
}
