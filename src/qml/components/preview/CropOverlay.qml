import QtQuick
import QtQuick.Controls.Basic
import Drift
import ".."

// Canvas crop tool. Lives outside the (clipped) canvas rect so the
// crop frame can be dragged past the current edges to grow the
// output. Values are kept in project pixels; committing hands the
// rect to AppController, which rebases clip layout so nothing
// moves or rescales — content outside the new frame is simply lost.
Item {
    id: root

    // Owning PreviewPanel viewport (zoom/pan math + reset) and the canvas
    // rectangle whose on-screen geometry the crop frame maps against.
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

    // Hint bookkeeping. Set as each input is first used; once all
    // three are known the hint retires and does not come back, so
    // reopening the tool is not nagging.
    property bool didResize: false
    property bool didZoom: false
    property bool didPan: false
    property bool hintDismissed: false
    onDidResizeChanged: maybeDismissHint()
    onDidZoomChanged: maybeDismissHint()
    onDidPanChanged: maybeDismissHint()

    function maybeDismissHint() {
        if (didResize && didZoom && didPan)
            hintDismissed = true
    }
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
        if (visible)
            reset()
    }

    // Navigation sits below the handles in stacking order, and takes
    // only the middle button, so left-drags still reach the grips.
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.MiddleButton
        cursorShape: pressed ? Qt.ClosedHandCursor : Qt.ArrowCursor

        property real lastX: 0
        property real lastY: 0

        onPressed: (mouse) => {
            lastX = mouse.x
            lastY = mouse.y
        }
        onPositionChanged: (mouse) => {
            if (!pressed)
                return
            root.previewViewport.panX += mouse.x - lastX
            root.previewViewport.panY += mouse.y - lastY
            lastX = mouse.x
            lastY = mouse.y
            root.didPan = true
        }

        // Zoom lives here rather than in a sibling WheelHandler: a
        // MouseArea accepts wheel events even with no onWheel bound,
        // so a separate handler behind it never got them. Ctrl-less
        // scrolls are explicitly rejected so they keep propagating.
        onWheel: (wheel) => {
            if (!Theme.primaryModifierPressed(wheel.modifiers)
                    || wheel.angleDelta.y === 0) {
                wheel.accepted = false
                return
            }
            root.previewViewport.zoomAt(wheel.x, wheel.y,
                            wheel.angleDelta.y > 0 ? 1.15 : 1 / 1.15)
            root.didZoom = true
            wheel.accepted = true
        }
    }

    // Everything outside the crop frame is discarded, so dim it.
    // Four bands rather than a mask: no shader, no clipping cost.
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

        // Rule-of-thirds inside the crop frame, the usual cue for
        // judging a reframe.
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

    // Eight drag handles: 4 edges then 4 corners. `dx`/`dy` say
    // which edges each handle moves (-1 = left/top, +1 = right/bottom).
    Repeater {
        model: [
            { dx: -1, dy:  0, cursor: Qt.SizeHorCursor },
            { dx:  1, dy:  0, cursor: Qt.SizeHorCursor },
            { dx:  0, dy: -1, cursor: Qt.SizeVerCursor },
            { dx:  0, dy:  1, cursor: Qt.SizeVerCursor },
            { dx: -1, dy: -1, cursor: Qt.SizeFDiagCursor },
            { dx:  1, dy:  1, cursor: Qt.SizeFDiagCursor },
            { dx:  1, dy: -1, cursor: Qt.SizeBDiagCursor },
            { dx: -1, dy:  1, cursor: Qt.SizeBDiagCursor }
        ]

        delegate: Rectangle {
            id: grip
            required property var modelData

            readonly property real hs: Theme.spacingLg
            width: hs
            height: hs
            radius: Theme.radiusXs
            color: gripArea.containsMouse || gripArea.pressed
                   ? Theme.primaryForeground : Theme.primary
            border.width: Theme.borderWidth
            border.color: gripArea.containsMouse || gripArea.pressed
                          ? Theme.primary : Theme.primaryForeground

            Behavior on color {
                ColorAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
            }

            x: root.frameX + (modelData.dx === 0 ? root.frameW / 2
                                     : (modelData.dx < 0 ? 0 : root.frameW)) - hs / 2
            y: root.frameY + (modelData.dy === 0 ? root.frameH / 2
                                     : (modelData.dy < 0 ? 0 : root.frameH)) - hs / 2

            property real startX: 0
            property real startY: 0
            property real startCropX: 0
            property real startCropY: 0
            property real startCropW: 0
            property real startCropH: 0

            MouseArea {
                id: gripArea
                anchors.fill: parent
                // Generous invisible margin: the visible dot stays
                // small enough not to hide the frame it sits on.
                anchors.margins: -Theme.spacingMd
                hoverEnabled: true
                cursorShape: grip.modelData.cursor

                // Zooming onto a vertex means the cursor is over a
                // grip; without this the grip would eat the wheel
                // event exactly where zoom is most wanted.
                onWheel: (wheel) => { wheel.accepted = false }

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
                        // Dragging the left edge moves the origin and
                        // shrinks the width by the same amount.
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

    // First-run hint. The crop tool has three non-obvious inputs and
    // no menu surface to discover them from, so they are spelled out
    // while the tool is open. Fades once the user has driven all
    // three, and stays gone for the rest of the session.
    Rectangle {
        id: cropHint
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        anchors.topMargin: Theme.spacingLg
        width: hintRow.width + Theme.spacing2xl
        height: hintRow.height + Theme.spacingLg
        radius: Theme.radiusSm
        // Sits on the preview scrim, not a panel surface, so it uses
        // the on-media tokens — panelForeground would go dark-on-dark
        // in light mode.
        color: Theme.scrimStrong
        border.width: Theme.borderWidth
        border.color: Theme.guideWeak

        opacity: root.hintDismissed ? 0 : 1
        visible: opacity > 0

        Behavior on opacity {
            NumberAnimation { duration: Theme.durationSlow; easing.type: Theme.easing }
        }

        Row {
            id: hintRow
            anchors.centerIn: parent
            spacing: Theme.spacingLg

            Text {
                text: qsTr("Drag the edges to reframe")
                color: root.didResize ? Theme.guideMedium : Theme.onMedia
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
            }
            Text {
                text: "·"
                color: Theme.guideWeak
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
            }
            Text {
                text: Theme.platformShortcutText(qsTr("Ctrl + scroll to zoom"))
                color: root.didZoom ? Theme.guideMedium : Theme.onMedia
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
            }
            Text {
                text: "·"
                color: Theme.guideWeak
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
            }
            Text {
                text: qsTr("Middle-drag to pan")
                color: root.didPan ? Theme.guideMedium : Theme.onMedia
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
            }
        }
    }

    // Readout + commit, pinned under the crop frame but kept inside
    // the viewport so it stays reachable at any crop size.
    Row {
        spacing: Theme.spacingMd
        x: Math.max(0, Math.min(root.width - width,
                                root.frameX + (root.frameW - width) / 2))
        y: Math.max(0, Math.min(root.height - height,
                                root.frameY + root.frameH + Theme.spacingMd))

        Rectangle {
            anchors.verticalCenter: parent.verticalCenter
            width: sizeLabel.width + Theme.spacingLg
            height: sizeLabel.height + Theme.spacingSm
            radius: Theme.radiusSm
            // On-media surface, same as the hint: fixed dark scrim
            // with an on-media foreground rather than panel tokens.
            color: Theme.scrimStrong
            border.width: Theme.borderWidth
            border.color: Theme.guideWeak

            Text {
                id: sizeLabel
                anchors.centerIn: parent
                text: root.outW + "×" + root.outH
                      + "   " + Math.round(root.previewViewport.userZoom * 100) + "%"
                color: Theme.onMedia
                font.family: Theme.monoFontFamily
                font.pixelSize: Theme.fontSizeXs
            }
        }

        ThemedButton {
            variant: "secondary"
            text: qsTr("Fit view")
            tooltip: qsTr("Recentre and reset zoom")
            enabled: root.previewViewport.userZoom !== 1.0 || root.previewViewport.panX !== 0 || root.previewViewport.panY !== 0
            onClicked: root.previewViewport.resetView()
        }

        ThemedButton {
            variant: "secondary"
            text: qsTr("Reset")
            tooltip: qsTr("Reset crop to the full video size")
            enabled: root.changed
            onClicked: root.reset()
        }

        ThemedButton {
            variant: "primary"
            text: qsTr("Apply")
            onClicked: root.apply()
        }
    }
}
