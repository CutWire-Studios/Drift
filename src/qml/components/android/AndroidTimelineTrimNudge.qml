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
    height: Theme.androidControlHeight
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
                { label: qsTr("In"), side: -1, name: qsTr("Trim start") },
                { label: qsTr("Out"), side: 1, name: qsTr("Trim end") }
            ]

            delegate: Row {
                id: group
                required property var modelData
                required property int index
                spacing: 0

                Rectangle {
                    visible: group.index > 0
                    anchors.verticalCenter: parent.verticalCenter
                    width: Theme.c.androidTimelineTrimNudge.borderWidth
                    height: nudge.height - Theme.spacingMd * 2
                    color: Theme.c.androidTimelineTrimNudge.border
                }

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    leftPadding: group.index > 0 ? Theme.spacingSm : 0
                    rightPadding: Theme.spacingXs
                    text: group.modelData.label
                    color: Theme.c.androidTimelineTrimNudge.foregroundMuted
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeXs
                    font.weight: Font.Medium
                }

                Repeater {
                    model: [-1, 1]

                    delegate: Rectangle {
                        id: button
                        required property int modelData
                        width: Theme.androidChipHeight + Theme.spacingMd
                        height: nudge.height - nudge.border.width * 2
                        radius: nudge.radius
                        color: tap.pressed ? Theme.c.androidTimelineTrimNudge.pressed : "transparent"

                        Accessible.role: Accessible.Button
                        Accessible.name: button.modelData < 0
                            ? qsTr("%1 one frame earlier").arg(group.modelData.name)
                            : qsTr("%1 one frame later").arg(group.modelData.name)

                        IconGlyph {
                            anchors.centerIn: parent
                            glyph: button.modelData < 0 ? Theme.icons.chevronLeft : Theme.icons.chevronRight
                            iconSize: Theme.iconSizeMd
                            iconColor: Theme.c.androidTimelineTrimNudge.foreground
                        }

                        MouseArea {
                            id: tap
                            anchors.fill: parent
                            anchors.topMargin: -Math.max(0, (Theme.androidMinTouchTarget - button.height) / 2)
                            anchors.bottomMargin: anchors.topMargin
                            onClicked: nudge.step(group.modelData.side, button.modelData)
                        }
                    }
                }
            }
        }
    }
}
