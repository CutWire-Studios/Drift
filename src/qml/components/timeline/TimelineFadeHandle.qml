import QtQuick
import Drift

// Fade dot just inside the clip's top edge, centred on where the fade ends. `outgoing` is fade-out;
// otherwise fade-in.
Item {
    id: handle

    required property var owner
    property bool outgoing: false

    readonly property bool areaContainsMouse: fadeMouse.containsMouse
    readonly property bool areaPressed: fadeMouse.pressed
    width: owner.fadeHandleWidth
    height: owner.fadeHandleHeight
    // Inside the clip: above its top edge the first track sits under the ruler, which takes the press.
    y: 2
    z: 40
    visible: owner.timelineFadeHandles && owner.selected
             && owner.width >= Theme.clipControlsMinWidth
    scale: areaContainsMouse || areaPressed ? 1.15 : 1
    transformOrigin: Item.Top

    Behavior on scale {
        NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
    }

    Rectangle {
        anchors.fill: parent
        anchors.margins: -1.5
        radius: width / 2
        color: Theme.c.timelineFadeHandle.shadow
    }
    Rectangle {
        anchors.fill: parent
        radius: width / 2
        color: Theme.c.timelineFadeHandle.background
        border.width: Theme.c.timelineFadeHandle.borderWidth
        border.color: Theme.c.timelineFadeHandle.border
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
            const raw = (outgoing ? owner.width - seconds * owner.panel.pxPerSecond
                                  : seconds * owner.panel.pxPerSecond) - handle.width / 2
            return Math.max(inset, Math.min(maxX, raw))
        }
    }

    MouseArea {
        id: fadeMouse
        anchors.fill: parent
        anchors.leftMargin: -6
        anchors.rightMargin: -6
        anchors.topMargin: -2
        anchors.bottomMargin: -4
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
                                         px - handle.width / 2))
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
