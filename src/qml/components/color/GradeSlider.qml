import QtQuick
import Drift

Item {
    id: root

    property string label: ""
    property real value: 0
    property real from: -1
    property real to: 1
    property real defaultValue: 0
    property real stepSize: 0.01
    property int decimals: 2
    property bool bipolar: true
    property var gradient: []
    property bool animated: false
    property int dragMoves: 0
    // Handle travel is warped by |p|^curvePower around defaultValue; only for symmetric ranges.
    property real curvePower: 1
    property real _pointerValue: 0
    property real _fineValue: 0

    signal began()
    signal moved(real value)
    signal ended()
    signal reset()

    readonly property bool gradientActive: gradient && gradient.length >= 2
    readonly property real span: Math.max(1e-9, to - from)
    readonly property bool modified: Math.abs(value - defaultValue) > stepSize / 2
    readonly property bool curved: curvePower !== 1 && Math.abs(from + to - 2 * defaultValue) < 1e-9

    function toSliderValue(v) {
        if (!curved)
            return v
        const p = Math.max(-1, Math.min(1, (v - defaultValue) / (span / 2)))
        return Math.sign(p) * Math.pow(Math.abs(p), 1 / curvePower)
    }

    function fromSliderValue(p) {
        if (!curved)
            return p
        return defaultValue + Math.sign(p) * Math.pow(Math.abs(p), curvePower) * (span / 2)
    }

    function resetToDefault() {
        commitTimer.stop()
        began()
        moved(defaultValue)
        ended()
        reset()
    }

    Timer {
        id: commitTimer
        interval: Qt.styleHints.mouseDoubleClickInterval + 50
        onTriggered: root.ended()
    }

    implicitWidth: 200
    implicitHeight: header.height + slider.height + Theme.spacingXs

    Item {
        id: header
        width: parent.width
        height: Theme.controlHeightSm

        Text {
            anchors.left: parent.left
            anchors.right: keyMark.visible ? keyMark.left : field.left
            anchors.rightMargin: Theme.spacingSm
            anchors.verticalCenter: parent.verticalCenter
            elide: Text.ElideRight
            text: root.label
            color: root.modified ? Theme.panelForeground : Theme.mutedForeground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeXs
            font.weight: root.modified ? Font.Medium : Font.Normal

            TapHandler {
                onDoubleTapped: root.resetToDefault()
            }

            HoverHandler {
                id: labelHover
            }

            ThemedToolTip {
                visible: labelHover.hovered
                text: qsTr("Double-click to reset. Hold Shift while dragging for fine control.")
            }
        }

        KeyframeDiamond {
            id: keyMark
            anchors.right: field.left
            anchors.rightMargin: Theme.spacingSm
            anchors.verticalCenter: parent.verticalCenter
            visible: root.animated
            animated: true
            tooltip: qsTr("Animated. Editing sets keyframes (needs Auto keyframes on).")
        }

        ThemedNumberField {
            id: field
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            width: 56
            from: root.from
            to: root.to
            step: root.stepSize
            decimals: root.decimals
            value: root.value
            onEdited: v => {
                root.began()
                root.moved(v)
                root.ended()
            }
        }
    }

    ThemedSlider {
        id: slider
        y: header.height + Theme.spacingXs
        width: parent.width
        label: root.label
        from: root.curved ? -1 : root.from
        to: root.curved ? 1 : root.to
        stepSize: root.stepSize
        showValueTooltip: false
        lockWhilePlaying: root.animated
        valueFormatter: v => Number(v).toFixed(root.decimals)

        Binding on value {
            when: !slider.pressed
            value: root.toSliderValue(root.value)
        }

        onPressedChanged: {
            if (pressed) {
                root.dragMoves = 0
                root._pointerValue = value
                root._fineValue = value
                if (commitTimer.running)
                    commitTimer.stop()
                else
                    root.began()
            } else if (root.dragMoves <= 1) {
                commitTimer.restart()
            } else {
                root.ended()
            }
        }
        onMoved: {
            if (pressed) {
                root.dragMoves++
                // Shift drags at a tenth of the pointer's speed; the handle follows the fine value.
                const fine = (EditorState.keyboardModifiers() & Qt.ShiftModifier) !== 0
                root._fineValue = fine
                    ? Math.max(slider.from, Math.min(slider.to, root._fineValue + (value - root._pointerValue) * 0.1))
                    : value
                root._pointerValue = value
                if (fine)
                    value = root._fineValue
                root.moved(root.fromSliderValue(root._fineValue))
                return
            }
            root.began()
            root.moved(root.fromSliderValue(value))
            root.ended()
        }

        TapHandler {
            onDoubleTapped: root.resetToDefault()
        }

        background: Item {
            x: slider.leftPadding
            y: slider.topPadding + slider.availableHeight / 2 - height / 2
            width: slider.availableWidth
            height: Theme.spacingSm

            Rectangle {
                anchors.fill: parent
                radius: Theme.c.themedSlider.trackRadius
                color: Theme.c.themedSlider.trackBackground
                visible: !root.gradientActive
            }

            Canvas {
                id: gradientTrack
                anchors.fill: parent
                visible: root.gradientActive
                onWidthChanged: requestPaint()
                onHeightChanged: requestPaint()
                Connections {
                    target: root
                    function onGradientChanged() { gradientTrack.requestPaint() }
                }
                onPaint: {
                    const ctx = getContext("2d")
                    ctx.reset()
                    const stops = root.gradient
                    if (!stops || stops.length < 2 || width < 2)
                        return
                    const g = ctx.createLinearGradient(0, 0, width, 0)
                    for (let i = 0; i < stops.length; ++i)
                        g.addColorStop(i / (stops.length - 1), String(stops[i]))
                    const r = height / 2
                    ctx.fillStyle = g
                    ctx.beginPath()
                    ctx.moveTo(r, 0)
                    ctx.lineTo(width - r, 0)
                    ctx.arc(width - r, r, r, -Math.PI / 2, Math.PI / 2)
                    ctx.lineTo(r, height)
                    ctx.arc(r, r, r, Math.PI / 2, 3 * Math.PI / 2)
                    ctx.closePath()
                    ctx.fill()
                }
            }

            Rectangle {
                readonly property real startPos: root.bipolar
                    ? Math.min(1, Math.max(0, (root.defaultValue - root.from) / root.span)) : 0
                x: Math.min(startPos, slider.visualPosition) * parent.width
                width: Math.abs(slider.visualPosition - startPos) * parent.width
                height: parent.height
                radius: parent.height / 2
                color: Theme.c.themedSlider.fillBackground
                visible: !root.gradientActive
            }

            Rectangle {
                visible: root.bipolar
                x: Math.min(1, Math.max(0, (root.defaultValue - root.from) / root.span)) * parent.width
                   - width / 2
                y: -1
                width: 1
                height: parent.height + 2
                color: Theme.panelForeground
                opacity: 0.35
            }
        }
    }
}
