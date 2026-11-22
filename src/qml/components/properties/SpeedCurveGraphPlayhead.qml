import QtQuick
import Drift

Item {
    property Item plotItem: null
    id: root
    y: 0
    // Over the plot's click area, which would otherwise swallow the press
    // before the drag could start. Still under the knobs (z 4) and their
    // handles (z 5), so editing the curve always wins over scrubbing.
    z: 3
    width: Theme.playheadLineWidth
    height: plotItem.height

    Binding {
        target: root
        property: "x"
        value: EditorState.speedCurve.sourcePosition * plotItem.width
        when: !rootDrag.drag.active
    }

    // Scrub live rather than only on release, so the preview follows the drag.
    onXChanged: {
        if (rootDrag.drag.active)
            EditorState.speedCurve.seekAtSource(root.x / plotItem.width)
    }

    Rectangle {
        anchors.left: parent.left
        width: Theme.playheadLineWidth
        height: parent.height
        color: Theme.c.speedCurveGraphPlayhead.accent
    }

    Rectangle {
        width: Theme.playheadHandleSize
        height: Theme.playheadHandleSize
        radius: Theme.radiusPill
        x: -width / 2 + Theme.playheadLineWidth / 2
        color: Theme.c.speedCurveGraphPlayhead.accent
    }

    MouseArea {
        id: rootDrag
        width: 9
        height: parent.height
        x: -(width - Theme.playheadLineWidth) / 2
        cursorShape: Qt.SizeHorCursor
        preventStealing: true
        drag.target: root
        drag.axis: Drag.XAxis
        drag.threshold: 0
        drag.minimumX: 0
        drag.maximumX: plotItem.width - Theme.playheadLineWidth
    }
}
