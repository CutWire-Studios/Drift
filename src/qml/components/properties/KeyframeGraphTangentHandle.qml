import QtQuick
import Drift

Item {
    property var host
    id: root
    required property var modelData

    readonly property var entry: host.series[host.focusedIndex]
    readonly property real keyX: host.xForSeconds(modelData.point.seconds)
    readonly property real keyY: host.yForValue(modelData.point.value, entry)
    readonly property real tipX:
        host.xForSeconds(modelData.point.seconds
                         + host.handleSeconds(modelData.point, modelData.outgoing))
    readonly property real tipY:
        host.yForValue(host.handleValue(modelData.point, modelData.outgoing), entry)

    anchors.fill: parent

    property real dragDx: 0
    property real dragDy: 0

    // Only the hit area: KeyframeCurvesItem draws the handle from the host's drag state.
    Item {
        id: grip
        width: 12
        height: 12
        x: root.tipX - width / 2 + root.dragDx
        y: root.tipY - height / 2 + root.dragDy

        HoverHandler { cursorShape: Qt.PointingHandCursor }

        DragHandler {
            target: null
            onActiveChanged: {
                if (active) {
                    // Freeze the model exactly as a key drag does, so the
                    // Repeater cannot destroy this delegate mid-grab and
                    // so the value axis stops auto-fitting under the cursor.
                    const p = root.modelData.point
                    host.frozenSeries = host.series
                    host.dragProp = host.series[host.focusedIndex].prop
                    host.dragTangentIndex = root.modelData.pointIndex
                    host.dragTangentInDx = p.inDx || 0
                    host.dragTangentInDy = p.inDy || 0
                    host.dragTangentOutDx = p.outDx || 0
                    host.dragTangentOutDy = p.outDy || 0
                    host.draggingKey = true
                    EditorState.beginPreviewDrag(qsTr("Edit keyframe curve"))
                } else {
                    // Same ordering as the key drag: unfreezing destroys
                    // this delegate, so reset before it.
                    host.dragProp = ""
                    host.dragTangentIndex = -1
                    root.dragDx = 0
                    root.dragDy = 0
                    EditorState.commitPreviewDrag()
                    host.draggingKey = false
                }
            }
            onTranslationChanged: {
                if (!active)
                    return
                const p = root.modelData.point
                const outgoing = root.modelData.outgoing

                const isShift = (EditorState.keyboardModifiers() & Qt.ShiftModifier) !== 0
                const newX = root.tipX + translation.x
                const newY = isShift ? root.keyY : (root.tipY + translation.y)
                // dx keeps its sign: an out-handle may not reach back
                // past its key, nor an in-handle forward past its own.
                let dx = host.secondsForX(newX - root.keyX)
                dx = outgoing ? Math.max(0, dx) : Math.min(0, dx)
                const dy = isShift ? 0.0 : (host.valueForY(newY, root.entry) - p.value)

                const inDx = outgoing ? (p.inDx || 0) : dx
                const outDx = outgoing ? dx : (p.outDx || 0)
                let inDy = outgoing ? (p.inDy || 0) : dy
                let outDy = outgoing ? dy : (p.outDy || 0)

                // Unless the key is a corner, the far tangent takes the
                // same slope so the curve stays smooth through the key.
                // Its dx has the opposite sign, so its dy flips with it.
                if (!p.corner && Math.abs(dx) > 1e-6) {
                    const slope = dy / dx
                    if (outgoing)
                        inDy = slope * inDx
                    else
                        outDy = slope * outDx
                }

                EditorState.previewSetKeyframeTangents(
                    EditorState.selectedTrack, EditorState.selectedClip,
                    host.series[host.focusedIndex].prop,
                    p.seconds, inDx, inDy, outDx, outDy, p.corner)

                host.dragTangentInDx = inDx
                host.dragTangentInDy = inDy
                host.dragTangentOutDx = outDx
                host.dragTangentOutDy = outDy
                root.dragDx = newX - root.tipX
                root.dragDy = isShift ? (root.keyY - root.tipY) : (newY - root.tipY)
            }
        }
    }
}
