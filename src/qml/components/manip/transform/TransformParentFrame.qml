import QtQuick
import QtQuick.Shapes
import Drift

// With a clip inside a transform layer selected: the layer's frame, dashed, and a chip that
// selects it (Shift+G does the same). Alt+click is not used: Alt drags the window on Linux.
Item {
    id: parentFrame

    required property var manip

    anchors.fill: parent
    z: 880
    visible: manip.parentFrameBox !== null && !manip.interacting
    readonly property var quad: manip.parentFrameBox ? (manip.parentFrameBox.quad || []) : []
    readonly property real qsx: manip.stage.width / Math.max(1, manip.parentFrameBox ? manip.parentFrameBox.canvasWidth : 1)
    readonly property real qsy: manip.stage.height / Math.max(1, manip.parentFrameBox ? manip.parentFrameBox.canvasHeight : 1)
    function corner(i) {
        const q = parentFrame.quad[i]
        return q ? Qt.point(q.x * qsx, q.y * qsy) : Qt.point(0, 0)
    }

    Shape {
        anchors.fill: parent
        visible: parentFrame.quad.length === 4
        preferredRendererType: Shape.CurveRenderer

        ShapePath {
            strokeColor: Theme.c.transformOverlayParentOutline.border
            strokeWidth: Theme.c.transformOverlayParentOutline.borderWidth
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
        id: chip
        visible: parentFrame.quad.length === 4
        x: Math.max(0, Math.min(parentFrame.manip.stage.width - width, parentFrame.corner(0).x))
        y: Math.max(0, parentFrame.corner(0).y - height - Theme.spacingSm)
        width: chipLabel.implicitWidth + (parentFrame.manip.touch ? Theme.spacingXl * 2 : Theme.spacingLg)
        height: parentFrame.manip.touch ? Theme.androidMinTouchTarget : chipLabel.implicitHeight + Theme.spacingSm
        radius: Theme.radiusPill
        color: chipMouse.containsMouse || chipMouse.pressed
               ? Theme.c.transformOverlayParentChip.backgroundHover
               : Theme.c.transformOverlayParentChip.background

        Text {
            id: chipLabel
            anchors.centerIn: parent
            text: qsTr("Select %1").arg(parentFrame.manip.parentFrameBox
                                           ? (parentFrame.manip.parentFrameBox.name || qsTr("Transform")) : "")
            color: Theme.c.transformOverlayParentChip.foreground
            font.family: Theme.fontFamily
            font.pixelSize: parentFrame.manip.touch ? Theme.fontSizeSm : Theme.fontSizeXs
        }

        MouseArea {
            id: chipMouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: {
                if (parentFrame.manip.touch)
                    Haptics.select()
                EditorState.selectTransformParent()
            }
        }

        ThemedToolTip {
            visible: chipMouse.containsMouse && !parentFrame.manip.touch
            text: qsTr("Select the transform layer moving this clip (%1)")
                      .arg(EditorState.shortcutFor("selectTransformLayer"))
        }
    }
}
