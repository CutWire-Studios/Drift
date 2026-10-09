import QtQuick
import Drift
import ".."

// The 2D / 3D switch: an icon pill that slides between the two. 2D shows the scene camera's
// picture, which is what exports; 3D shows the world from a free viewpoint with the camera drawn
// in it, which is where the camera and depth are arranged.
Rectangle {
    id: modeSwitch

    readonly property var modes: [
        { value: "2d", glyph: Theme.icons.monitor, label: qsTr("Camera Output"),
          tip: qsTr("The camera's picture, as it exports. Clips move and snap on the canvas.") },
        { value: "3d", glyph: Theme.icons.box, label: qsTr("3D Scene"),
          tip: qsTr("The scene from a free viewpoint, with the camera in it. Middle- or right-drag orbits, Shift pans, the wheel zooms, and the corner pad steps the view.") }
    ]
    readonly property int current: EditorState.preview.mode === "3d" ? 1 : 0
    readonly property real inset: 2
    readonly property real segmentWidth: Theme.iconButtonSize

    width: segmentWidth * modes.length + inset * 2
    height: Theme.iconButtonSize + inset * 2
    radius: height / 2
    // The same tokens as the app's other segmented toggles (ThemedToggleButton): the track is an
    // unchecked control, the pill a checked one.
    color: Theme.panelAccent
    border.width: Theme.borderWidth
    border.color: Theme.panelBorder

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
                height: Theme.iconButtonSize

                Accessible.role: Accessible.RadioButton
                Accessible.name: modelData.label
                Accessible.checked: selected

                IconGlyph {
                    anchors.centerIn: parent
                    glyph: segment.modelData.glyph
                    iconSize: Theme.iconSizeBase
                    iconColor: segment.selected ? Theme.panelSecondaryForeground
                                                : segmentArea.containsMouse ? Theme.panelForeground
                                                                            : Theme.mutedForeground

                    Behavior on iconColor {
                        ColorAnimation { duration: Theme.durationBase; easing.type: Theme.easing }
                    }
                }

                MouseArea {
                    id: segmentArea
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: EditorState.preview.mode = segment.modelData.value
                }

                ThemedToolTip {
                    text: segment.modelData.tip
                    visible: segmentArea.containsMouse
                }
            }
        }
    }
}
