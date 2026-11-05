import QtQuick
import QtQuick.Shapes
import Drift

// With a clip inside a transform layer selected: the layer's frame, dashed, and a
// finger-sized chip that selects it.
Item {
    required property var overlay
    id: parentFrame
    anchors.fill: parent
    z: 880
    visible: overlay.parentFrameBox !== null && !overlay.interacting
    readonly property var quad: overlay.parentFrameBox ? (overlay.parentFrameBox.quad || []) : []
    readonly property real qsx: overlay.stage.width / Math.max(1, overlay.parentFrameBox ? overlay.parentFrameBox.canvasWidth : 1)
    readonly property real qsy: overlay.stage.height / Math.max(1, overlay.parentFrameBox ? overlay.parentFrameBox.canvasHeight : 1)
    function corner(i) {
        const q = parentFrame.quad[i]
        return q ? Qt.point(q.x * qsx, q.y * qsy) : Qt.point(0, 0)
    }

    Shape {
        anchors.fill: parent
        visible: parentFrame.quad.length === 4
        preferredRendererType: Shape.CurveRenderer
        ShapePath {
            strokeColor: Theme.c.androidTransformParentFrame.stroke
            strokeWidth: Theme.c.androidTransformParentFrame.strokeWidth
            strokeStyle: ShapePath.DashLine
            dashPattern: [4, 3]
            fillColor: "transparent"
            startX: parentFrame.corner(0).x
            startY: parentFrame.corner(0).y
            PathLine { x: parentFrame.corner(1).x; y: parentFrame.corner(1).y }
            PathLine { x: parentFrame.corner(2).x; y: parentFrame.corner(2).y }
            PathLine { x: parentFrame.corner(3).x; y: parentFrame.corner(3).y }
            PathLine { x: parentFrame.corner(0).x; y: parentFrame.corner(0).y }
        }
    }

    Rectangle {
        visible: parentFrame.quad.length === 4
        x: Math.max(0, Math.min(overlay.stage.width - width, parentFrame.corner(0).x))
        y: Math.max(0, parentFrame.corner(0).y - height - Theme.spacingSm)
        width: androidParentChipLabel.implicitWidth + Theme.spacingXl * 2
        height: 48
        radius: height / 2
        color: parentChipTap.pressed ? Qt.lighter(Theme.c.androidTransformParentFrame.background, 1.15) : Theme.c.androidTransformParentFrame.background

        Text {
            id: androidParentChipLabel
            anchors.centerIn: parent
            text: qsTr("Select %1").arg(overlay.parentFrameBox ? (overlay.parentFrameBox.name || qsTr("Transform")) : "")
            color: Theme.c.androidTransformParentFrame.foreground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeSm
        }

        TapHandler {
            id: parentChipTap
            onTapped: {
                Haptics.select()
                EditorState.selectTransformParent()
            }
        }
    }
}
