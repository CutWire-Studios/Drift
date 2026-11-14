import QtQuick
import Drift

Rectangle {
    property var host
    id: root
    required property var modelData
    readonly property var entry: host.series[modelData.seriesIndex]
    width: 10
    height: 10
    rotation: 45
    radius: Theme.c.keyframeGraphKeyDot.radius
    color: modelData.color
    border.width: Theme.c.keyframeGraphKeyDot.borderWidth
    border.color: Theme.c.keyframeGraphKeyDot.border
    opacity: (host.curveEditing && !host.isFocused(modelData.seriesIndex))
             ? Theme.c.keyframeGraphKeyDot.opacityUnfocused : 1.0
    readonly property bool isLinkedPartnerKey: EditorState.positionLinked
        && ((host.dragProp === "positionX" && modelData.prop === "positionY") || (host.dragProp === "positionY" && modelData.prop === "positionX"))
        && host.dragIndex >= 0 && host.focusedIndex >= 0 && host.focusedIndex < host.series.length
        && Math.abs((host.series[host.focusedIndex].points[host.dragIndex] ? host.series[host.focusedIndex].points[host.dragIndex].seconds : -999) - modelData.seconds) < (1 / 30)
    x: host.xForSeconds(modelData.seconds) - width / 2 + (isLinkedPartnerKey ? (host.xForSeconds(host.dragSeconds) - host.xForSeconds(modelData.seconds)) : dragDx)
    y: host.yForValue(modelData.value, entry) - height / 2 + dragDy
    z: 2

    // Offsets rather than direct x/y writes, which would
    // clobber the bindings above for good.
    property real dragDx: 0
    property real dragDy: 0
    property real editSeconds: modelData.seconds

    // Touching a key moves focus to its series, which is what arms
    // that curve's tangent handles.
    function remove() {
        EditorState.removeClipKeyframe(
            EditorState.selectedTrack,
            EditorState.selectedClip,
            root.modelData.prop,
            root.modelData.seconds
        )
    }

    TapHandler {
        // Finger taps land on touchMove below, which takes the press.
        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                         | PointerDevice.Stylus
        onTapped: host.focusedProp = root.modelData.prop
        onDoubleTapped: root.remove()
    }

    TapHandler {
        acceptedButtons: Qt.RightButton
        acceptedDevices: PointerDevice.Mouse
        onTapped: host.openKeyMenu(root, point.position.x, point.position.y)
    }

    function beginMove() {
        host.focusedProp = root.modelData.prop
        root.editSeconds = root.modelData.seconds
        host.frozenSeries = host.series
        host.dragProp = root.modelData.prop
        host.dragIndex = root.modelData.pointIndex
        host.dragSeconds = root.modelData.seconds
        host.dragValue = root.modelData.value
        host.draggingKey = true
        EditorState.beginPreviewDrag(qsTr("Move keyframe"))
    }
    function endMove() {
        // Unfreezing rebuilds the Repeater and destroys this
        // delegate mid-handler, so every reset has to land first.
        host.dragProp = ""
        host.dragIndex = -1
        root.dragDx = 0
        root.dragDy = 0
        EditorState.commitPreviewDrag()
        host.draggingKey = false
    }
    function moveBy(dx, dy) {
        const entry = root.entry
        const baseSec = root.modelData.seconds
        const baseVal = root.modelData.value
        // snapTime pulls in beats, clip edges and the playhead,
        // and is a no-op when the toolbar's snap toggle is off.
        const rawSec = baseSec + dx / host.pxPerSecond
        const newSec = Math.max(
            host.clipStart,
            Math.min(host.clipStart + host.clipDuration,
                     EditorState.snapTime(rawSec)))
        const isShift = (EditorState.keyboardModifiers() & Qt.ShiftModifier) !== 0
        const newVal = isShift ? baseVal : Math.max(
            entry.valueMin,
            Math.min(entry.valueMax,
                     host.valueForY(
                         host.yForValue(baseVal, entry) + dy,
                         entry)))
        EditorState.previewMoveClipKeyframe(
            EditorState.selectedTrack, EditorState.selectedClip,
            root.modelData.prop, root.editSeconds, newSec, newVal)
        root.editSeconds = newSec
        root.dragDx = host.xForSeconds(newSec) - host.xForSeconds(baseSec)
        root.dragDy = host.yForValue(newVal, entry)
                      - host.yForValue(baseVal, entry)
        host.dragSeconds = newSec
        host.dragValue = newVal
    }

    DragHandler {
        target: null
        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                         | PointerDevice.Stylus
        onActiveChanged: {
            if (active)
                root.beginMove()
            else
                root.endMove()
        }
        onTranslationChanged: {
            if (active)
                root.moveBy(translation.x, translation.y)
        }
    }

    // Touchscreen: a finger on the lane pans it, so a key only
    // moves after a hold. Mouse presses are rejected for the DragHandler.
    MouseArea {
        id: touchMove
        anchors.fill: parent
        anchors.margins: -10
        pressAndHoldInterval: Theme.touchLiftInterval
        preventStealing: armed
        property bool armed: false
        property point origin
        onPressed: (mouse) => {
            if (mouse.source === Qt.MouseEventNotSynthesized) {
                mouse.accepted = false
                return
            }
            armed = false
        }
        onPressAndHold: (mouse) => {
            origin = mapToItem(null, mouse.x, mouse.y)
            armed = true
            Haptics.pickUp()
            root.beginMove()
        }
        onPositionChanged: (mouse) => {
            if (!armed)
                return
            const p = mapToItem(null, mouse.x, mouse.y)
            root.moveBy(p.x - origin.x, p.y - origin.y)
        }
        onClicked: host.focusedProp = root.modelData.prop
        onDoubleClicked: root.remove()
        onReleased: finish()
        onCanceled: finish()
        function finish() {
            if (!armed)
                return
            armed = false
            root.endMove()
        }
    }
}
