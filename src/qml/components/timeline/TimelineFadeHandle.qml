import QtQuick
import Drift

// Fade dot just inside the clip's top edge, centred on where the fade ends. `outgoing` is fade-out;
// otherwise fade-in.
Item {
    id: handle

    required property var owner
    property bool outgoing: false

    readonly property bool areaContainsMouse: fadeMouse.hovered
    readonly property bool areaPressed: fadeMouse.active
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
        visible: fadeMouse.active
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
        when: !fadeMouse.active || fadeMouse.reverted
        value: {
            const inset = owner.fadeHandleInset
            const maxX = owner.width - inset - handle.width
            const seconds = outgoing ? (owner.clipData.fadeOut || 0) : (owner.clipData.fadeIn || 0)
            const raw = (outgoing ? owner.width - seconds * owner.panel.pxPerSecond
                                  : seconds * owner.panel.pxPerSecond) - handle.width / 2
            return Math.max(inset, Math.min(maxX, raw))
        }
    }

    HandleKnob {
        id: fadeMouse
        anchors.centerIn: parent
        shape: "none"
        cursor: Qt.ArrowCursor
        space: null
        // Set when Esc puts the fade back mid-drag: the handle then follows the project again.
        property bool reverted: false
        property real pointerSceneX: 0
        property real pointerSceneY: 0

        onPressed: (point, modifiers, device) => {
            Qt.callLater(function() { owner.forceActiveFocus() })
            reverted = false
            pointerSceneX = mapToItem(null, point.x, point.y).x
            pointerSceneY = mapToItem(null, point.x, point.y).y
            EditorState.drag.begin(handle.outgoing ? "fadeOut" : "fadeIn", device, qsTr("Adjust fade"))
        }

        function applyFade() {
            if (reverted)
                return
            const local = mapFromItem(null, pointerSceneX, 0)
            const px = Math.max(0, Math.min(owner.width, mapToItem(owner, local.x, 0).x))
            handle.x = Math.max(owner.fadeHandleInset,
                                Math.min(owner.width - owner.fadeHandleInset - handle.width,
                                         px - handle.width / 2))
            let fadeIn, fadeOut
            if (handle.outgoing) {
                fadeIn = owner.clipData.fadeIn || 0
                fadeOut = Math.max(0, (owner.width - px) / owner.panel.pxPerSecond)
            } else {
                fadeIn = px / owner.panel.pxPerSecond
                fadeOut = owner.clipData.fadeOut || 0
            }
            EditorState.previewSetClipFade(owner.trackIndex, owner.clipIndex, fadeIn, fadeOut)
            const length = handle.outgoing ? fadeOut : fadeIn
            EditorState.drag.update(qsTr("%1 s").arg(length.toFixed(2)))
            owner.panel.setDragPointer(pointerSceneX, pointerSceneY)
        }

        // A fade on a clip wider than the pane could otherwise only be dragged as far as is on screen.
        Timer {
            interval: 16
            repeat: true
            running: fadeMouse.active && typeof owner.panel.dragEdgeScroll === "function"
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

        onMoved: (point, modifiers, device) => {
            pointerSceneX = mapToItem(null, point.x, point.y).x
            pointerSceneY = mapToItem(null, point.x, point.y).y
            applyFade()
        }
        onReleased: {
            reverted = false
            EditorState.drag.commit()
        }


        Connections {
            target: EditorState.drag
            function onCancelled() {
                if (fadeMouse.active)
                    fadeMouse.reverted = true
            }
        }

        TimelineCursorRequest {
            kind: handle.outgoing ? "fadeOut" : "fadeIn"
            active: fadeMouse.hovered
        }

        Loader {
            anchors.fill: parent
            active: owner.tooltipsActive
            sourceComponent: Component {
                ThemedToolTip {
                    visible: fadeMouse.hovered && !fadeMouse.active
                    text: handle.outgoing
                          ? qsTr("Fade out %1s").arg((owner.clipData.fadeOut || 0).toFixed(2))
                          : qsTr("Fade in %1s").arg((owner.clipData.fadeIn || 0).toFixed(2))
                }
            }
        }
    }
}
