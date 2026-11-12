import QtQuick
import Drift

Item {
    id: root

    required property var panel

    readonly property var timeView: panel.timeView
    readonly property bool busy: dragging || creating || marqueeActive

    property string mode: ""
    property bool dragging: false
    property bool creating: false
    property bool marqueeActive: false
    property bool touch: false
    property bool collapseOnRelease: false
    property var pressHit: null
    property real pressX: 0
    property real pressY: 0
    property real pressSec: 0
    property real panStartX: 0
    property var base: []
    property var moving: []
    property real createStart: 0
    property real createEnd: 0
    property real createLo: 0
    property real createHi: 0
    property real marqueeX: 0
    property real marqueeWidth: 0

    readonly property real minDuration: 1 / Math.max(1, EditorState.subtitles.projectFps())
    readonly property real snapThreshold: 8 / timeView.pxPerSecond

    function localSec(x) {
        return timeView.secondsFor(x) - panel.clipStart
    }

    function hitTest(x, y) {
        if (y > panel.cueAreaHeight)
            return null
        const hit = EditorState.subtitles.cueHitAt(localSec(x), timeView.pxPerSecond, touch ? 14 : 6)
        return hit.part ? hit : null
    }

    function snapValue(v, excluded) {
        const ph = EditorState.playheadSeconds - panel.clipStart
        return EditorState.subtitles.snapCueTime(v, snapThreshold, ph, panel.clipDuration, excluded)
    }

    function startDrag() {
        base = panel.copyCues(panel.liveCues)
        const sel = panel.selectedIndices()
        moving = (mode === "move" && sel.indexOf(pressHit.index) >= 0) ? sel : [pressHit.index]
        panel.gestureActive = true
        dragging = true
    }

    function updateDrag(x, mods) {
        const raw = (x - pressX) / timeView.pxPerSecond
        const snapping = !(mods & Qt.ShiftModifier)
        const list = panel.copyCues(base)
        const i0 = pressHit.index
        const n = base.length
        const end = panel.clipDuration > 0 ? panel.clipDuration : Infinity
        if (mode === "move") {
            const isMoving = {}
            for (let k = 0; k < moving.length; ++k)
                isMoving[moving[k]] = true
            let dMin = -Infinity
            let dMax = Infinity
            for (let k = 0; k < moving.length; ++k) {
                const i = moving[k]
                const lo = i === 0 ? 0 : (isMoving[i - 1] ? -Infinity : base[i - 1].end)
                const hi = i === n - 1 ? end : (isMoving[i + 1] ? Infinity : base[i + 1].start)
                dMin = Math.max(dMin, lo - base[i].start)
                dMax = Math.min(dMax, hi - base[i].end)
            }
            let d = raw
            if (snapping) {
                const s = snapValue(base[i0].start + raw, moving)
                const e = snapValue(base[i0].end + raw, moving)
                const ds = s - (base[i0].start + raw)
                const de = e - (base[i0].end + raw)
                if (ds !== 0 || de !== 0)
                    d = raw + (ds !== 0 && (de === 0 || Math.abs(ds) <= Math.abs(de)) ? ds : de)
            }
            d = dMin > dMax ? 0 : Math.max(dMin, Math.min(dMax, d))
            for (let k = 0; k < moving.length; ++k) {
                list[moving[k]].start = base[moving[k]].start + d
                list[moving[k]].end = base[moving[k]].end + d
            }
        } else if (mode === "trimStart") {
            const lo = i0 === 0 ? 0 : base[i0 - 1].end
            const hi = base[i0].end - minDuration
            let v = base[i0].start + raw
            if (snapping)
                v = snapValue(v, [i0])
            if (lo <= hi)
                list[i0].start = Math.max(lo, Math.min(hi, v))
        } else if (mode === "trimEnd") {
            const lo = base[i0].start + minDuration
            const hi = i0 === n - 1 ? end : base[i0 + 1].start
            let v = base[i0].end + raw
            if (snapping)
                v = snapValue(v, [i0])
            if (lo <= hi)
                list[i0].end = Math.max(lo, Math.min(hi, v))
        }
        panel.liveCues = list
        EditorState.previewSetSubtitleCues(panel.track, panel.clipIndex, list)
    }

    function finishDrag() {
        dragging = false
        panel.gestureActive = false
        EditorState.commitPreviewDrag()
    }

    function startCreate() {
        const cues = panel.liveCues
        createLo = 0
        createHi = panel.clipDuration > 0 ? panel.clipDuration : Infinity
        for (let i = 0; i < cues.length; ++i) {
            if (cues[i].end <= pressSec)
                createLo = Math.max(createLo, cues[i].end)
            else if (cues[i].start >= pressSec)
                createHi = Math.min(createHi, cues[i].start)
        }
        createStart = pressSec
        createEnd = pressSec
        creating = true
    }

    function updateCreate(x) {
        const s = localSec(x)
        createStart = Math.max(createLo, Math.min(createHi, Math.min(pressSec, s)))
        createEnd = Math.max(createLo, Math.min(createHi, Math.max(pressSec, s)))
    }

    function updateMarquee(x) {
        const x0 = Math.max(0, Math.min(pressX, x))
        const x1 = Math.min(width, Math.max(pressX, x))
        marqueeX = x0
        marqueeWidth = x1 - x0
        const s0 = localSec(x0)
        const s1 = localSec(x1)
        const hit = []
        const cues = panel.liveCues
        for (let i = 0; i < cues.length; ++i) {
            if (cues[i].end >= s0 && cues[i].start <= s1)
                hit.push(i)
        }
        EditorState.subtitles.setSelectedCues(hit)
    }

    function cancelGesture() {
        if (dragging) {
            EditorState.cancelPreviewDrag()
            panel.gestureActive = false
            panel.liveCues = panel.committedCues
            dragging = false
        }
        creating = false
        marqueeActive = false
        mode = ""
    }

    Connections {
        target: root.panel

        function onTrackChanged() { root.cancelGesture() }
        function onClipIndexChanged() { root.cancelGesture() }
    }

    MouseArea {
        id: area
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton | Qt.MiddleButton
        pressAndHoldInterval: Theme.touchLiftInterval
        cursorShape: {
            if (root.mode === "pan")
                return Qt.ClosedHandCursor
            const part = root.mode.indexOf("trim") === 0 ? root.mode : (hoverHit ? hoverHit.part : "")
            if (part === "trimStart" || part === "trimEnd")
                return Qt.SizeHorCursor
            return part === "move" ? Qt.OpenHandCursor : Qt.ArrowCursor
        }

        property var hoverHit: null

        onPressed: (mouse) => {
            root.panel.forceActiveFocus()
            root.touch = mouse.source !== Qt.MouseEventNotSynthesized
            root.pressX = mouse.x
            root.pressY = mouse.y
            root.pressSec = root.localSec(mouse.x)
            root.panStartX = root.timeView.contentX
            root.collapseOnRelease = false
            root.mode = ""
            if (mouse.button === Qt.MiddleButton) {
                root.mode = "pan"
                return
            }
            root.pressHit = root.hitTest(mouse.x, mouse.y)
            if (root.touch) {
                root.mode = "touch"
                return
            }
            const mods = mouse.modifiers
            const hit = root.pressHit
            if (hit) {
                if (mods & (Qt.ControlModifier | Qt.MetaModifier)) {
                    EditorState.subtitles.selectCue(hit.index, 1)
                    return
                }
                if (mods & Qt.ShiftModifier) {
                    EditorState.subtitles.selectCue(hit.index, 2)
                    return
                }
                if (root.panel.isSelected(hit.index))
                    root.collapseOnRelease = true
                else
                    EditorState.subtitles.selectCue(hit.index, 0)
                root.mode = hit.part
            } else {
                root.mode = (mods & (Qt.ControlModifier | Qt.MetaModifier)) ? "marquee" : "empty"
            }
        }

        onPressAndHold: (mouse) => {
            if (root.mode !== "touch" || !root.pressHit)
                return
            Haptics.pickUp()
            if (!root.panel.isSelected(root.pressHit.index))
                EditorState.subtitles.selectCue(root.pressHit.index, 0)
            root.mode = root.pressHit.part
            root.startDrag()
        }

        onPositionChanged: (mouse) => {
            if (!pressed) {
                hoverHit = root.hitTest(mouse.x, mouse.y)
                return
            }
            const dx = mouse.x - root.pressX
            const moved = Math.abs(dx) > 3 || Math.abs(mouse.y - root.pressY) > 3
            switch (root.mode) {
            case "touch":
                if (moved)
                    root.mode = "pan"
                break
            case "pan":
                root.timeView.contentX = root.panStartX - dx
                break
            case "move":
            case "trimStart":
            case "trimEnd":
                if (!root.dragging && moved)
                    root.startDrag()
                if (root.dragging)
                    root.updateDrag(mouse.x, mouse.modifiers)
                break
            case "empty":
                if (Math.abs(dx) > 3) {
                    root.mode = "create"
                    root.startCreate()
                    root.updateCreate(mouse.x)
                }
                break
            case "create":
                root.updateCreate(mouse.x)
                break
            case "marquee":
                if (!root.marqueeActive && moved)
                    root.marqueeActive = true
                if (root.marqueeActive)
                    root.updateMarquee(mouse.x)
                break
            }
        }

        onReleased: (mouse) => {
            const mode = root.mode
            root.mode = ""
            if (mode === "move" || mode === "trimStart" || mode === "trimEnd") {
                if (root.dragging) {
                    root.finishDrag()
                } else {
                    if (root.collapseOnRelease)
                        EditorState.subtitles.selectCue(root.pressHit.index, 0)
                    EditorState.playheadSeconds = root.panel.clipStart + root.panel.liveCues[root.pressHit.index].start
                }
            } else if (mode === "empty") {
                EditorState.subtitles.clearSelection()
                EditorState.playheadSeconds = EditorState.snapTime(Math.max(0, root.timeView.secondsFor(mouse.x)))
            } else if (mode === "create") {
                root.creating = false
                if (root.createEnd - root.createStart >= 0.2)
                    root.panel.insertCue(root.createStart, root.createEnd)
            } else if (mode === "marquee") {
                root.marqueeActive = false
            } else if (mode === "touch") {
                if (root.pressHit) {
                    EditorState.subtitles.selectCue(root.pressHit.index, 0)
                    EditorState.playheadSeconds = root.panel.clipStart + root.panel.liveCues[root.pressHit.index].start
                } else {
                    EditorState.subtitles.clearSelection()
                }
            }
        }

        onCanceled: {
            if (root.dragging)
                root.finishDrag()
            root.creating = false
            root.marqueeActive = false
            root.mode = ""
        }

        onExited: hoverHit = null

        onDoubleClicked: (mouse) => {
            if (mouse.button !== Qt.LeftButton)
                return
            const hit = root.hitTest(mouse.x, mouse.y)
            if (hit)
                root.panel.beginEdit(hit.index)
        }

        onWheel: (wheel) => {
            if (wheel.modifiers & Qt.ControlModifier) {
                root.timeView.zoomAt(wheel.x, Math.exp(wheel.angleDelta.y / 600))
                return
            }
            const dx = wheel.pixelDelta.x !== 0 ? wheel.pixelDelta.x : wheel.angleDelta.x / 2
            const dy = wheel.pixelDelta.y !== 0 ? wheel.pixelDelta.y : wheel.angleDelta.y / 2
            root.timeView.contentX -= dx !== 0 ? dx : dy
        }
    }

    PinchHandler {
        target: null
        acceptedDevices: PointerDevice.TouchScreen | PointerDevice.TouchPad
        property real startPps: 1
        onActiveChanged: {
            if (active) {
                startPps = root.timeView.pxPerSecond
                root.mode = ""
            }
        }
        onActiveScaleChanged: {
            if (active)
                root.timeView.zoomAt(centroid.position.x, startPps * activeScale / root.timeView.pxPerSecond)
        }
    }
}
