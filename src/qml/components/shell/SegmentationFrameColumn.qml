import QtQuick
import Drift

// ----- Frame + prompt overlay ------------------------------------------------------
Column {
    required property var tool
    property real sidebarWidth
    property alias frameSlider: frameSlider
    width: parent.width - sidebarWidth - Theme.spacingLg
    height: parent.height
    spacing: Theme.spacingMd

    Item {
        id: stage
        width: parent.width
        height: parent.height - (scrubRow.visible ? scrubRow.height + Theme.spacingMd : 0)

        readonly property real frameW: EditorState.segmentation.frameSize.width
        readonly property real frameH: EditorState.segmentation.frameSize.height
        readonly property real aspect: frameH > 0 ? frameW / frameH : 16 / 9

        // Letterboxed fit, and the single source of truth for mapping clicks back into
        // normalized frame coordinates.
        readonly property real fitW: Math.min(width, height * aspect)
        readonly property real fitH: fitW / aspect
        readonly property real fitX: (width - fitW) / 2
        readonly property real fitY: (height - fitH) / 2

        Rectangle {
            anchors.fill: parent
            color: Theme.c.segmentationFrameColumn.background
            radius: Theme.c.segmentationFrameColumn.radius
        }

        Image {
            id: frameImage
            x: stage.fitX
            y: stage.fitY
            width: stage.fitW
            height: stage.fitH
            fillMode: Image.Stretch
            cache: false
            // The revision defeats QML's URL-keyed image cache; the pixels behind these
            // URLs change on every prompt edit.
            source: EditorState.segmentation.sessionActive
                    ? "image://segment/frame?rev=" + EditorState.segmentation.revision
                    : ""
        }

        Image {
            id: maskImage
            x: stage.fitX
            y: stage.fitY
            width: stage.fitW
            height: stage.fitH
            fillMode: Image.Stretch
            cache: false
            opacity: 0.45
            source: EditorState.segmentation.sessionActive
                    ? "image://segment/mask?rev=" + EditorState.segmentation.revision
                    : ""
        }

        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.LeftButton | Qt.RightButton
            enabled: EditorState.segmentation.sessionActive && EditorState.segmentation.backendUsesPoints
                     && !EditorState.segmentation.encoding && !EditorState.segmentation.running
            pressAndHoldInterval: 400
            property bool heldExclude: false
            property bool pressedByTouch: false
            onPressed: (mouse) => {
                heldExclude = false
                pressedByTouch = mouse.source !== Qt.MouseEventNotSynthesized
            }
            // Touch has no right button: hold to mark what to exclude.
            onPressAndHold: function (mouse) {
                if (!pressedByTouch || !(mouse.buttons & Qt.LeftButton))
                    return
                const nx = (mouse.x - stage.fitX) / stage.fitW
                const ny = (mouse.y - stage.fitY) / stage.fitH
                if (nx < 0 || nx > 1 || ny < 0 || ny > 1)
                    return
                heldExclude = true
                EditorState.segmentation.addPoint(nx, ny, false)
            }
            onClicked: function (mouse) {
                if (heldExclude)
                    return
                const nx = (mouse.x - stage.fitX) / stage.fitW
                const ny = (mouse.y - stage.fitY) / stage.fitH
                if (nx < 0 || nx > 1 || ny < 0 || ny > 1)
                    return
                EditorState.segmentation.addPoint(nx, ny, mouse.button === Qt.LeftButton)
            }
        }

        // Prompt markers. Green includes the subject, red carves it out; click one to drop it.
        Repeater {
            model: EditorState.segmentation.backendUsesPoints ? EditorState.segmentation.points : []
            delegate: Rectangle {
                required property int index
                required property var modelData

                width: 14
                height: 14
                radius: Theme.c.segmentationFrameColumn.markerRadius
                x: stage.fitX + modelData.x * stage.fitW - 7
                y: stage.fitY + modelData.y * stage.fitH - 7
                color: modelData.include ? Theme.c.segmentationFrameColumn.markerInclude
                                         : Theme.c.segmentationFrameColumn.markerExclude
                border.width: Theme.c.segmentationFrameColumn.markerBorderWidth
                border.color: Theme.c.segmentationFrameColumn.markerBorder

                MouseArea {
                    anchors.fill: parent
                    anchors.margins: Theme.touchInput ? -9 : 0
                    enabled: !EditorState.segmentation.running
                    onClicked: EditorState.segmentation.removePoint(parent.index)
                }
            }
        }

        Rectangle {
            anchors.centerIn: parent
            visible: EditorState.segmentation.encoding
            width: encodingLabel.width + Theme.spacingXl
            height: encodingLabel.height + Theme.spacingLg
            radius: Theme.c.segmentationFrameColumn.radius
            color: Theme.c.segmentationFrameColumn.scrim

            ThemedLabel {
                id: encodingLabel
                anchors.centerIn: parent
                text: qsTr("Looking at this moment…")
            }
        }
    }

    Row {
        id: scrubRow
        visible: !EditorState.segmentation.sessionStill
        width: parent.width
        spacing: Theme.spacingMd

        ThemedLabel {
            anchors.verticalCenter: parent.verticalCenter
            text: qsTr("Frame")
        }

        ThemedSlider {
            id: frameSlider
            label: qsTr("Frame")
            width: parent.width - 220
            anchors.verticalCenter: parent.verticalCenter
            from: 0
            to: Math.max(0.001, tool.clipDurationSeconds)
            enabled: !EditorState.segmentation.encoding && !EditorState.segmentation.running
            // Re-encoding on every slider tick would queue seconds of work per drag, so a
            // drag only shows frames and the encode waits for the release.
            onMoved: {
                if (pressed)
                    EditorState.segmentation.scrubFrame(tool.clipStartSeconds + value)
            }
            onPressedChanged: {
                if (!pressed)
                    EditorState.segmentation.setFrame(tool.clipStartSeconds + value)
            }
        }

        ThemedLabel {
            anchors.verticalCenter: parent.verticalCenter
            text: frameSlider.value.toFixed(2) + qsTr("s")
        }
    }
}
