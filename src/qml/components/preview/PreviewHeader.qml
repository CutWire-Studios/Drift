import QtQuick
import Drift
import ".."

// The strip above the preview: the 2D / 3D switch, and in 3D the view's own tools. 2D shows the
// scene camera's picture, which is what exports; 3D shows the world from a free viewpoint with the
// camera drawn in it, which is where the camera and depth are arranged.
Item {
    id: header

    readonly property bool mode3d: EditorState.previewMode === "3d"

    function withShortcut(label, key) {
        return key.length > 0 ? qsTr("%1 (%2)").arg(label).arg(key) : label
    }

    height: Theme.controlHeightSm + Theme.spacingMd * 2

    Rectangle {
        anchors.bottom: parent.bottom
        width: parent.width
        height: Theme.borderWidth
        color: Theme.panelBorder
    }

    // What the 3D view is looking through.
    Text {
        visible: header.mode3d
        anchors.left: parent.left
        anchors.leftMargin: Theme.spacing2xl
        anchors.verticalCenter: parent.verticalCenter
        text: EditorState.editorLookThrough ? qsTr("Looking through the camera") : qsTr("Free view")
        color: Theme.mutedForeground
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSizeXs
    }

    // A pill switch: the chosen mode is a raised pill that slides between the two labels.
    Rectangle {
        id: modeSwitch

        readonly property var modes: [
            { value: "2d", label: qsTr("Camera Output"),
              tip: qsTr("The camera's picture, as it exports. Clips move and snap on the canvas.") },
            { value: "3d", label: qsTr("3D Scene"),
              tip: qsTr("The scene from a free viewpoint, with the camera in it. Middle- or right-drag orbits, Shift pans, the wheel zooms.") }
        ]
        readonly property int current: header.mode3d ? 1 : 0
        readonly property real inset: 2
        readonly property real segmentWidth: Math.max(labelProbe0.implicitWidth, labelProbe1.implicitWidth)
                                             + Theme.spacing2xl * 2

        anchors.centerIn: parent
        width: segmentWidth * modes.length + inset * 2
        height: Theme.controlHeightSm
        radius: height / 2
        // The same tokens as the app's other segmented toggles (ThemedToggleButton): the
        // track is an unchecked control, the pill a checked one.
        color: Theme.panelAccent
        border.width: Theme.borderWidth
        border.color: Theme.panelBorder

        // Sized off the widest label, so both segments match.
        Text { id: labelProbe0; visible: false; text: modeSwitch.modes[0].label; font.family: Theme.fontFamily; font.pixelSize: Theme.fontSizeSm; font.weight: Font.DemiBold }
        Text { id: labelProbe1; visible: false; text: modeSwitch.modes[1].label; font.family: Theme.fontFamily; font.pixelSize: Theme.fontSizeSm; font.weight: Font.DemiBold }

        Rectangle {
            x: modeSwitch.inset + modeSwitch.current * modeSwitch.segmentWidth
            y: modeSwitch.inset
            width: modeSwitch.segmentWidth
            height: parent.height - modeSwitch.inset * 2
            radius: height / 2
            color: Theme.panelSecondaryBg
            border.width: Theme.borderWidth
            border.color: Theme.panelSecondaryBorder

            Behavior on x {
                NumberAnimation { duration: Theme.durationBase; easing.type: Theme.easing }
            }
        }

        Row {
            x: modeSwitch.inset
            anchors.verticalCenter: parent.verticalCenter

            Repeater {
                model: modeSwitch.modes
                delegate: Item {
                    id: segment
                    required property var modelData
                    required property int index
                    readonly property bool selected: index === modeSwitch.current
                    width: modeSwitch.segmentWidth
                    height: modeSwitch.height

                    Accessible.role: Accessible.RadioButton
                    Accessible.name: modelData.label
                    Accessible.checked: selected

                    Text {
                        anchors.centerIn: parent
                        text: segment.modelData.label
                        color: segment.selected ? Theme.panelSecondaryForeground
                                                : segmentArea.containsMouse ? Theme.panelForeground
                                                                            : Theme.mutedForeground
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSizeSm
                        font.weight: Font.DemiBold

                        Behavior on color {
                            ColorAnimation { duration: Theme.durationBase; easing.type: Theme.easing }
                        }
                    }

                    MouseArea {
                        id: segmentArea
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: EditorState.previewMode = segment.modelData.value
                    }

                    ThemedToolTip {
                        text: segment.modelData.tip
                        visible: segmentArea.containsMouse
                    }
                }
            }
        }
    }

    Row {
        visible: header.mode3d
        anchors.right: parent.right
        anchors.rightMargin: Theme.spacing2xl
        anchors.verticalCenter: parent.verticalCenter
        spacing: 0

        Repeater {
            model: [
                { value: "move", glyph: Theme.icons.move3d, label: qsTr("Move"), action: "gizmoMove" },
                { value: "rotate", glyph: Theme.icons.rotate3d, label: qsTr("Rotate"), action: "gizmoRotate" },
                { value: "scale", glyph: Theme.icons.scale3d, label: qsTr("Scale"), action: "gizmoScale" }
            ]
            delegate: IconButton {
                required property var modelData
                anchors.verticalCenter: parent.verticalCenter
                glyph: modelData.glyph
                variant: "text"
                tooltip: header.withShortcut(modelData.label, EditorState.shortcutFor(modelData.action))
                Accessible.name: modelData.label
                active: EditorState.gizmoTool === modelData.value
                onClicked: EditorState.gizmoTool = modelData.value
            }
        }

        IconButton {
            anchors.verticalCenter: parent.verticalCenter
            glyph: Theme.icons.locateFixed
            variant: "text"
            tooltip: EditorState.gizmoOrientation === "local"
                     ? qsTr("Gizmo follows the selection's own axes (click for world axes)")
                     : qsTr("Gizmo follows the world axes (click for the selection's own)")
            active: EditorState.gizmoOrientation === "local"
            onClicked: EditorState.gizmoOrientation = EditorState.gizmoOrientation === "local" ? "global" : "local"
        }

        Rectangle {
            width: Theme.borderWidth
            height: Theme.iconSizeBase
            color: Theme.panelBorder
            anchors.verticalCenter: parent.verticalCenter
        }

        IconButton {
            anchors.verticalCenter: parent.verticalCenter
            glyph: Theme.icons.video
            variant: "text"
            tooltip: header.withShortcut(qsTr("Look through the camera"), qsTr("Numpad 0"))
            active: EditorState.editorLookThrough
            onClicked: EditorState.editorToggleLookThrough()
        }
        IconButton {
            anchors.verticalCenter: parent.verticalCenter
            glyph: Theme.icons.refresh
            variant: "text"
            tooltip: qsTr("Reset the scene camera's position")
            enabled: {
                void EditorState.tracksRevision
                void EditorState.playheadSeconds
                return EditorState.previewCameraActive()
            }
            onClicked: EditorState.resetSceneCamera()
        }
        IconButton {
            anchors.verticalCenter: parent.verticalCenter
            glyph: Theme.icons.zoomFit
            variant: "text"
            tooltip: header.withShortcut(qsTr("Frame the selection"), "F")
            onClicked: EditorState.editorFrameSelection()
        }
        IconButton {
            anchors.verticalCenter: parent.verticalCenter
            glyph: Theme.icons.reset
            variant: "text"
            tooltip: header.withShortcut(qsTr("Reset the view"), "Home")
            onClicked: EditorState.editorResetView()
        }
    }
}
