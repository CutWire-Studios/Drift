import QtQuick
import Drift

// Focus reticle. With "Follow focus point" on, focus tracks whatever is under it
// on every frame; otherwise dropping it here sets the focus distance to the depth
// at this spot, once.
Item {
    id: reticle

    required property var source
    required property var frame
    required property var overlay

    visible: !reticle.source.isLight
    width: 34
    height: 34
    x: reticle.source.hx * reticle.frame.width - width / 2
    y: reticle.source.hy * reticle.frame.height - height / 2

    Rectangle {
        anchors.fill: parent
        radius: Theme.radiusPill
        color: "transparent"
        border.width: Theme.c.depthEffectFocusReticle.borderWidth
        border.color: focusArea.containsMouse || focusArea.pressed
                      ? Theme.c.depthEffectFocusReticle.borderHover : Theme.c.depthEffectFocusReticle.border
    }
    Rectangle {
        anchors.centerIn: parent
        width: Theme.c.depthEffectFocusReticle.strokeWidth
        height: parent.height * 0.5
        color: Theme.c.depthEffectFocusReticle.foreground
    }
    Rectangle {
        anchors.centerIn: parent
        width: parent.width * 0.5
        height: Theme.c.depthEffectFocusReticle.strokeWidth
        color: Theme.c.depthEffectFocusReticle.foreground
    }
    Text {
        anchors.top: parent.bottom
        anchors.topMargin: 2
        anchors.horizontalCenter: parent.horizontalCenter
        text: reticle.source.values.follow ? qsTr("Focus") : qsTr("Pick focus")
        color: Theme.c.depthEffectFocusReticle.foreground
        style: Text.Outline
        styleColor: Theme.c.depthEffectFocusReticle.shadow
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSizeXs
    }

    MouseArea {
        id: focusArea
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.CrossCursor
        preventStealing: true

        onPressed: {
            reticle.overlay.interacting = true
            EditorState.beginPreviewDrag(qsTr("Move focus"))
        }
        onPositionChanged: (mouse) => {
            if (!pressed)
                return
            const p = reticle.overlay.framePoint(focusArea, mouse)
            reticle.source.liveX = Math.max(0, Math.min(1, p.x))
            reticle.source.liveY = Math.max(0, Math.min(1, p.y))
            reticle.overlay.writeParam(reticle.source.modelData.effect, "focusX", reticle.source.liveX)
            reticle.overlay.writeParam(reticle.source.modelData.effect, "focusY", reticle.source.liveY)
            if (!reticle.source.values.follow) {
                const d = EditorState.depth.sampleAt(EditorState.selectedTrack,
                                                     EditorState.selectedClip,
                                                     reticle.source.liveX, reticle.source.liveY)
                if (d >= 0)
                    reticle.overlay.writeParam(reticle.source.modelData.effect, "focusDepth", d)
            }
        }
        onReleased: { reticle.source.clearLive(); reticle.overlay.endInteraction() }
        onCanceled: { reticle.source.clearLive(); reticle.overlay.endInteraction() }
    }
}
