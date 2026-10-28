pragma ComponentBehavior: Bound
import QtQuick
import Drift

Item {
    id: root

    required property var panel
    required property var lanes

    readonly property var timeView: panel.timeView
    readonly property var selection: panel.selection
    readonly property var rowsModel: panel.rowsModel
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
    property real pressContentX: 0
    property real pressContentY: 0
    property real lastX: 0
    property real lastY: 0
    property bool lastInvert: false
    property real dragTime: 0
    property string hoverTip: ""
    readonly property real edgeZone: 24
    readonly property real fps: Math.max(1, EditorState.projectFile.projectFps())
    readonly property real slop: touch || Theme.touchInput ? 14 : 8

    signal contextRequested(real x, real y, var hit)

    function hitTest(x, y, slop) {
        const hit = rowsModel.hitTest(x, y, lanes.list.contentY, lanes.rowHeight, slop)
        if (hit.row < 0)
            return null
        hit.rowData = panel.rows[hit.row]
        return hit
    }

    function startDotDrag() {
        dragRefs = selection.refs()
        dragResult = dragRefs
        dragOrigin = pressHit.seconds
        dragTime = dragOrigin
        EditorState.beginPreviewDrag(qsTr("Move keyframes"))
        dotDrag = true
    }

    function updateDotDrag(x, invertSnap) {
        lastInvert = !!invertSnap
        const dt = rowsModel.moveDelta(dragOrigin, x - pressX + timeView.contentX - pressContentX, dragRefs,
                                       lastInvert, false)
        dragTime = dragOrigin + dt
        const res = EditorState.keyframes.previewMoveKeys(panel.track, panel.clip, dragRefs, dt)
        if (res && res.length > 0) {
            dragResult = res
            selection.replace(res)
        }
    }

    function finishDotDrag() {
        EditorState.commitPreviewDrag()
        EditorState.keyframes.clearCollisions()
        selection.replace(dragResult)
        dotDrag = false
    }

    function cancelDrag() {
        if (dotDrag) {
            EditorState.cancelPreviewDrag()
            EditorState.keyframes.clearCollisions()
            selection.replace(dragRefs)
            dotDrag = false
        }
        marqueeActive = false
        mode = ""
    }

    function updateMarquee(x, y) {
        const anchorX = pressX - (timeView.contentX - pressContentX)
        const anchorY = pressY - (lanes.list.contentY - pressContentY)
        const px = Math.max(0, Math.min(width, x))
        const py = Math.max(0, Math.min(height, y))
        const x0 = Math.min(anchorX, px)
        const x1 = Math.max(anchorX, px)
        const y0 = Math.min(anchorY, py)
        const y1 = Math.max(anchorY, py)
        marquee.x = Math.max(0, x0)
        marquee.y = Math.max(0, y0)
        marquee.width = Math.min(width, x1) - marquee.x
        marquee.height = Math.min(height, y1) - marquee.y
        selection.replace(rowsModel.marqueeRefs(x0, x1, y0, y1, lanes.list.contentY, lanes.rowHeight,
                                                marqueeBase))
    }

    function edgePush(pos, size) {
        if (pos < edgeZone)
            return -(edgeZone - pos) * 0.5
        if (pos > size - edgeZone)
            return (pos - (size - edgeZone)) * 0.5
        return 0
    }

    function autoScroll() {
        const dx = edgePush(lastX, width)
        const dy = marqueeActive ? edgePush(lastY, height) : 0
        if (dx === 0 && dy === 0)
            return
        if (dx !== 0) {
            const lo = lanes.scrollMin
            const hi = Math.max(lo, lanes.scrollMin + lanes.scrollTotal - lanes.width)
            timeView.contentX = Math.max(lo, Math.min(hi, timeView.contentX + dx))
        }
        if (dy !== 0)
            lanes.scrollBy(dy)
        if (marqueeActive)
            updateMarquee(lastX, lastY)
        else
            updateDotDrag(lastX, lastInvert)
    }

    function readoutText() {
        const frames = Math.round((dragTime - dragOrigin) * fps)
        return panel.formatTimecode(dragTime) + "  " + (frames >= 0 ? "+" : "") + frames
    }

    function tipFor(hit) {
        if (!hit || !hit.dot)
            return ""
        const row = hit.rowData
        let text = (row.label ?? "") + "\n" + panel.formatTimecode(hit.seconds)
        if (row.type !== "group") {
            const keys = row.keys
            for (let i = 0; i < keys.length; ++i) {
                if (Math.abs(keys[i].seconds - hit.seconds) < 1e-4) {
                    text += "\n" + Number(keys[i].value).toFixed(row.decimals ?? 2)
                    break
                }
            }
        } else {
            text += "\n" + qsTr("%n key(s)", "", hit.refs.length)
        }
        return text
    }

    function pressDot(mods, hit) {
        const outcome = selection.press(mods, hit.refs)
        collapseOnRelease = outcome === 2
        return outcome !== 0
    }

    function beginMarquee(mods) {
        marqueeBase = (mods & Qt.ShiftModifier) ? selection.refs() : []
        mode = "marquee"
    }

    function openContext(x, y, hit) {
        if (hit && hit.dot && !selection.hasAll(hit.refs))
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

    KeyframesDragReadout {
        visible: root.dotDrag
        text: root.readoutText()
        x: Math.max(0, Math.min(root.width - width, root.lastX + Theme.spacingXl))
        y: Math.max(0, root.lastY - height - Theme.spacingLg)
    }

    Timer {
        interval: 16
        repeat: true
        running: root.dotDrag || root.marqueeActive
        onTriggered: root.autoScroll()
    }

    ThemedToolTip {
        parent: root
        delay: 600
        x: root.lastX + Theme.spacingXl
        y: root.lastY + Theme.spacingXl
        text: root.hoverTip
        visible: root.hoverTip !== "" && !area.pressed
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
            root.lastX = mouse.x
            root.lastY = mouse.y
            root.pressContentX = root.timeView.contentX
            root.pressContentY = root.lanes.list.contentY
            root.hoverTip = ""
            root.collapseOnRelease = false
            root.pressHit = root.hitTest(mouse.x, mouse.y, root.slop)
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
            root.lastX = mouse.x
            root.lastY = mouse.y
            if (!pressed) {
                const hit = root.hitTest(mouse.x, mouse.y, root.slop)
                root.lanes.hoverRow = hit && hit.dot ? hit.row : -1
                root.lanes.hoverSeconds = hit && hit.dot ? hit.seconds : 0
                root.hoverTip = root.tipFor(hit)
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
                    if (root.selection.hasAll(hit.refs))
                        root.openContext(mouse.x, mouse.y, hit)
                    else
                        root.selection.replace(hit.refs)
                } else {
                    root.selection.clear()
                }
            }
        }

        onCanceled: root.cancelDrag()

        onExited: {
            root.lanes.hoverRow = -1
            root.hoverTip = ""
        }

        onDoubleClicked: (mouse) => {
            if (mouse.button !== Qt.LeftButton)
                return
            const hit = root.hitTest(mouse.x, mouse.y, root.slop)
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
