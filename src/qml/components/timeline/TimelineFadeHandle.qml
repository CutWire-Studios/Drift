import QtQuick
import QtQuick.Shapes
import Drift

// Fade wedge standing on the clip's top edge, shaped like the ramp it sets. Its vertical edge marks
// where the fade ends. `outgoing` is fade-out; otherwise fade-in.
Item {
    id: handle

    required property var owner
    property bool outgoing: false

    readonly property bool areaContainsMouse: fadeMouse.containsMouse
    readonly property bool areaPressed: fadeMouse.pressed
    // Right-angle corner on the fade's end edge, slope down to the clip's outer edge.
    readonly property real edgeX: outgoing ? 0.75 : width - 0.75
    readonly property real farX: outgoing ? width - 0.75 : 0.75

    width: owner.fadeHandleWidth
    height: owner.fadeHandleHeight
    y: -height
    z: 40
    visible: owner.timelineFadeHandles && owner.selected
             && owner.width >= Theme.clipControlsMinWidth
    scale: areaContainsMouse || areaPressed ? 1.15 : 1
    transformOrigin: outgoing ? Item.BottomLeft : Item.BottomRight

    Behavior on scale {
        NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
    }

    Shape {
        anchors.fill: parent
        preferredRendererType: Shape.CurveRenderer

        ShapePath {
            strokeWidth: Theme.c.timelineFadeHandle.borderWidth + 3
            strokeColor: Theme.c.timelineFadeHandle.shadow
            fillColor: Theme.c.timelineFadeHandle.shadow
            joinStyle: ShapePath.RoundJoin
            startX: handle.edgeX; startY: 0.75
            PathLine { x: handle.edgeX; y: handle.height - 0.75 }
            PathLine { x: handle.farX; y: handle.height - 0.75 }
            PathLine { x: handle.edgeX; y: 0.75 }
        }
        ShapePath {
            strokeWidth: Theme.c.timelineFadeHandle.borderWidth
            strokeColor: Theme.c.timelineFadeHandle.border
            fillColor: Theme.c.timelineFadeHandle.background
            joinStyle: ShapePath.RoundJoin
            startX: handle.edgeX; startY: 0.75
            PathLine { x: handle.edgeX; y: handle.height - 0.75 }
            PathLine { x: handle.farX; y: handle.height - 0.75 }
            PathLine { x: handle.edgeX; y: 0.75 }
        }
    }

    Rectangle {
        visible: fadeMouse.pressed
        x: handle.outgoing ? -width - 8 : handle.width + 8
        anchors.verticalCenter: parent.verticalCenter
        width: fadeLabel.implicitWidth + 10
        height: fadeLabel.implicitHeight + 4
        radius: height / 2
        color: Theme.c.timelineFadeHandle.labelBackground

        Text {
            id: fadeLabel
            anchors.centerIn: parent
            text: qsTr("%1s").arg(((handle.outgoing ? owner.clipData.fadeOut
                                                    : owner.clipData.fadeIn) || 0).toFixed(2))
            color: Theme.c.timelineFadeHandle.labelForeground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeTiny
            font.weight: Font.DemiBold
        }
    }

    Binding {
        target: handle
        property: "x"
        when: !fadeMouse.pressed
        value: {
            const inset = owner.fadeHandleInset
            const maxX = owner.width - inset - handle.width
            const seconds = outgoing ? (owner.clipData.fadeOut || 0) : (owner.clipData.fadeIn || 0)
            const raw = outgoing
                    ? owner.width - seconds * owner.panel.pxPerSecond
                    : seconds * owner.panel.pxPerSecond - handle.width
            return Math.max(inset, Math.min(maxX, raw))
        }
    }

    MouseArea {
        id: fadeMouse
        anchors.fill: parent
        anchors.leftMargin: -6
        anchors.rightMargin: -6
        anchors.topMargin: -6
        anchors.bottomMargin: 0
        z: 1
        preventStealing: true
        hoverEnabled: true
        onPressed: (mouse) => {
            Qt.callLater(function() { owner.forceActiveFocus() })
            mouse.accepted = true
            pointerSceneX = mapToItem(null, mouse.x, mouse.y).x
            EditorState.beginPreviewDrag(qsTr("Adjust fade"))
        }
        property real pointerSceneX: 0

        function applyFade() {
            const local = mapFromItem(null, pointerSceneX, 0)
            const px = Math.max(0, Math.min(owner.width, mapToItem(owner, local.x, 0).x))
            handle.x = Math.max(owner.fadeHandleInset,
                                Math.min(owner.width - owner.fadeHandleInset - handle.width,
                                         handle.outgoing ? px : px - handle.width))
            if (handle.outgoing) {
                EditorState.previewSetClipFade(owner.trackIndex, owner.clipIndex,
                                               owner.clipData.fadeIn || 0,
                                               Math.max(0, (owner.width - px) / owner.panel.pxPerSecond))
            } else {
                EditorState.previewSetClipFade(owner.trackIndex, owner.clipIndex,
                                               px / owner.panel.pxPerSecond,
                                               owner.clipData.fadeOut || 0)
            }
        }

        // A fade on a clip wider than the pane could otherwise only be dragged as far as is on screen.
        Timer {
            interval: 16
            repeat: true
            running: fadeMouse.pressed && typeof owner.panel.dragEdgeScroll === "function"
            onTriggered: {
                const panel = owner.panel
                const px = fadeMouse.mapToItem(owner.parent, fadeMouse.mapFromItem(null, fadeMouse.pointerSceneX, 0).x, 0).x
                         - panel.timelineViewX
                const edgeX = Math.min(48, panel.timelineViewW * 0.25)
                let dx = 0
                if (px < edgeX)
                    dx = -Math.min(24, 6 + (edgeX - px) * 0.4)
                else if (px > panel.timelineViewW - edgeX)
                    dx = Math.min(24, 6 + (px - (panel.timelineViewW - edgeX)) * 0.4)
                if (dx !== 0 && panel.dragEdgeScroll(dx, 0).x !== 0)
                    fadeMouse.applyFade()
            }
        }

        onPositionChanged: (mouse) => {
            if (!pressed)
                return
            pointerSceneX = mapToItem(null, mouse.x, mouse.y).x
            applyFade()
        }
        onReleased: EditorState.commitPreviewDrag()
        onCanceled: EditorState.cancelPreviewDrag()

        TimelineCursorRequest {
            kind: handle.outgoing ? "fadeOut" : "fadeIn"
            active: fadeMouse.containsMouse || fadeMouse.pressed
        }

        Loader {
            anchors.fill: parent
            active: owner.tooltipsActive
            sourceComponent: Component {
                ThemedToolTip {
                    visible: fadeMouse.containsMouse && !fadeMouse.pressed
                    text: handle.outgoing
                          ? qsTr("Fade out %1s").arg((owner.clipData.fadeOut || 0).toFixed(2))
                          : qsTr("Fade in %1s").arg((owner.clipData.fadeIn || 0).toFixed(2))
                }
            }
        }
    }
}
