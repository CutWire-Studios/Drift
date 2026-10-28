pragma ComponentBehavior: Bound
import QtQuick
import Drift

Item {
    id: root

    required property var panel

    readonly property var timeView: panel.timeView
    readonly property var span: {
        void panel.selection.revision
        return panel.selection.span()
    }
    readonly property bool active: !!span && span.end - span.start > 1e-4
    readonly property real spanLeft: active ? timeView.xFor(span.start) : 0
    readonly property real spanRight: active ? timeView.xFor(span.end) : 0

    property var pressRefs: []
    property real pivot: 0
    property real edgeSeconds: 0
    property bool dragging: false

    function begin(mouse, fromRight) {
        pressRefs = panel.selection.refs()
        edgeSeconds = fromRight ? span.end : span.start
        pivot = (mouse.modifiers & Qt.AltModifier) ? panel.playhead : (fromRight ? span.start : span.end)
        EditorState.beginPreviewDrag(qsTr("Stretch keyframes"))
        dragging = true
    }

    function update(x) {
        const denom = edgeSeconds - pivot
        if (Math.abs(denom) < 1e-4)
            return
        const target = panel.snapKeyTime(Math.max(0, timeView.secondsFor(x)), pressRefs, false)
        const factor = Math.max(0.01, (target - pivot) / denom)
        const res = EditorState.keyframes.previewStretchKeys(panel.track, panel.clip, pressRefs, pivot, factor)
        if (res && res.length > 0)
            panel.selection.replace(res)
    }

    function finish() {
        if (!dragging)
            return
        dragging = false
        EditorState.commitPreviewDrag()
    }

    function cancel() {
        if (!dragging)
            return
        dragging = false
        EditorState.cancelPreviewDrag()
        panel.selection.replace(pressRefs)
    }

    visible: active

    Connections {
        target: root.panel

        function onTrackChanged() { root.cancel() }
        function onClipChanged() { root.cancel() }
    }

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
                anchors.margins: -Theme.spacingXs
                cursorShape: Qt.SizeHorCursor
                preventStealing: true
                onPressed: (mouse) => root.begin(mouse, grip.modelData)
                onPositionChanged: (mouse) => {
                    if (root.dragging)
                        root.update(mapToItem(root, mouse.x, 0).x)
                }
                onReleased: root.finish()
                onCanceled: root.cancel()
            }
        }
    }
}
