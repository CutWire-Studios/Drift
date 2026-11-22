import QtQuick
import Drift

// New-track indicator: a ghost lane straddling the boundary the track would be
// inserted at, drawn at real track height so it reads as a track rather than a
// hairline. Aiming at the top or bottom band of a row is the only way to say
// "put this on its own track" without a keyboard.
Item {
    id: newTrackIndicator

    // AndroidTimeline: dropCreatesNewTrack, pendingDropKind, dropNewTrackIndex,
    // newTrackBoundaryY, seekHeaderHeight, dropStartSeconds, dropDurationSeconds, pxPerSecond.
    required property var panel
    property real viewX: 0

    visible: newTrackIndicator.panel.dropCreatesNewTrack
    readonly property real laneHeight: {
        const t = newTrackIndicator.panel.pendingDropKind.length > 0
                ? EditorState.trackTypeForKind(newTrackIndicator.panel.pendingDropKind)
                : EditorState.trackTypeForAsset(EditorState.draggingAssetIndex)
        if (t === "video") return Theme.trackHeightVideo
        if (t === "audio") return Theme.trackHeightAudio
        if (t === "shape") return Theme.trackHeightShape
        if (t === "subtitle") return Theme.trackHeightSubtitle
        return Theme.trackHeightText
    }
    x: 0
    y: newTrackIndicator.panel.seekHeaderHeight
       + newTrackIndicator.panel.newTrackBoundaryY(newTrackIndicator.panel.dropNewTrackIndex)
       - laneHeight / 2
    width: parent ? parent.width : 0
    height: laneHeight
    z: 7

    Rectangle {
        anchors.fill: parent
        radius: Theme.c.androidTimelineNewTrackGhost.radius
        color: Qt.alpha(Theme.c.androidTimelineNewTrackGhost.background, Theme.c.androidTimelineNewTrackGhost.backgroundOpacity)
        border.width: Theme.c.androidTimelineNewTrackGhost.borderWidth
        border.color: Theme.c.androidTimelineNewTrackGhost.border
    }

    // The boundary itself, so which gap the lane is going into
    // is unambiguous.
    Rectangle {
        width: parent.width
        height: Theme.c.androidTimelineNewTrackGhost.guideWidth
        anchors.verticalCenter: parent.verticalCenter
        color: Theme.c.androidTimelineNewTrackGhost.guide
    }

    // Where in time the clip would land, inside the lane it would
    // land on — the outline on an existing row is suppressed while
    // this is up.
    Rectangle {
        x: newTrackIndicator.panel.dropStartSeconds * newTrackIndicator.panel.pxPerSecond
        width: newTrackIndicator.panel.dropDurationSeconds * newTrackIndicator.panel.pxPerSecond
        height: parent.height
        radius: Theme.c.androidTimelineNewTrackGhost.radius
        color: Qt.alpha(Theme.c.androidTimelineNewTrackGhost.dropBackground, Theme.c.androidTimelineNewTrackGhost.dropBackgroundOpacity)
        border.width: Theme.c.androidTimelineNewTrackGhost.dropBorderWidth
        border.color: Theme.c.androidTimelineNewTrackGhost.dropBorder
    }

    // Pinned to the viewport: the lane spans the whole timeline,
    // and a label at its centre is usually off screen.
    Text {
        x: newTrackIndicator.viewX + Theme.spacingLg
        anchors.verticalCenter: parent.verticalCenter
        text: qsTr("New track")
        color: Theme.c.androidTimelineNewTrackGhost.foreground
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSizeXs
        font.weight: Font.Medium
    }
}
