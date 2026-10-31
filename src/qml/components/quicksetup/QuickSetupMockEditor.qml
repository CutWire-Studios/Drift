import QtQuick
import Drift

Rectangle {
    id: root

    implicitWidth: 480
    implicitHeight: 300
    radius: Theme.c.quickSetupMockEditor.radius
    color: Theme.c.quickSetupMockEditor.window
    border.width: Theme.c.quickSetupMockEditor.borderWidth
    border.color: Theme.c.quickSetupMockEditor.border
    clip: true

    readonly property real headerHeight: height * 0.1
    readonly property real timelineHeight: height * 0.38
    readonly property real gap: width * 0.012
    readonly property real stageX: gap * 2 + width * 0.27
    readonly property real stageY: headerHeight + gap
    readonly property real stageWidth: width - stageX - gap
    readonly property real stageHeight: height - headerHeight - timelineHeight - gap * 2
    readonly property real timelineY: height - timelineHeight - gap
    readonly property var trackClips: [
        [{ x: 0.02, w: 0.30, c: "clipVideo" }, { x: 0.34, w: 0.22, c: "clipVideo" }, { x: 0.58, w: 0.36, c: "clipVideo" }],
        [{ x: 0.12, w: 0.20, c: "clipText" }, { x: 0.50, w: 0.26, c: "clipEffect" }],
        [{ x: 0.02, w: 0.92, c: "clipAudio" }]
    ]

    Rectangle {
        id: header

        width: parent.width
        height: root.headerHeight
        color: Theme.c.quickSetupMockEditor.panel

        Text {
            anchors.left: parent.left
            anchors.leftMargin: root.gap * 2
            anchors.verticalCenter: parent.verticalCenter
            text: qsTr("Drift")
            color: Theme.c.quickSetupMockEditor.text
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeTiny
            font.weight: Font.DemiBold
        }

        Rectangle {
            anchors.right: parent.right
            anchors.rightMargin: root.gap * 2
            anchors.verticalCenter: parent.verticalCenter
            width: exportLabel.implicitWidth + root.gap * 3
            height: parent.height * 0.62
            radius: Theme.c.quickSetupMockEditor.radius
            color: Theme.c.quickSetupMockEditor.primary

            Text {
                id: exportLabel

                anchors.centerIn: parent
                text: qsTr("Export")
                color: Theme.c.quickSetupMockEditor.primaryForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeTiny
                font.weight: Font.Medium
            }
        }

        Rectangle {
            anchors.bottom: parent.bottom
            width: parent.width
            height: Theme.c.quickSetupMockEditor.borderWidth
            color: Theme.c.quickSetupMockEditor.border
        }
    }

    Rectangle {
        id: bin

        x: root.gap
        y: header.height + root.gap
        width: root.width * 0.27
        height: root.height - header.height - root.timelineHeight - root.gap * 2
        radius: Theme.c.quickSetupMockEditor.radius
        color: Theme.c.quickSetupMockEditor.panel
        border.width: Theme.c.quickSetupMockEditor.borderWidth
        border.color: Theme.c.quickSetupMockEditor.border

        Grid {
            anchors.fill: parent
            anchors.margins: root.gap
            columns: 3
            spacing: root.gap

            Repeater {
                model: 9

                Rectangle {
                    width: (bin.width - root.gap * 4) / 3
                    height: width * 0.7
                    radius: Theme.c.quickSetupMockEditor.radius / 2
                    color: Theme.c.quickSetupMockEditor.tile
                }
            }
        }
    }

    Rectangle {
        id: stage

        x: bin.x + bin.width + root.gap
        y: bin.y
        width: root.width - x - root.gap
        height: bin.height
        radius: Theme.c.quickSetupMockEditor.radius
        color: Theme.c.quickSetupMockEditor.panel
        border.width: Theme.c.quickSetupMockEditor.borderWidth
        border.color: Theme.c.quickSetupMockEditor.border

        Rectangle {
            id: frame

            anchors.horizontalCenter: parent.horizontalCenter
            y: root.gap
            height: parent.height - controls.height - root.gap * 3
            width: Math.min(height * 16 / 9, parent.width - root.gap * 2)
            radius: Theme.c.quickSetupMockEditor.radius / 2
            color: Theme.c.quickSetupMockEditor.tile

            IconGlyph {
                anchors.centerIn: parent
                glyph: Theme.icons.play
                iconSize: Math.min(parent.height * 0.4, 28)
                iconColor: Theme.c.quickSetupMockEditor.textMuted
            }
        }

        Row {
            id: controls

            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom
            anchors.bottomMargin: root.gap
            height: 14
            spacing: root.gap * 2

            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                width: 26
                height: 12
                radius: height / 2
                color: Theme.c.quickSetupMockEditor.primary

                Rectangle {
                    x: parent.width - width - 2
                    anchors.verticalCenter: parent.verticalCenter
                    width: 8
                    height: 8
                    radius: 4
                    color: Theme.c.quickSetupMockEditor.primaryForeground
                }
            }

            Item {
                anchors.verticalCenter: parent.verticalCenter
                width: stage.width * 0.4
                height: 12

                Rectangle {
                    anchors.verticalCenter: parent.verticalCenter
                    width: parent.width
                    height: 4
                    radius: 2
                    color: Theme.c.quickSetupMockEditor.toggleTrack
                }

                Rectangle {
                    anchors.verticalCenter: parent.verticalCenter
                    width: parent.width * 0.6
                    height: 4
                    radius: 2
                    color: Theme.c.quickSetupMockEditor.primary
                }

                Rectangle {
                    x: parent.width * 0.6 - 5
                    anchors.verticalCenter: parent.verticalCenter
                    width: 10
                    height: 10
                    radius: 5
                    color: Theme.c.quickSetupMockEditor.primary
                }
            }
        }
    }

    Rectangle {
        id: timeline

        x: root.gap
        y: root.height - root.timelineHeight - root.gap
        width: root.width - root.gap * 2
        height: root.timelineHeight
        radius: Theme.c.quickSetupMockEditor.radius
        color: Theme.c.quickSetupMockEditor.panel
        border.width: Theme.c.quickSetupMockEditor.borderWidth
        border.color: Theme.c.quickSetupMockEditor.border
        clip: true

        Column {
            anchors.fill: parent
            anchors.margins: root.gap
            spacing: root.gap

            Repeater {
                model: root.trackClips

                Rectangle {
                    id: track

                    required property var modelData

                    width: parent.width
                    height: (timeline.height - root.gap * 5) / 3
                    radius: Theme.c.quickSetupMockEditor.radius / 2
                    color: Theme.c.quickSetupMockEditor.track

                    Repeater {
                        model: track.modelData

                        Rectangle {
                            required property var modelData

                            x: modelData.x * track.width
                            width: modelData.w * track.width
                            height: track.height
                            radius: Theme.c.quickSetupMockEditor.radius / 2
                            color: Theme.c.quickSetupMockEditor[modelData.c]
                        }
                    }
                }
            }
        }

        Rectangle {
            x: timeline.width * 0.42
            width: 2
            height: parent.height
            color: Theme.c.quickSetupMockEditor.playhead
        }
    }
}
