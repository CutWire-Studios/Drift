import QtQuick
import Drift

// Premiere/Resolve-style height handle on a header's bottom edge. Mouse only: touch keeps the
// context menu's height items. Shift scales every track by the same factor.
MouseArea {
    id: handle

    property var column
    property int trackIndex: -1
    property real lanesPx: 0

    property real startY: 0
    property real startHeight: 0
    property real startScale: 1
    property var startScales: []

    function pointerY(mouse) {
        return mapToItem(column, mouse.x, mouse.y).y
    }

    function scalesOf() {
        const out = []
        for (let i = 0; i < column.tracks.length; ++i)
            out.push(column.tracks[i].heightScale || 1)
        return out
    }

    anchors.left: parent.left
    anchors.right: parent.right
    anchors.bottom: parent.bottom
    height: 6
    acceptedButtons: Qt.LeftButton
    hoverEnabled: true
    preventStealing: true
    cursorShape: Qt.SplitVCursor

    onPressed: (mouse) => {
        if (mouse.source !== Qt.MouseEventNotSynthesized) {
            mouse.accepted = false
            return
        }
        startY = pointerY(mouse)
        startHeight = column.trackHeight(trackIndex)
        startScale = column.tracks[trackIndex].heightScale || 1
        startScales = scalesOf()
    }
    onPositionChanged: (mouse) => {
        if (!pressed)
            return
        const base = (startHeight - lanesPx) / startScale
        if (base <= 0)
            return
        const wanted = (startHeight + pointerY(mouse) - startY - lanesPx) / base
        if (mouse.modifiers & Qt.ShiftModifier) {
            const factor = wanted / startScale
            for (let i = 0; i < startScales.length; ++i)
                EditorState.setTrackHeightScale(i, startScales[i] * factor)
        } else {
            EditorState.setTrackHeightScale(trackIndex, wanted)
        }
    }
    onDoubleClicked: (mouse) => {
        if (mouse.source !== Qt.MouseEventNotSynthesized)
            return
        if (mouse.modifiers & Qt.ShiftModifier) {
            for (let i = 0; i < column.tracks.length; ++i)
                EditorState.setTrackHeightScale(i, 1.0)
        } else {
            EditorState.setTrackHeightScale(trackIndex, 1.0)
        }
    }

    Rectangle {
        anchors.verticalCenter: parent.verticalCenter
        width: parent.width
        height: 2
        color: Theme.c.trackHeaderBottomResize.foreground
        opacity: handle.pressed || handle.containsMouse ? 1 : 0

        Behavior on opacity {
            NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
        }
    }
}
