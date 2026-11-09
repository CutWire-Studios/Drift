import QtQuick
import Drift

// A two-axis effect param: a dot dragged inside a rectangle, plus exact X / Y fields. It keyframes
// as the two float tracks "<prop>.x" and "<prop>.y". With keys present, edits write the key at the
// playhead; without, they set the static value.
Column {
    id: root

    required property var paramData
    required property int effectIndex

    readonly property var keys: (paramData.keyframes && paramData.keyframes.points) || []
    readonly property bool animated: keys.length > 0
    readonly property real lo: paramData.min
    readonly property real hi: paramData.max
    property real liveX: NaN
    property real liveY: NaN

    function axisAt(suffix, fallback) {
        if (!animated)
            return fallback
        return EditorState.propertyValueAt(EditorState.selectedTrack, EditorState.selectedClip,
                                           paramData.prop + suffix, EditorState.inspectorPlayheadSeconds,
                                           fallback)
    }

    readonly property real curX: !isNaN(liveX) ? liveX : axisAt(".x", Number(paramData.value))
    readonly property real curY: !isNaN(liveY) ? liveY : axisAt(".y", Number(paramData.valueY))

    function clampAxis(v) {
        return Math.min(hi, Math.max(lo, v))
    }

    function preview(x, y) {
        liveX = x
        liveY = y
        if (animated) {
            EditorState.previewSetClipKeyframe(EditorState.selectedTrack, EditorState.selectedClip,
                                               paramData.prop + ".x", EditorState.playheadSeconds, x)
            EditorState.previewSetClipKeyframe(EditorState.selectedTrack, EditorState.selectedClip,
                                               paramData.prop + ".y", EditorState.playheadSeconds, y)
        } else {
            EditorState.previewSetEffectVec2Param(EditorState.selectedTrack, EditorState.selectedClip,
                                                  effectIndex, paramData.key, x, y)
        }
    }

    function commitTyped(x, y) {
        if (animated) {
            EditorState.setClipVec2Keyframe(EditorState.selectedTrack, EditorState.selectedClip,
                                            paramData.prop, EditorState.playheadSeconds, x, y)
        } else {
            EditorState.setEffectVec2Param(EditorState.selectedTrack, EditorState.selectedClip,
                                           effectIndex, paramData.key, x, y)
        }
    }

    spacing: 4

    Row {
        width: parent.width
        spacing: 6
        ChannelKeyButton {
            anchors.verticalCenter: parent.verticalCenter
            keyframeList: root.keys
            label: root.paramData.label
            onAddRequested: EditorState.setClipVec2Keyframe(
                                EditorState.selectedTrack, EditorState.selectedClip, root.paramData.prop,
                                EditorState.playheadSeconds, root.curX, root.curY)
            onRemoveRequested: EditorState.removeClipChannelKeyframes(
                                   EditorState.selectedTrack, EditorState.selectedClip, root.paramData.prop,
                                   EditorState.playheadSeconds, [".x", ".y"])
        }
        Text {
            text: root.paramData.label
            color: Theme.c.vec2Param.foregroundMuted
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeXs
            anchors.verticalCenter: parent.verticalCenter
        }
    }

    readonly property bool isWheel: paramData.widget === "colorWheel"

    Vec2ParamPad {
        host: root
        visible: !root.isWheel
    }

    ColorWheelPad {
        id: wheel
        visible: root.isWheel
        width: Math.min(parent.width * 0.6, 140)
        height: width
        anchors.horizontalCenter: parent.horizontalCenter
        offset: Qt.point(root.curX, root.curY)
        neutralColor: Theme.c.colorWheel.discNeutral
        rimStrength: Theme.c.colorWheel.rimStrength
        ringColor: Theme.c.colorWheel.ring
        ringActiveColor: Theme.c.colorWheel.ringActive
        focusRingColor: Theme.focusRing
        ringWidth: Theme.c.colorWheel.ringWidth
        crosshairColor: Theme.c.colorWheel.crosshair
        crosshairOpacity: Theme.c.colorWheel.crosshairOpacity
        crosshairWidth: Theme.c.vec2ParamPad.crosshairWidth
        puckColor: Theme.c.colorWheel.puck
        puckBorderColor: Theme.c.colorWheel.puckBorder
        puckBorderWidth: Theme.c.colorWheel.puckBorderWidth
        puckSize: Theme.touchInput ? 18 : 12
        vectorColor: Theme.c.colorWheel.vector
        vectorOpacity: Theme.c.colorWheel.vectorOpacity
        onBegan: EditorState.beginPreviewDrag(qsTr("Edit %1").arg(root.paramData.label))
        onOffsetMoved: (x, y) => root.preview(root.clampAxis(x), root.clampAxis(y))
        onEnded: {
            EditorState.commitPreviewDrag()
            root.liveX = NaN
            root.liveY = NaN
        }
        onResetRequested: {
            EditorState.beginPreviewDrag(qsTr("Reset %1").arg(root.paramData.label))
            root.preview(root.clampAxis(0), root.clampAxis(0))
            EditorState.commitPreviewDrag()
            root.liveX = NaN
            root.liveY = NaN
        }

        HoverHandler {
            id: wheelHover
        }
    }

    ThemedToolTip {
        parent: wheel
        visible: root.isWheel && wheelHover.hovered && !wheel.pressed
        text: qsTr("Drag to tint. Shift for fine control. Double-click to reset the color.")
    }

    Row {
        width: parent.width
        spacing: 8
        ThemedNumberField {
            width: (parent.width - 8) / 2
            from: root.lo
            to: root.hi
            step: root.paramData.step > 0 ? root.paramData.step : 0.01
            decimals: 3
            value: root.curX
            onEdited: v => root.commitTyped(root.clampAxis(v), root.curY)
        }
        ThemedNumberField {
            width: (parent.width - 8) / 2
            from: root.lo
            to: root.hi
            step: root.paramData.step > 0 ? root.paramData.step : 0.01
            decimals: 3
            value: root.curY
            onEdited: v => root.commitTyped(root.curX, root.clampAxis(v))
        }
    }
}
