import QtQuick
import QtQuick.Controls.Basic
import Drift
import ".."

// Touch canvas crop tool. Lives outside the (clipped) canvas rect so the crop
// frame can be dragged past the current edges to grow the output. Values are
// kept in project pixels; committing hands the rect to AppController, which
// rebases clip layout so nothing moves or rescales — content outside the new
// frame is simply lost.
//
// Navigation is not owned here: the viewport's pinch gesture zooms and pans
// underneath, so this overlay only claims the grips.
Item {
    id: root

    // Owning AndroidPreview viewport (zoom/pan reset) and the canvas rectangle
    // whose on-screen geometry the crop frame maps against.
    property var previewViewport
    property var previewCanvas

    readonly property int projW: { void EditorState.tracks; return Math.max(1, EditorState.projectWidth()) }
    readonly property int projH: { void EditorState.tracks; return Math.max(1, EditorState.projectHeight()) }
    // Project px → viewport px.
    readonly property real pxScale: root.previewCanvas.width / projW

    property real cropX: 0
    property real cropY: 0
    property real cropW: projW
    property real cropH: projH

    readonly property int outW: Math.round(cropW)
    readonly property int outH: Math.round(cropH)

    readonly property real gripSize: 14
    readonly property real gripTouch: 44

    property bool didResize: false

    readonly property bool changed: outW !== projW || outH !== projH
                                    || Math.round(cropX) !== 0 || Math.round(cropY) !== 0

    function reset() {
        cropX = 0
        cropY = 0
        cropW = projW
        cropH = projH
    }

    function apply() {
        if (changed)
            EditorState.applyCanvasCrop(cropX, cropY, cropW, cropH)
        EditorState.canvasCropMode = false
    }

    // Screen-space crop frame, relative to the viewport.
    readonly property real frameX: root.previewCanvas.x + cropX * pxScale
    readonly property real frameY: root.previewCanvas.y + cropY * pxScale
    readonly property real frameW: cropW * pxScale
    readonly property real frameH: cropH * pxScale

    onVisibleChanged: {
        root.previewViewport.resetView()
        if (visible) {
            reset()
            didResize = false
        }
    }

    // Everything outside the crop frame is discarded, so dim it. Four bands
    // rather than a mask: no shader, no clipping cost.
    Rectangle {
        color: Theme.overlayColor
        opacity: 0.65
        x: 0
        y: 0
        width: Math.max(0, root.frameX)
        height: root.height
    }
    Rectangle {
        color: Theme.overlayColor
        opacity: 0.65
        x: root.frameX + root.frameW
        y: 0
        width: Math.max(0, root.width - x)
        height: root.height
    }
    Rectangle {
        color: Theme.overlayColor
        opacity: 0.65
        x: root.frameX
        y: 0
        width: Math.max(0, root.frameW)
        height: Math.max(0, root.frameY)
    }
    Rectangle {
        color: Theme.overlayColor
        opacity: 0.65
        x: root.frameX
        y: root.frameY + root.frameH
        width: Math.max(0, root.frameW)
        height: Math.max(0, root.height - y)
    }

    Rectangle {
        x: root.frameX
        y: root.frameY
        width: root.frameW
        height: root.frameH
        color: "transparent"
        border.width: Theme.borderWidthFocus
        border.color: Theme.primary

        // Rule-of-thirds inside the crop frame, the usual cue for judging a reframe.
        Repeater {
            model: 2
            Rectangle {
                width: 1
                height: parent.height
                x: parent.width * (index + 1) / 3
                color: Theme.guideWeak
            }
        }
        Repeater {
            model: 2
            Rectangle {
                height: 1
                width: parent.width
                y: parent.height * (index + 1) / 3
                color: Theme.guideWeak
            }
        }
    }

    // Eight drag handles: 4 edges then 4 corners. `dx`/`dy` say which edges each
    // handle moves (-1 = left/top, +1 = right/bottom).
    Repeater {
        model: [
            { dx: -1, dy:  0 },
            { dx:  1, dy:  0 },
            { dx:  0, dy: -1 },
            { dx:  0, dy:  1 },
            { dx: -1, dy: -1 },
            { dx:  1, dy:  1 },
            { dx:  1, dy: -1 },
            { dx: -1, dy:  1 }
        ]

        delegate: Item {
            id: grip
            required property var modelData

            // The touch target is the whole delegate; the dot inside it stays
            // small enough not to hide the frame it sits on.
            width: root.gripTouch
            height: root.gripTouch

            x: root.frameX + (modelData.dx === 0 ? root.frameW / 2
                                                 : (modelData.dx < 0 ? 0 : root.frameW)) - width / 2
            y: root.frameY + (modelData.dy === 0 ? root.frameH / 2
                                                 : (modelData.dy < 0 ? 0 : root.frameH)) - height / 2

            Rectangle {
                anchors.centerIn: parent
                width: root.gripSize
                height: root.gripSize
                radius: Theme.radiusXs
                color: gripArea.pressed ? Theme.primaryForeground : Theme.primary
                border.width: Theme.borderWidth
                border.color: gripArea.pressed ? Theme.primary : Theme.primaryForeground

                Behavior on color {
                    ColorAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
                }
            }

            property real startX: 0
            property real startY: 0
            property real startCropX: 0
            property real startCropY: 0
            property real startCropW: 0
            property real startCropH: 0

            MouseArea {
                id: gripArea
                anchors.fill: parent
                preventStealing: true

                onPressed: (mouse) => {
                    const p = mapToItem(root, mouse.x, mouse.y)
                    grip.startX = p.x
                    grip.startY = p.y
                    grip.startCropX = root.cropX
                    grip.startCropY = root.cropY
                    grip.startCropW = root.cropW
                    grip.startCropH = root.cropH
                }

                onPositionChanged: (mouse) => {
                    if (!pressed)
                        return
                    const p = mapToItem(root, mouse.x, mouse.y)
                    const ddx = (p.x - grip.startX) / root.pxScale
                    const ddy = (p.y - grip.startY) / root.pxScale
                    root.didResize = true

                    if (grip.modelData.dx < 0) {
                        // Dragging the left edge moves the origin and shrinks the
                        // width by the same amount.
                        const nx = Math.min(grip.startCropX + ddx,
                                            grip.startCropX + grip.startCropW - 16)
                        root.cropX = nx
                        root.cropW = grip.startCropX + grip.startCropW - nx
                    } else if (grip.modelData.dx > 0) {
                        root.cropW = Math.max(16, grip.startCropW + ddx)
                    }

                    if (grip.modelData.dy < 0) {
                        const ny = Math.min(grip.startCropY + ddy,
                                            grip.startCropY + grip.startCropH - 16)
                        root.cropY = ny
                        root.cropH = grip.startCropY + grip.startCropH - ny
                    } else if (grip.modelData.dy > 0) {
                        root.cropH = Math.max(16, grip.startCropH + ddy)
                    }
                }
            }
        }
    }

    // First-run hint. The crop frame is the only thing on screen that can be
    // dragged, and nothing else says so; it retires once the user has dragged.
    Rectangle {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        anchors.topMargin: Theme.spacingLg
        width: hintLabel.width + Theme.spacing2xl
        height: hintLabel.height + Theme.spacingLg
        radius: Theme.radiusSm
        // Sits on the preview scrim, not a panel surface, so it uses the on-media
        // tokens — panelForeground would go dark-on-dark in light mode.
        color: Theme.scrimStrong
        border.width: Theme.borderWidth
        border.color: Theme.guideWeak

        opacity: root.didResize ? 0 : 1
        visible: opacity > 0

        Behavior on opacity {
            NumberAnimation { duration: Theme.durationSlow; easing.type: Theme.easing }
        }

        Text {
            id: hintLabel
            anchors.centerIn: parent
            text: qsTr("Drag the edges to reframe · pinch to zoom")
            color: Theme.onMedia
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeXs
        }
    }

    // Readout + commit. Pinned to the bottom of the viewport rather than to the
    // frame: at a small crop the frame-following row landed under a fingertip.
    Row {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: Theme.spacingMd
        spacing: Theme.spacingMd

        Rectangle {
            anchors.verticalCenter: parent.verticalCenter
            width: sizeLabel.width + Theme.spacingLg
            height: sizeLabel.height + Theme.spacingSm
            radius: Theme.radiusSm
            color: Theme.scrimStrong
            border.width: Theme.borderWidth
            border.color: Theme.guideWeak

            Text {
                id: sizeLabel
                anchors.centerIn: parent
                text: root.outW + "×" + root.outH
                color: Theme.onMedia
                font.family: Theme.monoFontFamily
                font.pixelSize: Theme.fontSizeXs
            }
        }

        ThemedButton {
            variant: "secondary"
            text: qsTr("Reset")
            enabled: root.changed
            onClicked: root.reset()
        }

        ThemedButton {
            variant: "secondary"
            text: qsTr("Cancel")
            onClicked: EditorState.canvasCropMode = false
        }

        ThemedButton {
            variant: "primary"
            text: qsTr("Apply")
            onClicked: root.apply()
        }
    }
}
