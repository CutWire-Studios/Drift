import QtQuick
import QtQuick.Controls.Basic
import Drift

Column {
    id: root

    property int revision: 0
    readonly property int trackIndex: EditorState.selectedTrack
    readonly property int clipIndex: EditorState.selectedClip
    readonly property var shapeIds: ["original", "circle", "ellipse", "triangle", "square", "diamond",
                                     "pentagon", "hexagon", "octagon", "star", "heart"]
    readonly property var shapeLabels: [qsTr("Original"), qsTr("Circle"), qsTr("Ellipse"), qsTr("Triangle"),
                                        qsTr("Square"), qsTr("Diamond"), qsTr("Pentagon"), qsTr("Hexagon"),
                                        qsTr("Octagon"), qsTr("Star"), qsTr("Heart")]
    readonly property string shape: {
        void revision
        return EditorState.masking.clipShape(trackIndex, clipIndex)
    }
    readonly property var framing: {
        void revision
        return EditorState.masking.reframe(trackIndex, clipIndex)
    }

    spacing: Theme.spacingSm

    Connections {
        target: EditorState
        function onSelectionChanged() { root.revision++ }
        function onTracksChanged() { root.revision++ }
    }

    Text {
        text: qsTr("Shape")
        SearchAnchor {
            searchId: "transform-shape"
            title: qsTr("Shape")
            keywords: "crop circle star heart hexagon mask"
        }
        color: Theme.c.transformInspector.foregroundMuted
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSizeXs
        font.weight: Font.Medium
    }

    ThemedComboBox {
        width: parent.width
        model: root.shapeLabels
        currentIndex: Math.max(0, root.shapeIds.indexOf(root.shape))
        onActivated: index => EditorState.masking.setClipShape(root.trackIndex, root.clipIndex, root.shapeIds[index])
    }

    Column {
        width: parent.width
        spacing: Theme.spacingSm
        visible: root.shape !== "original"

        Text {
            text: qsTr("Content inside shape")
            color: Theme.c.transformInspector.foregroundMuted
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeXs
            font.weight: Font.Medium
        }

        Row {
            width: parent.width
            spacing: Theme.spacingSm

            IconButton {
                id: zoomOutButton
                anchors.verticalCenter: parent.verticalCenter
                buttonSize: 24
                iconSize: 16
                glyph: Theme.icons.zoomOut
                tooltip: qsTr("Zoom out")
                onClicked: EditorState.masking.setReframe(root.trackIndex, root.clipIndex,
                                                          root.framing.zoom / 1.1, NaN, NaN)
            }
            ThemedSlider {
                id: zoomSlider
                lockWhilePlaying: true
                label: qsTr("Zoom")
                width: parent.width - zoomOutButton.width - zoomInButton.width - parent.spacing * 2
                anchors.verticalCenter: parent.verticalCenter
                from: 1
                to: 8
                valueFormatter: function (v) { return Math.round(v * 100) + "%" }
                Binding on value {
                    when: !zoomSlider.pressed
                    value: root.framing.zoom
                }
                onPressedChanged: {
                    if (pressed)
                        EditorState.beginPreviewDrag(qsTr("Reframe clip"))
                    else
                        EditorState.commitPreviewDrag()
                }
                onMoved: EditorState.masking.previewReframe(root.trackIndex, root.clipIndex, value, NaN, NaN)
            }
            IconButton {
                id: zoomInButton
                anchors.verticalCenter: parent.verticalCenter
                buttonSize: 24
                iconSize: 16
                glyph: Theme.icons.zoomIn
                tooltip: qsTr("Zoom in")
                onClicked: EditorState.masking.setReframe(root.trackIndex, root.clipIndex,
                                                          root.framing.zoom * 1.1, NaN, NaN)
            }
        }

        Text {
            text: qsTr("Horizontal")
            color: Theme.c.transformInspector.foregroundMuted
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeXs
        }
        ThemedSlider {
            id: posXSlider
            lockWhilePlaying: true
            label: qsTr("Horizontal position")
            width: parent.width
            from: 0
            to: 1
            valueFormatter: function (v) { return Math.round(v * 100) + "%" }
            Binding on value {
                when: !posXSlider.pressed
                value: root.framing.x
            }
            onPressedChanged: {
                if (pressed)
                    EditorState.beginPreviewDrag(qsTr("Reframe clip"))
                else
                    EditorState.commitPreviewDrag()
            }
            onMoved: EditorState.masking.previewReframe(root.trackIndex, root.clipIndex,
                                                        root.framing.zoom, value, root.framing.y)
        }

        Text {
            text: qsTr("Vertical")
            color: Theme.c.transformInspector.foregroundMuted
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeXs
        }
        ThemedSlider {
            id: posYSlider
            lockWhilePlaying: true
            label: qsTr("Vertical position")
            width: parent.width
            from: 0
            to: 1
            valueFormatter: function (v) { return Math.round(v * 100) + "%" }
            Binding on value {
                when: !posYSlider.pressed
                value: root.framing.y
            }
            onPressedChanged: {
                if (pressed)
                    EditorState.beginPreviewDrag(qsTr("Reframe clip"))
                else
                    EditorState.commitPreviewDrag()
            }
            onMoved: EditorState.masking.previewReframe(root.trackIndex, root.clipIndex,
                                                        root.framing.zoom, root.framing.x, value)
        }

        Row {
            spacing: Theme.spacingSm
            IconButton {
                buttonSize: 28
                iconSize: Theme.iconSizeSm
                glyph: Theme.icons.chevronLeft
                tooltip: qsTr("Nudge content left")
                onClicked: EditorState.masking.nudgeReframe(root.trackIndex, root.clipIndex, -0.05, 0)
            }
            IconButton {
                buttonSize: 28
                iconSize: Theme.iconSizeSm
                glyph: Theme.icons.chevronUp
                tooltip: qsTr("Nudge content up")
                onClicked: EditorState.masking.nudgeReframe(root.trackIndex, root.clipIndex, 0, -0.05)
            }
            IconButton {
                buttonSize: 28
                iconSize: Theme.iconSizeSm
                glyph: Theme.icons.chevronDown
                tooltip: qsTr("Nudge content down")
                onClicked: EditorState.masking.nudgeReframe(root.trackIndex, root.clipIndex, 0, 0.05)
            }
            IconButton {
                buttonSize: 28
                iconSize: Theme.iconSizeSm
                glyph: Theme.icons.chevronRight
                tooltip: qsTr("Nudge content right")
                onClicked: EditorState.masking.nudgeReframe(root.trackIndex, root.clipIndex, 0.05, 0)
            }
        }

        Flow {
            width: parent.width
            spacing: Theme.spacingSm
            ThemedButton {
                text: qsTr("Center")
                onClicked: EditorState.masking.centerReframe(root.trackIndex, root.clipIndex)
            }
            ThemedButton {
                text: qsTr("Show whole video")
                onClicked: EditorState.masking.showWholeVideo(root.trackIndex, root.clipIndex)
            }
        }
    }
}
