import QtQuick
import Drift

// Coverage ribbons for a collapsed parent: one thin strip per nested lane along the bottom of
// the row, showing where that lane has clips. A click expands the lanes.
Item {
    id: ribbons

    property var panel
    property int trackIndex: -1

    readonly property var lanes: {
        const dep = panel.tracks
        return EditorState.adjustmentLaneSpans(trackIndex)
    }
    property int hoveredLane: -1

    height: lanes.length * Theme.adjustmentRibbonHeight

    function kindColor(kind) {
        return kind === "audioEffects" ? Theme.c.timelineLaneRibbons.audio
               : kind === "mask" ? Theme.c.timelineLaneRibbons.mask
               : Theme.c.timelineLaneRibbons.video
    }
    function kindLabel(kind) {
        return kind === "audioEffects" ? qsTr("Audio FX") : kind === "mask" ? qsTr("Mask") : qsTr("Effects")
    }

    Repeater {
        model: ribbons.lanes
        delegate: Item {
            id: ribbon
            required property var modelData
            required property int index
            y: index * Theme.adjustmentRibbonHeight
            width: ribbons.width
            height: Theme.adjustmentRibbonHeight
            opacity: modelData.hidden ? 0.3 : 1

            Repeater {
                model: ribbon.modelData.spans
                delegate: Rectangle {
                    required property var modelData
                    x: modelData.start * ribbons.panel.pxPerSecond
                    width: Math.max(1, (modelData.end - modelData.start) * ribbons.panel.pxPerSecond)
                    height: parent.height
                    color: ribbons.kindColor(ribbon.modelData.kind)
                }
            }
        }
    }

    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onPositionChanged: (mouse) => ribbons.hoveredLane = Math.max(0, Math.min(
            ribbons.lanes.length - 1, Math.floor(mouse.y / Theme.adjustmentRibbonHeight)))
        onExited: ribbons.hoveredLane = -1
        onClicked: EditorState.setTrackLanesCollapsed(ribbons.trackIndex, false)

        ThemedToolTip {
            visible: parent.containsMouse && ribbons.hoveredLane >= 0 && ribbons.hoveredLane < ribbons.lanes.length
            text: ribbons.hoveredLane >= 0 && ribbons.hoveredLane < ribbons.lanes.length
                  ? qsTr("%1 — click to expand").arg(ribbons.kindLabel(ribbons.lanes[ribbons.hoveredLane].kind)) : ""
        }
    }
}
