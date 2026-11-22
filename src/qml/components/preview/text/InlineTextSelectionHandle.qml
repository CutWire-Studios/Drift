import QtQuick
import Drift

// Touch selection handle. "caret", "start" or "end" is which way the teardrop's point faces.
Item {
    id: handle

    property string kind: "caret"
    property point tip
    property real touchExtent: 44
    property real knobSize: 20
    property real caretHeight: 0
    // The editor item whose coordinates the drag is reported in.
    required property var space

    signal grabbed()
    signal moved(point pos)

    width: handle.touchExtent
    height: handle.touchExtent
    x: tip.x - width / 2
    y: tip.y

    Rectangle {
        width: handle.knobSize
        height: handle.knobSize
        x: (handle.width - width) / 2
        y: width * (Math.SQRT2 - 1) / 2 + 2
        radius: Theme.radiusPill
        topLeftRadius: handle.kind === "start" ? radius : 0
        topRightRadius: handle.kind === "start" ? 0 : radius
        rotation: handle.kind === "start" ? -45 : 45
        color: Theme.c.inlineTextSelectionHandle.background
    }

    DragHandler {
        id: drag
        target: null
        property point grab: Qt.point(0, 0)

        function scenePoint() {
            return handle.space.mapFromItem(null, centroid.scenePosition.x, centroid.scenePosition.y)
        }

        onActiveChanged: {
            if (!active)
                return
            const p = drag.scenePoint()
            drag.grab = Qt.point(p.x - handle.tip.x, p.y - handle.tip.y)
            handle.grabbed()
        }
        onCentroidChanged: {
            if (!active)
                return
            const p = drag.scenePoint()
            // Hit-tested at the middle of the line the finger's tip points at.
            handle.moved(Qt.point(p.x - drag.grab.x, p.y - drag.grab.y - handle.caretHeight / 2))
        }
    }
}
