import QtQuick
import Drift

// One gizmo axis: back strokes, front strokes, and the head.
Item {
    id: handleItem

    required property var modelData
    required property var host

    readonly property bool hot: handleItem.host.dragging === modelData.id
                                || (handleItem.host.dragging === "" && handleItem.host.hovered === modelData.id)
    readonly property color ink: hot ? Theme.c.transformGizmoHandle.hot : handleItem.host.colorFor(modelData.id)
    readonly property real lineWidth: (hot ? Theme.c.transformGizmoHandle.strokeWidthHot : Theme.c.transformGizmoHandle.strokeWidth) * (handleItem.host.touch ? 1.25 : 1)
    // While one handle is dragged the others step back, so the one in use reads clearly.
    // An axis that lines up with the view direction fades out (and cannot be picked) as well.
    opacity: (handleItem.host.dragging === "" || hot ? 1 : Theme.c.transformGizmoHandle.opacity) * modelData.alpha

    // The far half of a ring, behind the clip.
    Repeater {
        model: handleItem.modelData.back
        delegate: TransformGizmoStroke {
            ink: handleItem.ink
            thickness: handleItem.lineWidth
            fade: Theme.c.transformGizmoHandle.backOpacity
        }
    }

    Repeater {
        model: handleItem.modelData.front
        delegate: TransformGizmoStroke {
            ink: handleItem.ink
            thickness: handleItem.lineWidth
            fade: Theme.c.transformGizmoHandle.frontOpacity
        }
    }

    TransformGizmoHead {
        headPath: handleItem.modelData.head
        ink: handleItem.ink
        show: handleItem.modelData.head.length > 0
    }
}
