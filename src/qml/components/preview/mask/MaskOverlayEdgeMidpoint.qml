import QtQuick
import Drift

// Edge midpoint: click to split that edge.
Rectangle {
    id: midpoint

    required property var modelData
    required property int index
    required property var mask
    required property var frame

    // The edge from this vertex to the next, wrapping at the end.
    readonly property var nextPoint: {
        const pts = midpoint.mask.maskData.points || []
        return pts[(midpoint.index + 1) % pts.length]
    }
    readonly property real mxN: nextPoint
            ? (modelData.x + nextPoint.x) / 2 : modelData.x
    readonly property real myN: nextPoint
            ? (modelData.y + nextPoint.y) / 2 : modelData.y

    width: Theme.spacingMd
    height: Theme.spacingMd
    radius: width / 2
    color: "transparent"
    border.width: Theme.c.maskOverlayEdgeMidpoint.borderWidth
    border.color: Theme.c.maskOverlayEdgeMidpoint.border
    opacity: midArea.containsMouse ? 1.0 : 0.5
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
        onClicked: EditorState.insertMaskPoint(
            midpoint.mask.layerTrack, midpoint.mask.layerClip,
            midpoint.index + 1, midpoint.mxN, midpoint.myN)
    }
}
