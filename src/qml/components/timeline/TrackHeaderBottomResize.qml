import QtQuick
import Drift

// Premiere/Resolve-style height handle on a header's bottom edge. Mouse only: touch keeps the
// context menu's height items. Shift scales every track by the same factor.
HandleKnob {
    id: handle

    property var column
    property int trackIndex: -1
    property real lanesPx: 0

    property real startY: 0
    property real startHeight: 0
    property real startScale: 1
    property var startScales: []
    property real lastClickMs: 0

    function pointerY(point) {
        return mapToItem(column, point.x, point.y).y
    }

    function scalesOf() {
        const out = []
        for (let i = 0; i < column.tracks.length; ++i)
            out.push(column.tracks[i].heightScale || 1)
        return out
    }

    function heightPx(wanted) {
        return Math.round(lanesPx + (startHeight - lanesPx) / startScale * wanted)
    }

    x: 0
    y: parent.height - 7
    width: parent.width
    height: 14
    mouseHitSize: width
    mouseHitHeight: height
    touchHitSize: 0
    touchHitHeight: 0
    shape: "none"
    space: null
    cursor: Qt.SizeVerCursor
    TimelineCursorRequest {
        kind: "trackResizeV"
        active: handle.hovered || handle.active
    }

    Rectangle {
        anchors.verticalCenter: parent.verticalCenter
        width: parent.width
        height: 2
        color: Theme.c.trackHeaderBottomResize.foreground
        opacity: handle.active || handle.hovered ? 1 : 0

        Behavior on opacity {
            NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
        }
    }

    onPressed: (point, modifiers, device) => {
        startY = pointerY(point)
        startHeight = column.trackHeight(trackIndex)
        startScale = column.tracks[trackIndex].heightScale || 1
        startScales = scalesOf()
        EditorState.drag.begin("trackHeight", device, qsTr("Resize track"))
    }
    onMoved: (point, modifiers, device) => {
        const base = (startHeight - lanesPx) / startScale
        if (base <= 0)
            return
        const wanted = (startHeight + pointerY(point) - startY - lanesPx) / base
        if (modifiers & Qt.ShiftModifier) {
            const factor = wanted / startScale
            for (let i = 0; i < startScales.length; ++i)
                EditorState.setTrackHeightScale(i, startScales[i] * factor)
        } else {
            EditorState.setTrackHeightScale(trackIndex, wanted)
        }
        EditorState.drag.update(qsTr("%1 px").arg(heightPx(wanted)))
        column.setDragPointer(mapToItem(null, point.x, point.y).x, mapToItem(null, point.x, point.y).y)
    }
    onReleased: EditorState.drag.commit()
    onClicked: (point, modifiers, device) => {
        const now = Date.now()
        if (now - lastClickMs > Qt.styleHints.mouseDoubleClickInterval) {
            lastClickMs = now
            return
        }
        lastClickMs = 0
        if (modifiers & Qt.ShiftModifier) {
            for (let i = 0; i < column.tracks.length; ++i)
                EditorState.setTrackHeightScale(i, 1.0)
        } else {
            EditorState.setTrackHeightScale(trackIndex, 1.0)
        }
    }

}
