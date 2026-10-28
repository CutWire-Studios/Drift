import QtQuick
import Drift

// Edge midpoint: click to split that edge.
Rectangle {
    id: midpoint

    required property int index
    required property var mask
    required property var frame

    // The edge from this vertex to the next, wrapping at the end.
    readonly property var pts: midpoint.mask.displayPoints
    readonly property var thisPoint: pts[midpoint.index]
    readonly property var nextPoint: pts.length > 0 ? pts[(midpoint.index + 1) % pts.length] : undefined
    readonly property real mxN: thisPoint
            ? (nextPoint ? (thisPoint.x + nextPoint.x) / 2 : thisPoint.x) : 0
    readonly property real myN: thisPoint
            ? (nextPoint ? (thisPoint.y + nextPoint.y) / 2 : thisPoint.y) : 0

    width: Theme.spacingMd
    height: Theme.spacingMd
    radius: width / 2
    color: "transparent"
    border.width: Theme.c.maskOverlayEdgeMidpoint.borderWidth
    border.color: Theme.c.maskOverlayEdgeMidpoint.border
    opacity: midArea.containsMouse ? 1.0 : 0.8
    x: mxN * midpoint.frame.width - width / 2
    y: myN * midpoint.frame.height - height / 2
    z: 199

    MouseArea {
        id: midArea
        anchors.fill: parent
        anchors.margins: -Theme.spacingSm
        hoverEnabled: true
        cursorShape: Qt.CrossCursor
        preventStealing: true
        onWheel: (wheel) => { wheel.accepted = false }
        // Inserting before the far end of the edge splits that edge.
        onClicked: {
            const id = midpoint.mask.layerId
            const i = midpoint.index + 1
            const nx = midpoint.mxN
            const ny = midpoint.myN
            Qt.callLater(() => EditorState.masking.editSession.insertPoint(id, i, nx, ny))
        }
    }
}
