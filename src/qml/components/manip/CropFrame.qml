pragma ComponentBehavior: Bound
import QtQuick
import Drift

// The one crop rectangle: the desktop and phone canvas crop, the clip's source-frame crop, the bin
// preview crop and Source Lab all edit through it. It draws and moves the rect; what the numbers
// mean is the caller's: it passes the rect in whatever space it keeps (project px, source px, or
// 0..1 of a picture) and takes the result back through `edited`.
//
// The crop is `crop`, in a space `spaceSize` units across that is drawn into `stageRect` (this
// item's px) and turned by `stageRotation`. `contentSize` is how many real px that space is, which
// the readout and the ratios speak in. A drag proposes through `edited(rect)` as it goes and, when
// it ends, `committed(rect)`; Esc reverts through `edited` with the start rect and commits nothing.
//
// Gestures (Figma / Adobe convention):
//   resize  corner or edge grips. A preset ratio holds; Shift frees it, or with no preset Shift
//           holds the ratio the crop had. Alt scales about the centre, Ctrl/Cmd suspends snapping.
//   move    drag inside the frame; Shift locks to an axis. Arrows nudge 1 px (Shift 10), a burst
//           of nudges is one commit.
//   keys    Enter confirms (`confirmed`), Esc cancels the drag in flight or else emits `escaped`.
// On a finger the context bar's chips stand in for the keys: Lock ratio, From centre and Snap, and
// the ratio presets sit above it. Which gesture a press is decided by the device that made it.
ManipOverlay {
    id: root

    // Where the space lies in this item, and its turn about `stagePivot` (stage px).
    property rect stageRect: Qt.rect(0, 0, width, height)
    property real stageRotation: 0
    property point stagePivot: Qt.point(stageRect.width / 2, stageRect.height / 2)
    property size spaceSize: Qt.size(1, 1)
    property size contentSize: spaceSize
    property rect crop: Qt.rect(0, 0, 1, 1)

    // w / h in content px; 0 is free.
    property real aspect: 0
    // The smallest side, in space units.
    property real minSize: 0.08
    // True lets the rect leave the space (the canvas crop grows the output past its edges).
    property bool overflow: false
    property bool editable: true
    // Dim beyond the space too, out to this item's edges.
    property bool scrimFillsViewport: false
    // The ratio chips; off leaves only the thirds toggle.
    property bool showPresets: false
    // False leaves the ratio chips and the thirds toggle to the host (the phone crop page shows
    // them in its own controls), so the touch bar is only the modifier chips.
    property bool touchPresets: true
    property bool touch: Theme.touchInput
    // A PreviewViewport (zoomAt/panX/panY) to zoom and pan with Ctrl+wheel, middle-drag and pinch.
    property var viewport: null
    property string undoText: qsTr("Crop")

    readonly property alias stage: stage
    readonly property real originalAspect: contentSize.height > 0 ? contentSize.width / contentSize.height : 0
    // Where the frame is, in stage px.
    readonly property rect frameRect: Qt.rect(crop.x * stage.sx, crop.y * stage.sy,
                                              crop.width * stage.sx, crop.height * stage.sy)
    // Content px per space unit.
    readonly property real unitPxX: spaceSize.width > 0 ? contentSize.width / spaceSize.width : 1
    readonly property real unitPxY: spaceSize.height > 0 ? contentSize.height / spaceSize.height : 1
    readonly property bool gesturing: gesture !== null
    // Lower edge of the desktop preset bar, for a host that stacks something under it.
    readonly property real barBottom: presetPill.visible ? presetPill.y + presetPill.height : 0

    property bool thirds: LayoutMemory.layoutNumber("crop/thirds", 1) !== 0

    signal edited(rect crop)
    signal committed(rect crop)
    signal viewZoomed()
    signal viewPanned()

    property var gesture: null
    property rect startCrop: Qt.rect(0, 0, 0, 0)
    property bool pending: false
    property bool ready: false
    property string lastHit: ""

    refreshPolicy: "none"
    nudgeEnabled: editable
    nudgeText: qsTr("Nudge crop")
    chipModifiers: touchBarItem ? touchBarItem.modifiers : null
    touchBar: touch ? touchBarComponent : null

    Component.onCompleted: ready = true

    function toggleThirds() {
        thirds = !thirds
        LayoutMemory.setLayoutNumber("crop/thirds", thirds ? 1 : 0)
    }

    // The ratio as w / h in space units, which is what the arithmetic below works in.
    function unitRatio(contentRatio) {
        return contentRatio > 0 ? contentRatio * unitPxY / unitPxX : 0
    }

    function sameRect(a, b) {
        return Math.abs(a.x - b.x) < 1e-6 && Math.abs(a.y - b.y) < 1e-6
               && Math.abs(a.width - b.width) < 1e-6 && Math.abs(a.height - b.height) < 1e-6
    }

    function clampInto(r) {
        if (overflow)
            return r
        const w = Math.min(r.width, spaceSize.width)
        const h = Math.min(r.height, spaceSize.height)
        return Qt.rect(Math.max(0, Math.min(spaceSize.width - w, r.x)),
                       Math.max(0, Math.min(spaceSize.height - h, r.y)), w, h)
    }

    // The largest rect of the ratio inside the current one, about its centre.
    function refit(ratio) {
        const k = unitRatio(ratio)
        if (k <= 0 || crop.width <= 0 || crop.height <= 0)
            return
        if (Math.abs(crop.width / crop.height - k) < 0.002 * k)
            return
        const cx = crop.x + crop.width / 2
        const cy = crop.y + crop.height / 2
        let w = Math.min(crop.width, crop.height * k)
        let h = w / k
        if (!overflow) {
            if (h > spaceSize.height) {
                h = spaceSize.height
                w = h * k
            }
            if (w > spaceSize.width) {
                w = spaceSize.width
                h = w / k
            }
        }
        const next = clampInto(Qt.rect(cx - w / 2, cy - h / 2, w, h))
        if (!sameRect(next, crop)) {
            edited(next)
            committed(next)
        }
    }

    onAspectChanged: {
        if (ready && aspect > 0 && !gesturing)
            refit(aspect)
    }

    function hudFor(r) {
        return Math.round(r.width * unitPxX) + "×" + Math.round(r.height * unitPxY)
    }

    // Pulls one coordinate onto the space's edges and centre.
    function snapCoord(value, length, scale, noSnap, hits) {
        const hit = Manip.snapValue(value, [0, length / 2, length], scale, noSnap)
        if (hit.snapped)
            hits.push(hit.target)
        return hit.value
    }

    // The smallest pull any of `candidates` (an edge or the centre) gets, as a delta.
    function snapDelta(candidates, length, scale, noSnap, hits) {
        let best = null
        for (const c of candidates) {
            const hit = Manip.snapValue(c, [0, length / 2, length], scale, noSnap)
            if (hit.snapped && (best === null || Math.abs(hit.value - c) < Math.abs(best.delta)))
                best = { delta: hit.value - c, target: hit.target }
        }
        if (best === null)
            return 0
        hits.push(best.target)
        return best.delta
    }

    // One axis of a resize: [lo, hi] of the edge pair after dragging the side `d` (-1 low, +1 high,
    // 0 neither) by `delta`, snapped, kept inside the space and no smaller than minSize.
    function axisSolve(start, size, d, delta, fromCenter, length, scale, noSnap, hits) {
        if (d === 0)
            return [start, start + size]
        if (!fromCenter) {
            const anchor = d < 0 ? start + size : start
            let moved = (d < 0 ? start : start + size) + delta
            moved = snapCoord(moved, length, scale, noSnap, hits)
            if (!overflow)
                moved = Math.max(0, Math.min(length, moved))
            moved = d < 0 ? Math.min(moved, anchor - minSize) : Math.max(moved, anchor + minSize)
            return d < 0 ? [moved, anchor] : [anchor, moved]
        }
        const c = start + size / 2
        let half = size / 2 + d * delta
        half = Math.abs(snapCoord(c + half, length, scale, noSnap, hits) - c)
        const room = overflow ? Infinity : Math.min(c, length - c)
        half = Math.max(minSize / 2, Math.min(room, half))
        return [c - half, c + half]
    }

    function resizeRect(s, dx, dy, ddx, ddy, mods, ratio) {
        const hitsX = []
        const hitsY = []
        const W = spaceSize.width
        const H = spaceSize.height
        const ax = axisSolve(s.x, s.width, dx, ddx, mods.fromCenter, W, stage.sx, mods.noSnap, hitsX)
        const ay = axisSolve(s.y, s.height, dy, ddy, mods.fromCenter, H, stage.sy, mods.noSnap, hitsY)
        publishLines(hitsX, hitsY)
        if (ratio <= 0)
            return Qt.rect(ax[0], ay[0], ax[1] - ax[0], ay[1] - ay[0])

        let w = ax[1] - ax[0]
        let h = ay[1] - ay[0]
        if (dx !== 0 && dy !== 0) {
            if (w / ratio >= h)
                h = w / ratio
            else
                w = h * ratio
        } else if (dx !== 0) {
            h = w / ratio
        } else {
            w = h * ratio
        }
        const cx = s.x + s.width / 2
        const cy = s.y + s.height / 2
        const centreX = mods.fromCenter || dx === 0
        const centreY = mods.fromCenter || dy === 0
        let roomW = Infinity
        let roomH = Infinity
        if (!overflow) {
            roomW = centreX ? 2 * Math.min(cx, W - cx) : (dx < 0 ? s.x + s.width : W - s.x)
            roomH = centreY ? 2 * Math.min(cy, H - cy) : (dy < 0 ? s.y + s.height : H - s.y)
        }
        const shrink = Math.min(1, roomW / w, roomH / h)
        w *= shrink
        h *= shrink
        const grow = Math.max(1, minSize / w, minSize / h)
        w *= grow
        h *= grow
        return Qt.rect(centreX ? cx - w / 2 : (dx < 0 ? s.x + s.width - w : s.x),
                       centreY ? cy - h / 2 : (dy < 0 ? s.y + s.height - h : s.y), w, h)
    }

    function moveRect(s, ddx, ddy, mods, axisLock) {
        const hitsX = []
        const hitsY = []
        if (axisLock) {
            if (Math.abs(ddx) * stage.sx >= Math.abs(ddy) * stage.sy)
                ddy = 0
            else
                ddx = 0
        }
        let x = s.x + ddx
        let y = s.y + ddy
        x += snapDelta([x, x + s.width / 2, x + s.width], spaceSize.width, stage.sx, mods.noSnap, hitsX)
        y += snapDelta([y, y + s.height / 2, y + s.height], spaceSize.height, stage.sy, mods.noSnap, hitsY)
        publishLines(hitsX, hitsY)
        return clampInto(Qt.rect(x, y, s.width, s.height))
    }

    // Snap lines are drawn axis-aligned by the base, so a turned stage shows none.
    function publishLines(hitsX, hitsY) {
        if (stageRotation !== 0 || (hitsX.length === 0 && hitsY.length === 0)) {
            snapLinesX = []
            snapLinesY = []
            lastHit = ""
            return
        }
        snapLinesX = hitsX.map(v => stage.mapToItem(root, v * stage.sx, 0).x)
        snapLinesY = hitsY.map(v => stage.mapToItem(root, 0, v * stage.sy).y)
        const key = hitsX.join(",") + "|" + hitsY.join(",")
        if (touchDrag && key !== lastHit)
            Haptics.detent()
        lastHit = key
    }

    // The ratio a resize holds, in space units, or 0 for free. A preset holds unless Shift frees
    // it; with none, Shift holds the shape the crop had. A finger has no Shift, so its chip says it.
    function gestureRatio(mods) {
        const preset = unitRatio(aspect)
        const startRatio = gesture.start.height > 0 ? gesture.start.width / gesture.start.height : 0
        const lock = mods.constrain
        if (chipModifiers !== null && gesture.device !== DragSession.Mouse)
            return lock ? (preset > 0 ? preset : startRatio) : 0
        return preset > 0 ? (lock ? 0 : preset) : (lock ? startRatio : 0)
    }

    function beginGesture(kind, dx, dy, point, device) {
        forceActiveFocus()
        gesture = { kind: kind, dx: dx, dy: dy, start: crop, press: point, device: device }
        startCrop = crop
        pending = true
        EditorState.drag.begin("crop", device, undoText)
        if (device !== DragSession.Mouse)
            Haptics.pickUp()
    }

    function beginResize(dx, dy, point, device) {
        beginGesture("resize", dx, dy, point, device)
    }

    function beginMove(point, device) {
        beginGesture("move", 0, 0, point, device)
    }

    function updateGesture(point, qtModifiers) {
        if (!gesture)
            return
        const mods = modifiersFor(qtModifiers)
        const ddx = (point.x - gesture.press.x) / stage.sx
        const ddy = (point.y - gesture.press.y) / stage.sy
        let next
        if (gesture.kind === "move")
            next = moveRect(gesture.start, ddx, ddy, mods, Manip.fromQtFlags(qtModifiers).constrain)
        else
            next = resizeRect(gesture.start, gesture.dx, gesture.dy, ddx, ddy, mods, gestureRatio(mods))
        root.pointer = stage.mapToItem(root, point.x, point.y)
        EditorState.drag.update(hudFor(next))
        edited(next)
    }

    function moveResize(point, modifiers) {
        updateGesture(point, modifiers)
    }

    function endGesture() {
        if (gesture)
            EditorState.drag.commit()
    }

    function clearGesture() {
        gesture = null
        snapLinesX = []
        snapLinesY = []
        lastHit = ""
    }

    onNudged: (dx, dy) => {
        if (!editable)
            return
        if (!pending) {
            startCrop = crop
            pending = true
        }
        const next = clampInto(Qt.rect(crop.x + dx / unitPxX, crop.y + dy / unitPxY, crop.width, crop.height))
        if (!sameRect(next, crop))
            edited(next)
    }

    Connections {
        target: EditorState.drag

        function onCommitted() {
            if (!root.pending)
                return
            root.pending = false
            root.clearGesture()
            root.committed(root.crop)
        }

        function onCancelled() {
            if (!root.pending)
                return
            root.pending = false
            root.clearGesture()
            root.edited(root.startCrop)
        }
    }

    ViewNavigation {
        anchors.fill: parent
        viewport: root.viewport
        enabled: root.viewport !== null
        visible: root.viewport !== null
        pinchEnabled: false
        onZoomed: root.viewZoomed()
        onPanned: root.viewPanned()
    }

    Item {
        id: stage

        readonly property real sx: width / Math.max(1e-9, root.spaceSize.width)
        readonly property real sy: height / Math.max(1e-9, root.spaceSize.height)

        x: root.stageRect.x
        y: root.stageRect.y
        width: root.stageRect.width
        height: root.stageRect.height
        transform: Rotation {
            origin.x: root.stagePivot.x
            origin.y: root.stagePivot.y
            angle: root.stageRotation
        }

        CropScrim {
            frame: root.frameRect
            extent: root.scrimFillsViewport ? Qt.rect(-stage.x, -stage.y, root.width, root.height)
                                            : Qt.rect(0, 0, stage.width, stage.height)
        }

        Rectangle {
            x: root.frameRect.x
            y: root.frameRect.y
            width: root.frameRect.width
            height: root.frameRect.height
            color: "transparent"
            border.width: Theme.c.cropFrame.borderWidth
            border.color: Theme.c.cropFrame.border

            CropThirds {
                anchors.fill: parent
                visible: root.thirds || root.gesturing
            }
        }

        // Dragging inside the frame moves it; the grips above keep their own presses.
        MouseArea {
            id: body
            x: root.frameRect.x
            y: root.frameRect.y
            width: root.frameRect.width
            height: root.frameRect.height
            enabled: root.editable
            acceptedButtons: Qt.LeftButton
            cursorShape: Qt.SizeAllCursor
            preventStealing: !byTouch

            property bool byTouch: false

            function spot(mouse) {
                return mapToItem(stage, mouse.x, mouse.y)
            }

            onPressed: (mouse) => {
                byTouch = mouse.source !== Qt.MouseEventNotSynthesized
                root.beginMove(spot(mouse), byTouch ? DragSession.Touch : DragSession.Mouse)
            }
            onPositionChanged: (mouse) => {
                if (pressed)
                    root.updateGesture(spot(mouse), mouse.modifiers)
            }
            onReleased: root.endGesture()
            onCanceled: root.endGesture()
            onWheel: (wheel) => { wheel.accepted = false }
        }

        Repeater {
            model: [
                { dx: -1, dy: 0 },
                { dx: 1, dy: 0 },
                { dx: 0, dy: -1 },
                { dx: 0, dy: 1 },
                { dx: -1, dy: -1 },
                { dx: 1, dy: 1 },
                { dx: 1, dy: -1 },
                { dx: -1, dy: 1 }
            ]

            delegate: CropGrip {
                crop: root
            }
        }
    }

    // Above the grips so a pinch that lands on one still zooms; it takes touch only, and a single
    // finger passes through to the grip underneath.
    ViewNavigation {
        anchors.fill: parent
        viewport: root.viewport
        enabled: root.viewport !== null
        visible: root.viewport !== null
        middlePan: false
        wheelZoom: false
        onZoomed: root.viewZoomed()
        onPanned: root.viewPanned()
    }

    Rectangle {
        id: presetPill
        visible: !root.touch && root.editable
        anchors.horizontalCenter: parent.horizontalCenter
        y: Theme.spacingLg
        z: 91
        width: pillPresets.implicitWidth + Theme.spacingSm * 2
        height: pillPresets.implicitHeight + Theme.spacingSm * 2
        radius: Theme.c.manipContextBar.radius
        color: Theme.c.manipContextBar.background
        border.width: Theme.c.manipContextBar.borderWidth
        border.color: Theme.c.manipContextBar.border

        MouseArea {
            anchors.fill: parent
        }

        CropPresets {
            id: pillPresets
            anchors.centerIn: parent
            aspectChips: root.showPresets
            aspect: root.aspect
            originalAspect: root.originalAspect
            thirds: root.thirds
            onPicked: (ratio) => root.aspect = ratio
            onThirdsToggled: root.toggleThirds()
        }
    }

    Component {
        id: touchBarComponent

        Column {
            id: stack
            spacing: Theme.spacingSm
            readonly property var modifiers: bar.modifiers

            Rectangle {
                id: presetStrip
                anchors.horizontalCenter: parent.horizontalCenter
                width: Math.min(root.width - Theme.spacingLg * 2, strip.implicitWidth + Theme.spacingMd * 2)
                height: strip.implicitHeight + Theme.spacingSm * 2
                radius: Theme.c.manipContextBar.radius
                color: Theme.c.manipContextBar.background
                border.width: Theme.c.manipContextBar.borderWidth
                border.color: Theme.c.manipContextBar.border
                visible: root.showPresets && root.touchPresets

                MouseArea {
                    anchors.fill: parent
                }

                Flickable {
                    anchors.fill: parent
                    anchors.margins: Theme.spacingSm
                    contentWidth: strip.implicitWidth
                    contentHeight: strip.implicitHeight
                    flickableDirection: Flickable.HorizontalFlick
                    boundsBehavior: Flickable.StopAtBounds
                    clip: true

                    CropPresets {
                        id: strip
                        variant: "outline"
                        aspect: root.aspect
                        originalAspect: root.originalAspect
                        thirds: root.thirds
                        onPicked: (ratio) => root.aspect = ratio
                        onThirdsToggled: root.toggleThirds()
                    }
                }
            }

            TouchContextBar {
                id: bar
                anchors.horizontalCenter: parent.horizontalCenter
                width: Math.min(implicitWidth, root.width - Theme.spacingLg * 2)
                visible: root.editable
                chips: ["lockRatio", "fromCenter", "snap"]

                Binding {
                    target: bar
                    property: "lockRatio"
                    value: root.aspect > 0
                    restoreMode: Binding.RestoreNone
                }

                ThemedChip {
                    visible: !root.showPresets && root.touchPresets
                    text: qsTr("Thirds")
                    selected: root.thirds
                    onClicked: root.toggleThirds()
                }
            }
        }
    }
}
