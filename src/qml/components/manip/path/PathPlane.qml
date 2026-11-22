import QtQuick
import Drift

// An invisible item laid out exactly like the edited clip's frame (position, size, spin, and the
// 3D pose or transform-layer parent when there is one), parented to the canvas rect. It exists so
// PathEditor can ask where the frame's corners land on screen; the editing maths is all in C++.
Item {
    id: plane

    // PreviewController::trackerFrameOf / maskEditorState / maskingFrame data.
    property var frame: ({})
    property real sx: 1
    property real sy: 1

    readonly property real canvasW: Math.max(1, frame.canvasWidth || 1)
    readonly property real canvasH: Math.max(1, frame.canvasHeight || 1)
    readonly property bool is3d: (frame.rotationX || 0) !== 0 || (frame.rotationY || 0) !== 0
                                 || (frame.z || 0) !== 0 || frame.anchorX !== undefined || frame.anchorY !== undefined
    readonly property bool hasParent: frame.parentActive === true

    visible: false
    x: (frame.x || 0) * sx
    y: (frame.y || 0) * sy
    width: Math.max(1, (frame.width || canvasW) * sx)
    height: Math.max(1, (frame.height || canvasH) * sy)
    transformOrigin: Item.Center
    rotation: is3d || hasParent ? 0 : (frame.rotation || 0)
    transform: Matrix4x4 {
        matrix: plane.is3d || plane.hasParent
                ? EditorState.preview.clipPoseMatrix(plane.frame, plane.frame.x || 0, plane.frame.y || 0,
                                                    plane.frame.width || plane.canvasW,
                                                    plane.frame.height || plane.canvasH,
                                                    plane.frame.rotation || 0, plane.sx, plane.sy)
                : Qt.matrix4x4()
    }

    // The frame's (0,0), (1,0), (1,1) and (0,1) corners in `target`'s coordinates.
    function corners(target) {
        return [plane.mapToItem(target, 0, 0), plane.mapToItem(target, width, 0),
                plane.mapToItem(target, width, height), plane.mapToItem(target, 0, height)]
    }
}
