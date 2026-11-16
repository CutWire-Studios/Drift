import QtQuick
import Drift
import "../../timeline/TimelineWheel.js" as TimelineWheel

// Light knob in the 3D view: drag slides it at its current distance, the wheel steps depth.
Rectangle {
    id: knob

    required property var source
    required property var overlay

    width: 18
    height: 18
    radius: Theme.c.depthLight3dKnob.radius
    x: knob.source.hx - width / 2
    y: knob.source.hy - height / 2
    color: knob.source.values.color || Theme.c.depthLight3dKnob.background
    border.width: Theme.c.depthLight3dKnob.borderWidth
    border.color: lightArea.containsMouse || lightArea.pressed
                  ? Theme.c.depthLight3dKnob.borderHover : Theme.c.depthLight3dKnob.border

    Text {
        anchors.centerIn: parent
        text: knob.source.modelData.light || ""
        // Dark on the light's own colour: every default is a pale tint.
        color: Theme.c.depthLight3dKnob.foreground
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSizeXs
        font.weight: Font.Medium
    }

    MouseArea {
        id: lightArea
        anchors.fill: parent
        anchors.margins: -Theme.spacingSm
        hoverEnabled: true
        cursorShape: pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor
        preventStealing: true
        property real wheelAccum: 0

        onPressed: (mouse) => {
            const p = lightArea.mapToItem(knob.overlay, mouse.x, mouse.y)
            knob.source.grab = Qt.point(knob.source.hx - p.x, knob.source.hy - p.y)
            knob.overlay.interacting = true
            EditorState.beginPreviewDrag(qsTr("Move light"))
        }
        onPositionChanged: (mouse) => {
            if (!pressed)
                return
            const p = lightArea.mapToItem(knob.overlay, mouse.x, mouse.y)
            const x = p.x + knob.source.grab.x
            const y = p.y + knob.source.grab.y
            const moved = EditorState.preview.moveDepthLight3d(knob.source.modelData.effect,
                                                               knob.source.modelData.light,
                                                               x / knob.overlay.sx, y / knob.overlay.sy)
            if (moved.x === undefined)
                return
            knob.source.liveX = x
            knob.source.liveY = y
            knob.overlay.writeParam(knob.source.prefix + "x", moved.x)
            knob.overlay.writeParam(knob.source.prefix + "y", moved.y)
            knob.overlay.writeParam(knob.source.prefix + "z", moved.z)
        }
        onReleased: { knob.source.clearLive(); knob.overlay.endInteraction() }
        onCanceled: { knob.source.clearLive(); knob.overlay.endInteraction() }

        // Scrolling up brings the light towards the viewer; Shift for fine steps.
        onWheel: (wheel) => {
            const n = TimelineWheel.notches(wheelAccum, TimelineWheel.oneAxis(wheel.angleDelta.x, wheel.angleDelta.y))
            wheelAccum = n.rest
            wheel.accepted = true
            if (n.steps === 0)
                return
            const step = (wheel.modifiers & Qt.ShiftModifier) !== 0 ? 0.01 : 0.05
            const z = Math.max(knob.source.values.zMin, Math.min(knob.source.values.zMax,
                               (knob.source.values.z || 0) - n.steps * step))
            EditorState.beginPreviewDrag(qsTr("Move light"))
            knob.overlay.writeParam(knob.source.prefix + "z", z)
            EditorState.commitPreviewDrag()
        }
    }
}
