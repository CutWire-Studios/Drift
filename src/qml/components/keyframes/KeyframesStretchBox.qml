pragma ComponentBehavior: Bound
import QtQuick
import Drift

Item {
    id: root

    required property var panel
    required property var interaction

    readonly property var timeView: panel.timeView
    readonly property var span: {
        void panel.selection.revision
        return panel.selection.span()
    }
    readonly property bool active: !!span && span.end - span.start > 1e-4
    readonly property real spanLeft: active ? timeView.xFor(span.start) : 0
    readonly property real spanRight: active ? timeView.xFor(span.end) : 0

    function begin(mouse, fromRight) {
        interaction.beginStretch(fromRight, mouse.modifiers, mouse.source === Qt.MouseEventNotSynthesized ? DragSession.Mouse : DragSession.Touch)
    }

    visible: active

    Rectangle {
        x: root.spanLeft
        width: Math.max(0, root.spanRight - root.spanLeft)
        height: parent.height
        color: Qt.rgba(Theme.c.keyframesStretchBox.background.r, Theme.c.keyframesStretchBox.background.g,
                       Theme.c.keyframesStretchBox.background.b, Theme.c.keyframesStretchBox.backgroundOpacity)
        border.width: Theme.c.keyframesStretchBox.borderWidth
        border.color: Theme.c.keyframesStretchBox.border
    }

    Repeater {
        model: [false, true]
        delegate: Rectangle {
            id: grip

            required property bool modelData

            x: modelData ? root.spanRight + Theme.spacingSm : root.spanLeft - width - Theme.spacingSm
            y: root.height - height - Theme.spacingXs
            width: Theme.c.keyframesStretchBox.gripWidth
            height: Theme.c.keyframesStretchBox.gripHeight
            radius: Theme.c.keyframesStretchBox.radius
            color: Theme.c.keyframesStretchBox.grip
            border.width: Theme.c.keyframesStretchBox.borderWidth
            border.color: Theme.c.keyframesStretchBox.gripBorder

            MouseArea {
                anchors.fill: parent
                anchors.margins: -Math.max(Theme.spacingXs, (Theme.androidMinTouchTarget - grip.width) / 2)
                cursorShape: Qt.SizeHorCursor
                preventStealing: true
                onPressed: (mouse) => root.begin(mouse, grip.modelData)
                onPositionChanged: (mouse) => {
                    if (pressed)
                        root.interaction.updateStretch(mapToItem(root, mouse.x, 0).x, mouse.modifiers)
                }
                onReleased: root.interaction.endStretch()
                onCanceled: root.interaction.cancel()
            }
        }
    }
}
