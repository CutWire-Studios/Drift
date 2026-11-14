import QtQuick
import Drift

// Cut-tool overlay: red dashed virtual playhead, doomed-side tint, and the click that splits.
Item {
    id: cutOverlay

    property var panel
    visible: panel.timelineTool !== ""
    enabled: panel.timelineTool !== ""
    x: 0
    y: Theme.timelineRulerHeight + Theme.timelineBookmarkRowHeight
    width: parent.width
    height: panel.totalTracksHeightCached
    z: 20

    readonly property bool trimMode: panel.timelineTool === "trimStart"
                                     || panel.timelineTool === "trimEnd"
    property real alignedSeconds: -1
    readonly property var hoverClip: (panel.cutHoverTrack >= 0 && panel.cutHoverClip >= 0
        && panel.cutHoverTrack < panel.tracks.length
        && panel.cutHoverClip < panel.tracks[panel.cutHoverTrack].clips.length)
        ? panel.tracks[panel.cutHoverTrack].clips[panel.cutHoverClip]
        : null

    function seconds(mx) {
        return EditorState.snapTime(Math.max(0, mx) / panel.pxPerSecond)
    }

    function updateHover(mx, my) {
        const raw = Math.max(0, mx) / panel.pxPerSecond
        const snapped = seconds(mx)
        panel.cutHoverSeconds = snapped
        alignedSeconds = EditorState.snapEnabled && Math.abs(snapped - raw) > 0.0005
                         ? snapped : -1
        const trackIdx = panel.trackIndexAtY(my)
        panel.cutHoverTrack = trackIdx
        panel.cutHoverClip = trackIdx >= 0
            ? panel.clipIndexAtPosition(trackIdx, Math.max(0, mx))
            : -1
    }

    MouseArea {
        id: cutMouse
        anchors.fill: parent
        hoverEnabled: true
        TimelineCursorRequest {
            kind: cutOverlay.panel.timelineTool === "trimStart" ? "trimStartTool"
                  : cutOverlay.panel.timelineTool === "trimEnd" ? "trimEndTool" : "blade"
            active: cutOverlay.visible && cutMouse.containsMouse
        }
        acceptedButtons: Qt.LeftButton
        onEntered: cutOverlay.updateHover(mouseX, mouseY)
        onPositionChanged: (mouse) => cutOverlay.updateHover(mouse.x, mouse.y)
        onExited: {
            panel.cutHoverSeconds = -1
            panel.cutHoverTrack = -1
            panel.cutHoverClip = -1
            cutOverlay.alignedSeconds = -1
        }
        onClicked: (mouse) => {
            const atSeconds = cutOverlay.seconds(mouse.x)
            const trackIdx = panel.trackIndexAtY(mouse.y)
            if (trackIdx < 0)
                return
            const clipIdx = panel.clipIndexAtPosition(trackIdx, Math.max(0, mouse.x))
            if (clipIdx < 0)
                return
            if (panel.timelineTool === "trimStart")
                EditorState.splitClipLeftAt(trackIdx, clipIdx, atSeconds)
            else if (panel.timelineTool === "trimEnd")
                EditorState.splitClipRightAt(trackIdx, clipIdx, atSeconds)
            else
                EditorState.splitClipAt(trackIdx, clipIdx, atSeconds)
        }
    }

    // Red gradient on the doomed side of the hovered clip: a
    // fixed band attached to the cut line, reddest at the
    // pointer and fading away from it, clamped to the clip.
    TimelineCutDoomGradient {
        host: cutOverlay
        panel: cutOverlay.panel
    }

    // A full-height alignment ruler makes an edge on another audio/video lane
    // visible when splitting or trimming with the cut tool.
    Rectangle {
        visible: cutOverlay.alignedSeconds >= 0
        x: cutOverlay.alignedSeconds * panel.pxPerSecond
        width: Math.max(1, Theme.c.timelineSnapGuide.borderWidth)
        height: parent.height
        color: Theme.c.timelineSnapGuide.foreground
        z: 1
    }

    // Red dashed virtual playhead. A column of dashes reflows
    // with the track height without any repaint plumbing.
    Column {
        visible: panel.cutHoverSeconds >= 0
        x: panel.cutHoverSeconds * panel.pxPerSecond
        spacing: 3
        Repeater {
            model: Math.ceil(cutOverlay.height / 7)
            delegate: TimelineCutDash { }
        }
    }
}
