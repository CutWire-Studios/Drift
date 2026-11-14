import QtQuick
import Drift

// One nested adjustment lane, drawn across the top of its parent track.
Item {
    id: laneStrip

    property var panel
    property Item timelineColumn
    property var viewState
    property real stripWidth: 0
    required property var modelData
    // The lane's own track index, under the name TimelineClipItem looks for on its
    // trackRow — which is this Item, not the row above.
    property int trackIndex: modelData

    width: stripWidth
    height: Theme.adjustmentLaneHeight
    opacity: (panel.tracks[trackIndex] && panel.tracks[trackIndex].hidden === true) ? 0.4 : 1

    TimelineAdjustmentLaneChrome {
        kind: EditorState.adjustmentLaneKind(laneStrip.trackIndex)
    }

    TimelineTrackArea {
        anchors.fill: parent
        panel: laneStrip.panel
        timelineColumn: laneStrip.timelineColumn
        trackIndex: laneStrip.trackIndex
        viewState: laneStrip.viewState
        dimHidden: false
    }

    Loader {
        anchors.fill: parent
        active: laneStrip.opacity < 1
        sourceComponent: TimelineDisabledStripes { viewState: laneStrip.viewState }
    }
}
