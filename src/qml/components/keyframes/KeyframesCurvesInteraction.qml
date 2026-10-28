pragma ComponentBehavior: Bound
import QtQuick
import Drift
import "KeyframesModel.js" as Model

Item {
    id: root

    required property var panel
    required property var view

    readonly property var timeView: panel.timeView
    readonly property var selection: panel.selection
    readonly property bool busy: dotDrag || marqueeActive

    property string mode: ""
    property bool dotDrag: false
    property bool marqueeActive: false
    property var pressHit: null
    property bool collapseOnRelease: false
    property bool touch: false
    property real pressX: 0
    property real pressY: 0
    property real panStartX: 0
    property real panLastY: 0
    property var dragRefs: []
    property var dragResult: []
    property real dragOrigin: 0
    property var marqueeBase: []

    signal contextRequested(real x, real y, var hit)

    function emptyHit(x) {
        return { "row": -1, "rowData": null, "dot": null, "refs": [], "seconds": timeView.secondsFor(x),
                 "entryIndex": -1 }
    }

    function hitTest(x, y, slop) {
        const series = view.series
        let best = null
        let bestDist = slop + 0.5
        let bestFocused = false
        for (let s = 0; s < series.length; ++s) {
            const entry = series[s]
            const focused = view.isFocused(s)
            for (let i = 0; i < entry.points.length; ++i) {
                const p = entry.points[i]
                const d = Math.hypot(timeView.xFor(p.seconds) - x, view.screenY(p.value, entry) - y)
                if (d < bestDist || (d <= bestDist && focused && !bestFocused)) {
                    bestDist = d
                    bestFocused = focused
                    best = { "entryIndex": s, "point": p }
                }
            }
        }
        if (!best)
            return emptyHit(x)
        const entry = series[best.entryIndex]
        const refs = [{ "prop": entry.prop, "seconds": best.point.seconds }]
        return {
            "row": panel.rows.indexOf(entry.row),
            "rowData": entry.row,
            "dot": { "seconds": best.point.seconds, "refs": refs, "hold": !!best.point.hold },
            "refs": refs,
            "seconds": best.point.seconds,
            "entryIndex": best.entryIndex
        }
    }

    function focusHit(hit) {
        const entry = view.series[hit.entryIndex]
        view.focusedTrack = entry.prop
        panel.focusedProp = entry.rowProp
    }

    function removeRefs(refs) {
        const drop = {}
        for (let i = 0; i < refs.length; ++i)
            drop[Model.keyId(refs[i].prop, refs[i].seconds)] = true
        selection.replace(selection.refs().filter(r => !drop[Model.keyId(r.prop, r.seconds)]))
    }

    function startDotDrag() {
        dragRefs = selection.refs()
        dragResult = dragRefs
        dragOrigin = pressHit.seconds
        view.lockRanges()
        EditorState.beginPreviewDrag(qsTr("Move keyframes"))
        dotDrag = true
    }

    function updateDotDrag(x, y, shift, invertSnap) {
        let dx = x - pressX
        let dy = y - pressY
        if (shift) {
            if (Math.abs(dx) >= Math.abs(dy))
                dy = 0
            else
                dx = 0
        }
        const raw = Math.max(timeView.clipStart,
                             Math.min(timeView.clipStart + timeView.clipDuration,
                                      dragOrigin + dx / timeView.pxPerSecond))
        const dt = dx === 0 ? 0 : panel.snapKeyTime(raw, dragRefs, invertSnap) - dragOrigin
        const locked = view.lockedRanges || ({})
        const refs = dragRefs.map(r => {
            const range = locked[r.prop]
            const dv = range ? -dy / view.graphHeight * (range.max - range.min) : 0
            return { "prop": r.prop, "seconds": r.seconds, "dv": dv }
        })
        const res = EditorState.keyframes.previewMoveKeys(panel.track, panel.clip, refs, dt)
        if (res && res.length > 0) {
            dragResult = res
            selection.replace(res)
        }
    }

    function finishDotDrag() {
        EditorState.commitPreviewDrag()
        selection.replace(dragResult)
        view.unlockRanges()
        dotDrag = false
    }

    function cancelDrag() {
        if (dotDrag) {
            EditorState.cancelPreviewDrag()
            selection.replace(dragRefs)
            view.unlockRanges()
            dotDrag = false
        }
        marqueeActive = false
        mode = ""
    }

    function updateMarquee(x, y) {
        const x0 = Math.max(0, Math.min(pressX, x))
        const x1 = Math.min(width, Math.max(pressX, x))
        const y0 = Math.max(0, Math.min(pressY, y))
        const y1 = Math.min(height, Math.max(pressY, y))
        marquee.x = x0
        marquee.y = y0
        marquee.width = x1 - x0
        marquee.height = y1 - y0
        const refs = marqueeBase.slice()
        const series = view.series
        for (let s = 0; s < series.length; ++s) {
            const entry = series[s]
            for (let i = 0; i < entry.points.length; ++i) {
                const p = entry.points[i]
                const px = timeView.xFor(p.seconds)
                const py = view.screenY(p.value, entry)
                if (px >= x0 && px <= x1 && py >= y0 && py <= y1)
                    refs.push({ "prop": entry.prop, "seconds": p.seconds })
            }
        }
        selection.replace(refs)
    }

    function pressDot(mods, hit) {
        const selected = Model.refsSelected(selection, hit.refs)
        focusHit(hit)
        if (mods & (Qt.ControlModifier | Qt.MetaModifier)) {
            if (selected) {
                removeRefs(hit.refs)
                return false
            }
            selection.add(hit.refs)
        } else if (mods & Qt.ShiftModifier) {
            selection.add(hit.refs)
        } else if (!selected) {
            selection.replace(hit.refs)
        } else {
            collapseOnRelease = true
        }
        return true
    }

    function beginMarquee(mods) {
        marqueeBase = (mods & Qt.ShiftModifier) ? selection.refs() : []
        mode = "marquee"
    }

    function openContext(x, y, hit) {
        if (hit && hit.dot && !Model.refsSelected(selection, hit.refs))
            selection.replace(hit.refs)
        contextRequested(x, y, hit)
    }

    function addKeyAt(x) {
        if (view.focusedIndex < 0)
            return
        const s = Math.max(timeView.clipStart,
                           Math.min(timeView.clipStart + timeView.clipDuration,
                                    panel.snapKeyTime(timeView.secondsFor(x), [], false)))
        EditorState.keyframes.addKeysAt(panel.track, panel.clip, [view.series[view.focusedIndex].prop], s)
    }

    KeyframesMarquee {
        id: marquee
        visible: root.marqueeActive && width > 2 && height > 2
    }

    Connections {
        target: root.panel

        function onTrackChanged() { root.cancelDrag() }
        function onClipChanged() { root.cancelDrag() }
    }

    MouseArea {
        id: area
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton | Qt.RightButton | Qt.MiddleButton
        pressAndHoldInterval: Theme.touchLiftInterval
        cursorShape: root.dotDrag ? Qt.SizeAllCursor
                                  : (root.view.hoverId !== "" ? Qt.PointingHandCursor : Qt.ArrowCursor)

        onPressed: (mouse) => {
            if (mouse.button === Qt.LeftButton && root.view.gripAt(mouse.x, mouse.y)) {
                mouse.accepted = false
                return
            }
            root.panel.forceActiveFocus()
            root.touch = mouse.source !== Qt.MouseEventNotSynthesized
            root.pressX = mouse.x
            root.pressY = mouse.y
            root.collapseOnRelease = false
            root.pressHit = root.hitTest(mouse.x, mouse.y, root.touch ? 14 : 6)
            root.mode = ""
            if (mouse.button === Qt.MiddleButton) {
                root.panStartX = root.timeView.contentX
                root.panLastY = mouse.y
                root.mode = "pan"
                return
            }
            if (mouse.button === Qt.RightButton) {
                if (!root.touch)
                    root.openContext(mouse.x, mouse.y, root.pressHit)
                return
            }
            if (root.touch) {
                root.panStartX = root.timeView.contentX
                root.panLastY = mouse.y
                root.mode = "touch"
                return
            }
            if (root.pressHit.dot) {
                if (root.pressDot(mouse.modifiers, root.pressHit))
                    root.mode = "dot"
            } else {
                if (!(mouse.modifiers & Qt.ShiftModifier))
                    root.selection.clear()
                root.beginMarquee(mouse.modifiers)
            }
        }

        onPressAndHold: (mouse) => {
            if (root.mode !== "touch")
                return
            Haptics.pickUp()
            if (root.pressHit.dot) {
                root.pressDot(0, root.pressHit)
                root.startDotDrag()
                root.mode = "dot"
            } else {
                root.beginMarquee(0)
                root.marqueeActive = true
            }
        }

        onPositionChanged: (mouse) => {
            if (!pressed) {
                const hit = root.hitTest(mouse.x, mouse.y, 6)
                root.view.hoverId = hit.dot ? Model.keyId(hit.refs[0].prop, hit.seconds) : ""
                return
            }
            const moved = Math.abs(mouse.x - root.pressX) > 3 || Math.abs(mouse.y - root.pressY) > 3
            if (root.mode === "touch") {
                if (!moved)
                    return
                root.mode = "pan"
            }
            if (root.mode === "pan") {
                root.timeView.contentX = root.panStartX - (mouse.x - root.pressX)
                if (!root.panel.normalize)
                    root.view.panValuesBy(mouse.y - root.panLastY)
                root.panLastY = mouse.y
            } else if (root.mode === "dot") {
                if (!root.dotDrag && moved)
                    root.startDotDrag()
                if (root.dotDrag)
                    root.updateDotDrag(mouse.x, mouse.y, mouse.modifiers & Qt.ShiftModifier,
                                       mouse.modifiers & Qt.AltModifier)
            } else if (root.mode === "marquee") {
                if (!root.marqueeActive && moved)
                    root.marqueeActive = true
                if (root.marqueeActive)
                    root.updateMarquee(mouse.x, mouse.y)
            }
        }

        onReleased: (mouse) => {
            const mode = root.mode
            root.mode = ""
            if (mouse.button === Qt.RightButton)
                return
            if (mode === "dot") {
                if (root.dotDrag) {
                    root.finishDotDrag()
                } else if (root.collapseOnRelease) {
                    root.selection.replace(root.pressHit.refs)
                }
            } else if (mode === "marquee") {
                root.marqueeActive = false
            } else if (mode === "touch") {
                const hit = root.pressHit
                if (hit.dot) {
                    root.focusHit(hit)
                    if (Model.refsSelected(root.selection, hit.refs))
                        root.openContext(mouse.x, mouse.y, hit)
                    else
                        root.selection.replace(hit.refs)
                } else {
                    root.selection.clear()
                }
            }
        }

        onCanceled: root.cancelDrag()

        onExited: root.view.hoverId = ""

        onDoubleClicked: (mouse) => {
            if (mouse.button !== Qt.LeftButton)
                return
            const hit = root.hitTest(mouse.x, mouse.y, root.touch ? 14 : 6)
            if (hit.dot)
                EditorState.playheadSeconds = hit.seconds
            else
                root.addKeyAt(mouse.x)
        }

        onWheel: (wheel) => {
            const mods = wheel.modifiers
            const dx = wheel.pixelDelta.x !== 0 ? wheel.pixelDelta.x : wheel.angleDelta.x / 2
            const dy = wheel.pixelDelta.y !== 0 ? wheel.pixelDelta.y : wheel.angleDelta.y / 2
            const sharedRange = !root.panel.normalize
            if (mods & Qt.ControlModifier) {
                if (mods & Qt.ShiftModifier) {
                    if (sharedRange)
                        root.view.zoomValuesAt(wheel.y, Math.exp(-(dy !== 0 ? dy : dx) / 300))
                } else {
                    root.timeView.zoomAt(wheel.x, Math.exp(wheel.angleDelta.y / 600))
                }
                return
            }
            if (mods & Qt.ShiftModifier) {
                root.timeView.contentX -= (dy !== 0 ? dy : dx)
                return
            }
            if (dx !== 0)
                root.timeView.contentX -= dx
            if (dy !== 0 && sharedRange)
                root.view.panValuesBy(dy)
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
