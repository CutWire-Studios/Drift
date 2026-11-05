import QtQuick
import Drift

// Fade dot at a clip corner. `outgoing` is fade-out; otherwise fade-in.
Rectangle {
    id: handle

    required property var owner
    property bool outgoing: false

    readonly property bool areaContainsMouse: fadeMouse.containsMouse
    readonly property bool areaPressed: fadeMouse.pressed

    width: owner.fadeHandleSize
    height: owner.fadeHandleSize
    radius: owner.fadeHandleSize / 2
    y: 2
    z: 40
    visible: owner.timelineFadeHandles && owner.selected
             && owner.width > owner.fadeHandleMinWidth
    color: Theme.c.timelineFadeHandle.background
    border.color: Theme.c.timelineFadeHandle.border
    border.width: Theme.c.timelineFadeHandle.borderWidth

    Binding {
        target: handle
        property: "x"
        when: !fadeMouse.pressed
        value: {
            const inset = owner.fadeHandleInset
            const maxX = owner.width - inset - handle.width
            const seconds = outgoing ? (owner.clipData.fadeOut || 0) : (owner.clipData.fadeIn || 0)
            const raw = outgoing
                    ? owner.width - seconds * owner.panel.pxPerSecond - handle.width / 2
                    : seconds * owner.panel.pxPerSecond - handle.width / 2
            return Math.max(inset, Math.min(maxX, raw))
        }
    }

    MouseArea {
        id: fadeMouse
        anchors.fill: parent
        anchors.leftMargin: -6
        anchors.rightMargin: -6
        anchors.topMargin: -6
        anchors.bottomMargin: owner.height < 35 ? 4 : -6
        z: 1
        preventStealing: true
        hoverEnabled: true
        cursorShape: Qt.SizeHorCursor
        onPressed: (mouse) => {
            Qt.callLater(function() { owner.forceActiveFocus() })
            mouse.accepted = true
            EditorState.beginPreviewDrag(qsTr("Adjust fade"))
        }
        onPositionChanged: (mouse) => {
            if (!pressed)
                return
            const px = Math.max(0, Math.min(owner.width,
                                            mapToItem(owner, mouse.x, mouse.y).x))
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
        onReleased: EditorState.commitPreviewDrag()
        onCanceled: EditorState.cancelPreviewDrag()

        HoverHandler { cursorShape: Qt.SizeHorCursor }

        Loader {
            anchors.fill: parent
            active: owner.tooltipsActive
            sourceComponent: Component {
                ThemedToolTip {
                    visible: fadeMouse.pressed || fadeMouse.containsMouse
                    text: handle.outgoing
                          ? qsTr("Fade out %1s").arg((owner.clipData.fadeOut || 0).toFixed(2))
                          : qsTr("Fade in %1s").arg((owner.clipData.fadeIn || 0).toFixed(2))
                }
            }
        }
    }
}
