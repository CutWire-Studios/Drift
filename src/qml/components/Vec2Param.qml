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
            color: Theme.mutedForeground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeXs
            anchors.verticalCenter: parent.verticalCenter
        }
    }

    Rectangle {
        id: pad
        width: parent.width
        height: Math.min(parent.width * 0.6, 140)
        radius: Theme.radiusSm
        color: Theme.panelAccent
        border.width: 1
        border.color: dragArea.pressed ? Theme.primary : Theme.panelBorder

        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            width: 1
            height: parent.height
            color: Theme.panelBorder
            opacity: 0.5
        }
        Rectangle {
            anchors.verticalCenter: parent.verticalCenter
            height: 1
            width: parent.width
            color: Theme.panelBorder
            opacity: 0.5
        }
        Rectangle {
            width: 12
            height: 12
            radius: 6
            color: Theme.primary
            border.width: 2
            border.color: Theme.panelForeground
            readonly property real span: root.hi - root.lo
            x: (span > 0 ? (root.curX - root.lo) / span : 0) * pad.width - width / 2
            // Up is larger Y, matching the shader's bottom-left uv origin.
            y: (1 - (span > 0 ? (root.curY - root.lo) / span : 0)) * pad.height - height / 2
        }

        MouseArea {
            id: dragArea
            anchors.fill: parent
            cursorShape: Qt.CrossCursor
            // Stops a parent Flickable taking the drag over, as ThemedSlider does: only Flickables
            // that opt in with `dragLocks` and fold it into `interactive` can be locked.
            property var lockedFlickable: null
            function lockFlickable() {
                if (lockedFlickable)
                    return
                let node = pad.parent
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
            preventStealing: true
            Component.onDestruction: unlockFlickable()
            function update(mouse) {
                const fx = Math.min(1, Math.max(0, mouse.x / width))
                const fy = Math.min(1, Math.max(0, mouse.y / height))
                root.preview(root.lo + fx * (root.hi - root.lo),
                             root.lo + (1 - fy) * (root.hi - root.lo))
            }
            onPressed: mouse => {
                lockFlickable()
                EditorState.beginPreviewDrag(qsTr("Edit %1").arg(root.paramData.label))
                update(mouse)
            }
            onPositionChanged: mouse => { if (pressed) update(mouse) }
            onReleased: {
                unlockFlickable()
                EditorState.commitPreviewDrag()
                root.liveX = NaN
                root.liveY = NaN
            }
            onCanceled: {
                unlockFlickable()
                EditorState.commitPreviewDrag()
                root.liveX = NaN
                root.liveY = NaN
            }
        }
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
