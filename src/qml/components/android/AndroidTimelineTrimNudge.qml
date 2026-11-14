import QtQuick
import Drift

// One-frame trim buttons for the selected clip: a fingertip cannot place an edge to the frame, and
// each press is its own undo step. A viewport-fixed overlay, like the playhead.
Rectangle {
    id: nudge

    readonly property bool shown: {
        void EditorState.selectionRevision
        return EditorState.selectedTrack >= 0 && EditorState.selectedClip >= 0
               && EditorState.selection.length <= 1 && EditorState.selectedTransitionTrack < 0
    }

    visible: shown
    width: row.width + Theme.spacingSm * 2
    height: Theme.androidMinTouchTarget
    radius: Theme.c.androidTimelineTrimNudge.radius
    color: Theme.c.androidTimelineTrimNudge.background
    border.width: Theme.c.androidTimelineTrimNudge.borderWidth
    border.color: Theme.c.androidTimelineTrimNudge.border

    function step(side, frames) {
        const outcome = EditorState.nudgeTrimEdge(EditorState.selectedTrack, EditorState.selectedClip,
                                                  side, frames)
        if (outcome === 1 || outcome === 2)
            Haptics.press()
        else
            Haptics.boundary()
    }

    Row {
        id: row
        anchors.centerIn: parent
        spacing: Theme.spacingSm

        Repeater {
            model: [
                { label: qsTr("In"), side: -1 },
                { label: qsTr("Out"), side: 1 }
            ]

            delegate: Row {
                id: group
                required property var modelData
                spacing: 0

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    rightPadding: Theme.spacingXs
                    text: group.modelData.label
                    color: Theme.c.androidTimelineTrimNudge.foregroundMuted
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeTiny
                }

                Repeater {
                    model: [-1, 1]

                    delegate: Rectangle {
                        id: button
                        required property int modelData
                        width: Theme.androidMinTouchTarget
                        height: nudge.height - nudge.border.width * 2
                        radius: nudge.radius
                        color: tap.pressed ? Theme.c.androidTimelineTrimNudge.pressed : "transparent"

                        Text {
                            anchors.centerIn: parent
                            text: button.modelData < 0 ? "‹" : "›"
                            color: Theme.c.androidTimelineTrimNudge.foreground
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSizeBase * 1.6
                        }

                        MouseArea {
                            id: tap
                            anchors.fill: parent
                            onClicked: nudge.step(group.modelData.side, button.modelData)
                        }
                    }
                }
            }
        }
    }
}
