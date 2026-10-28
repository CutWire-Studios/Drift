pragma ComponentBehavior: Bound
import QtQuick
import Drift
import "KeyframesModel.js" as Model

Item {
    id: root

    required property var panel
    required property var lanes

    readonly property var timeView: panel.timeView
    readonly property var selection: panel.selection
    readonly property alias anchorProxy: anchorProxy
    readonly property bool dragging: dotDrag
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
    property real panStartY: 0
    property var dragRefs: []
    property var dragResult: []
    property real dragOrigin: 0
    property var marqueeBase: []

    signal contextRequested(real x, real y, var hit)

    function hitTest(x, y, slop) {
        const rows = panel.rows
        const idx = Math.floor((y + lanes.list.contentY) / lanes.rowHeight)
        if (idx < 0 || idx >= rows.length)
            return null
        const dots = Model.laneDots(rows, idx)
        let best = null
        let bestDist = slop + 0.5
        for (let i = 0; i < dots.length; ++i) {
            const d = Math.abs(timeView.xFor(dots[i].seconds) - x)
            if (d < bestDist) {
                bestDist = d
                best = dots[i]
            }
        }
        return {
            "row": idx,
            "rowData": rows[idx],
            "dot": best,
            "refs": best ? best.refs : [],
            "seconds": best ? best.seconds : timeView.secondsFor(x)
        }
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
        EditorState.beginPreviewDrag(qsTr("Move keyframes"))
        dotDrag = true
    }

    function updateDotDrag(x, invertSnap) {
        const raw = Math.max(timeView.clipStart,
                             Math.min(timeView.clipStart + timeView.clipDuration,
                                      dragOrigin + (x - pressX) / timeView.pxPerSecond))
        const res = EditorState.keyframes.previewMoveKeys(panel.track, panel.clip, dragRefs,
                                                          panel.snapKeyTime(raw, dragRefs, invertSnap) - dragOrigin)
        if (res && res.length > 0) {
            dragResult = res
            selection.replace(res)
        }
    }

    function finishDotDrag() {
        EditorState.commitPreviewDrag()
        selection.replace(dragResult)
        dotDrag = false
    }

    function cancelDrag() {
        if (dotDrag) {
            EditorState.cancelPreviewDrag()
            selection.replace(dragRefs)
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
        const s0 = timeView.secondsFor(x0)
        const s1 = timeView.secondsFor(x1)
        const first = Math.floor((y0 + lanes.list.contentY) / lanes.rowHeight)
        const last = Math.floor((y1 + lanes.list.contentY) / lanes.rowHeight)
        const refs = marqueeBase.slice()
        for (let r = Math.max(0, first); r <= last && r < panel.rows.length; ++r) {
            const dots = Model.laneDots(panel.rows, r)
            for (let i = 0; i < dots.length; ++i) {
                if (dots[i].seconds >= s0 && dots[i].seconds <= s1)
                    Array.prototype.push.apply(refs, dots[i].refs)
            }
        }
        selection.replace(refs)
    }

    function pressDot(mods, hit) {
        const selected = Model.refsSelected(selection, hit.refs)
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

    Item {
        id: anchorProxy
        width: 1
        height: 1
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
        cursorShape: root.dotDrag ? Qt.SizeHorCursor
                                  : (root.lanes.hoverRow >= 0 ? Qt.PointingHandCursor : Qt.ArrowCursor)

        onPressed: (mouse) => {
            root.panel.forceActiveFocus()
            root.touch = mouse.source !== Qt.MouseEventNotSynthesized
            root.pressX = mouse.x
            root.pressY = mouse.y
            root.collapseOnRelease = false
            root.pressHit = root.hitTest(mouse.x, mouse.y, root.touch ? 14 : 6)
            root.mode = ""
            if (mouse.button === Qt.MiddleButton) {
                root.panStartX = root.timeView.contentX
                root.panStartY = root.lanes.list.contentY
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
                root.panStartY = root.lanes.list.contentY
                root.mode = "touch"
                return
            }
            if (root.pressHit && root.pressHit.dot) {
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
            if (root.pressHit && root.pressHit.dot) {
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
                root.lanes.hoverRow = hit && hit.dot ? hit.row : -1
                root.lanes.hoverSeconds = hit && hit.dot ? hit.seconds : 0
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
                root.lanes.list.contentY = Math.max(0, Math.min(root.lanes.maxContentY(),
                                                                root.panStartY - (mouse.y - root.pressY)))
            } else if (root.mode === "dot") {
                if (!root.dotDrag && moved)
                    root.startDotDrag()
                if (root.dotDrag)
                    root.updateDotDrag(mouse.x, mouse.modifiers & Qt.AltModifier)
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
                if (hit && hit.dot) {
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

        onExited: root.lanes.hoverRow = -1

        onDoubleClicked: (mouse) => {
            if (mouse.button !== Qt.LeftButton)
                return
            const hit = root.hitTest(mouse.x, mouse.y, root.touch ? 14 : 6)
            if (!hit)
                return
            if (hit.dot)
                EditorState.playheadSeconds = hit.seconds
            else if (hit.rowData.type !== "group")
                EditorState.keyframes.addKeysAt(root.panel.track, root.panel.clip, hit.rowData.tracks,
                                                root.panel.snapKeyTime(hit.seconds, [], false))
        }

        onWheel: (wheel) => {
            const mods = wheel.modifiers
            if (mods & Qt.ControlModifier) {
                root.timeView.zoomAt(wheel.x, Math.exp(wheel.angleDelta.y / 600))
                return
            }
            const dx = wheel.pixelDelta.x !== 0 ? wheel.pixelDelta.x : wheel.angleDelta.x / 2
            const dy = wheel.pixelDelta.y !== 0 ? wheel.pixelDelta.y : wheel.angleDelta.y / 2
            if (mods & Qt.ShiftModifier) {
                root.lanes.panBy(-(dy !== 0 ? dy : dx))
                return
            }
            if (dx !== 0)
                root.lanes.panBy(-dx)
            if (dy !== 0)
                root.lanes.scrollBy(-dy)
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
