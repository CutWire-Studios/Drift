import QtQuick
import Drift

// A nested adjustment lane, drawn inside its parent's row rather than given
// one of its own. This item is what TimelineClipItem reads as its `trackRow`.
Item {
    id: laneStrip

    required property var modelData
    // AndroidTimeline. TimelineTrackArea reads panel.tracks and the move policy.
    required property var panel
    required property var timelineColumn
    required property var viewState

    property int trackIndex: modelData

    height: Theme.adjustmentLaneHeight
    opacity: laneStrip.panel.tracks[trackIndex].hidden === true ? 0.4 : 1

    Rectangle {
        anchors.fill: parent
        color: Qt.alpha(Theme.c.androidTimelineAdjustmentStrip.background, Theme.c.androidTimelineAdjustmentStrip.backgroundOpacity)
    }

    TimelineTrackArea {
        anchors.fill: parent
        panel: laneStrip.panel
        timelineColumn: laneStrip.timelineColumn
        trackIndex: laneStrip.trackIndex
        viewState: laneStrip.viewState
        touchMode: true
    }
}
