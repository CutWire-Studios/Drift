import QtQuick
import Drift

Item {
    id: root

    property string label: ""
    property point offset: Qt.point(0, 0)
    property real luma: 0
    property real size: 140
    property bool animated: false

    signal began()
    signal offsetMoved(real x, real y)
    signal lumaMoved(real v)
    signal ended()

    readonly property real radius: size / 2
    readonly property real puckSize: Theme.touchInput ? 18 : 12
    readonly property real chromeHeight: implicitHeight - size
    readonly property bool modified: offset.x !== 0 || offset.y !== 0 || luma !== 0
    property point liveOffset: offset
    readonly property point shown: drag.pressed ? liveOffset : offset

    function resetOffset() {
        began()
        offsetMoved(0, 0)
        ended()
    }

    function resetAll() {
        began()
        offsetMoved(0, 0)
        lumaMoved(0)
        ended()
    }

    function nudge(dx, dy) {
        let nx = offset.x + dx
        let ny = offset.y + dy
        const len = Math.hypot(nx, ny)
        if (len > 1) {
            nx /= len
            ny /= len
        }
        began()
        offsetMoved(nx, ny)
        ended()
    }

    activeFocusOnTab: true
    Keys.onPressed: event => {
        const step = (event.modifiers & Qt.ShiftModifier) ? 0.001 : 0.01
        if (event.key === Qt.Key_Left)
            nudge(-step, 0)
        else if (event.key === Qt.Key_Right)
            nudge(step, 0)
        else if (event.key === Qt.Key_Up)
            nudge(0, step)
        else if (event.key === Qt.Key_Down)
            nudge(0, -step)
        else if (event.key === Qt.Key_Delete || event.key === Qt.Key_Backspace)
            resetOffset()
        else
            return
        event.accepted = true
    }

    implicitWidth: size
    width: implicitWidth
    implicitHeight: header.height + size + lumaSlider.height + Theme.spacingMd * 2

    Item {
        id: header
        width: parent.width
        height: Theme.controlHeightSm

        Text {
            anchors.left: parent.left
            anchors.right: keyMark.visible ? keyMark.left : resetButton.left
            anchors.verticalCenter: parent.verticalCenter
            elide: Text.ElideRight
            text: root.label
            color: Theme.mutedForeground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeXs
        }

        KeyframeDiamond {
            id: keyMark
            anchors.right: resetButton.left
            anchors.verticalCenter: parent.verticalCenter
            visible: root.animated
            animated: true
            tooltip: qsTr("Animated. Editing sets keyframes (needs Auto keyframes on).")
        }

        IconButton {
            id: resetButton
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            glyph: Theme.icons.reset
            tooltip: qsTr("Reset %1").arg(root.label)
            buttonSize: Theme.controlHeightSm
            enabled: root.modified
            onClicked: root.resetAll()
        }
    }

    Item {
        id: disc
        y: header.height + Theme.spacingMd
        anchors.horizontalCenter: parent.horizontalCenter
        width: root.size
        height: root.size

        // The disc shows the YIQ shift the grade shader applies, laid over the theme's neutral:
        // a conical gradient of the rim tints under a radial fade back to neutral. Canvas
        // putImageData draws nothing on current Qt, so the disc is built from gradients.
        Canvas {
            id: hueDisc
            anchors.fill: parent
            onWidthChanged: requestPaint()
            onHeightChanged: requestPaint()
            Connections {
                target: Theme
                function onDarkModeChanged() { hueDisc.requestPaint() }
            }
            onPaint: {
                const ctx = getContext("2d")
                ctx.reset()
                const w = width
                const h = height
                if (w < 2 || h < 2)
                    return
                const cx = w / 2
                const cy = h / 2
                const rad = Math.min(w, h) / 2
                const n = Theme.c.colorWheel.discNeutral
                const k = Theme.c.colorWheel.rimStrength
                const clamp = v => Math.min(1, Math.max(0, v))
                const g = ctx.createConicalGradient(cx, cy, 0)
                const steps = 24
                for (let s = 0; s <= steps; ++s) {
                    const a = s / steps * 2 * Math.PI
                    const yi = 0.5957 * Math.cos(a)
                    const yq = 0.5226 * Math.sin(a)
                    g.addColorStop(s / steps, Qt.rgba(
                        clamp(n.r + k * (0.956 * yi + 0.619 * yq)),
                        clamp(n.g + k * (-0.272 * yi - 0.647 * yq)),
                        clamp(n.b + k * (-1.106 * yi + 1.703 * yq)), 1))
                }
                ctx.fillStyle = g
                ctx.beginPath()
                ctx.arc(cx, cy, rad, 0, 2 * Math.PI)
                ctx.fill()
                const r = ctx.createRadialGradient(cx, cy, 0, cx, cy, rad)
                r.addColorStop(0, Qt.rgba(n.r, n.g, n.b, 1))
                r.addColorStop(1, Qt.rgba(n.r, n.g, n.b, 0))
                ctx.fillStyle = r
                ctx.beginPath()
                ctx.arc(cx, cy, rad, 0, 2 * Math.PI)
                ctx.fill()
            }
        }

        Rectangle {
            anchors.fill: parent
            radius: width / 2
            color: "transparent"
            border.width: root.activeFocus ? Theme.c.colorWheel.puckBorderWidth : Theme.c.colorWheel.ringWidth
            border.color: root.activeFocus ? Theme.focusRing
                          : drag.pressed ? Theme.c.colorWheel.ringActive
                                         : Theme.c.colorWheel.ring
        }

        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            width: Theme.c.vec2ParamPad.crosshairWidth
            height: parent.height
            color: Theme.c.colorWheel.crosshair
            opacity: Theme.c.colorWheel.crosshairOpacity
        }
        Rectangle {
            anchors.verticalCenter: parent.verticalCenter
            width: parent.width
            height: Theme.c.vec2ParamPad.crosshairWidth
            color: Theme.c.colorWheel.crosshair
            opacity: Theme.c.colorWheel.crosshairOpacity
        }

        Rectangle {
            readonly property real length: Math.hypot(root.shown.x, root.shown.y) * root.radius
            visible: length > 0.5
            x: root.radius
            y: root.radius - height / 2
            width: length
            height: Theme.c.vec2ParamPad.crosshairWidth
            transformOrigin: Item.Left
            rotation: -Math.atan2(root.shown.y, root.shown.x) * 180 / Math.PI
            color: Theme.c.colorWheel.vector
            opacity: Theme.c.colorWheel.vectorOpacity
        }

        Rectangle {
            width: root.puckSize
            height: root.puckSize
            radius: width / 2
            color: Theme.c.colorWheel.puck
            border.width: Theme.c.colorWheel.puckBorderWidth
            border.color: Theme.c.colorWheel.puckBorder
            x: root.radius + root.shown.x * root.radius - width / 2
            y: root.radius - root.shown.y * root.radius - height / 2
        }

        HoverHandler {
            id: discHover
        }

        ThemedToolTip {
            visible: discHover.hovered && !drag.pressed
            text: qsTr("Drag to tint. Shift for fine control. Double-click to reset the color.")
        }

        MouseArea {
            id: drag
            anchors.fill: parent
            cursorShape: Qt.CrossCursor
            preventStealing: true

            property real lastX: 0
            property real lastY: 0
            property var lockedFlickable: null
            property bool begun: false

            function lockFlickable() {
                if (lockedFlickable)
                    return
                let node = root.parent
                while (node) {
                    if (node.dragLocks !== undefined && node.contentHeight !== undefined) {
                        lockedFlickable = node
                        node.dragLocks++
                        return
                    }
                    node = node.parent
                }
            }
            function unlockFlickable() {
                if (lockedFlickable) {
                    lockedFlickable.dragLocks = Math.max(0, lockedFlickable.dragLocks - 1)
                    lockedFlickable = null
                }
            }
            function finish() {
                unlockFlickable()
                if (begun) {
                    begun = false
                    root.ended()
                }
            }

            Component.onDestruction: unlockFlickable()

            onPressed: mouse => {
                if (Math.hypot(mouse.x - root.radius, mouse.y - root.radius) > root.radius + 6) {
                    mouse.accepted = false
                    return
                }
                root.forceActiveFocus()
                lockFlickable()
                lastX = mouse.x
                lastY = mouse.y
                root.liveOffset = root.offset
            }
            onPositionChanged: mouse => {
                if (!pressed)
                    return
                if (!begun) {
                    begun = true
                    root.began()
                }
                const fine = (mouse.modifiers & Qt.ShiftModifier) ? 0.1 : 1
                let nx = root.liveOffset.x + (mouse.x - lastX) / root.radius * fine
                let ny = root.liveOffset.y - (mouse.y - lastY) / root.radius * fine
                lastX = mouse.x
                lastY = mouse.y
                const len = Math.sqrt(nx * nx + ny * ny)
                if (len > 1) {
                    nx /= len
                    ny /= len
                }
                root.liveOffset = Qt.point(nx, ny)
                root.offsetMoved(nx, ny)
            }
            onReleased: finish()
            onCanceled: finish()
            onDoubleClicked: root.resetOffset()
        }
    }

    GradeSlider {
        id: lumaSlider
        y: disc.y + root.size + Theme.spacingMd
        width: parent.width
        label: qsTr("Brightness")
        from: -1
        to: 1
        defaultValue: 0
        value: root.luma
        gradient: Theme.c.colorGradeTracks.luma
        onBegan: root.began()
        onMoved: v => root.lumaMoved(v)
        onEnded: root.ended()
    }
}
