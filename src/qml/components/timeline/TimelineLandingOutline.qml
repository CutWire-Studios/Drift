import QtQuick
import Drift

// Landing preview outline for a library drop or a clip move.
Rectangle {
    id: outline
    property var panel
    property int trackIndex: -1
    visible: panel.dropTrackIndex === trackIndex
             && !panel.dropCreatesNewTrack
    x: panel.dropStartSeconds * panel.pxPerSecond
    width: panel.dropDurationSeconds * panel.pxPerSecond
    height: parent.height
    radius: Theme.c.timelineLandingOutline.radius
    color: Qt.rgba(Theme.c.timelineLandingOutline.background.r, Theme.c.timelineLandingOutline.background.g, Theme.c.timelineLandingOutline.background.b, 0.12)
    border.width: Theme.c.timelineLandingOutline.borderWidth
    border.color: Theme.c.timelineLandingOutline.border
    z: 5

    readonly property var trackClips: panel && panel.tracks && trackIndex >= 0 && trackIndex < panel.tracks.length
                                      ? panel.tracks[trackIndex].clips : []
    readonly property var overlapInfo: {
        if (!panel || !panel.preferences || !panel.preferences.autoCrossfadeOverlaps || !trackClips || trackClips.length === 0)
            return null
        const dropStart = panel.dropStartSeconds
        const dropEnd = dropStart + panel.dropDurationSeconds
        const excludeId = panel.dropExcludeClipId || ""
        for (let i = 0; i < trackClips.length; ++i) {
            const clip = trackClips[i]
            if (excludeId.length > 0 && clip.id === excludeId)
                continue
            const clipStart = clip.start
            const clipEnd = clip.start + clip.duration
            if (dropStart < clipEnd && dropEnd > clipStart) {
                const ovStart = Math.max(dropStart, clipStart)
                const ovEnd = Math.min(dropEnd, clipEnd)
                const ovDur = ovEnd - ovStart
                if (ovDur > 0.001) {
                    return {
                        "x": (ovStart - dropStart) * panel.pxPerSecond,
                        "width": ovDur * panel.pxPerSecond,
                        "duration": ovDur
                    }
                }
            }
        }
        return null
    }

    Rectangle {
        id: crossfadeZone
        visible: outline.overlapInfo !== null
        x: outline.overlapInfo ? outline.overlapInfo.x : 0
        width: outline.overlapInfo ? outline.overlapInfo.width : 0
        height: parent.height
        radius: outline.radius
        color: Qt.rgba(Theme.c.accent.r, Theme.c.accent.g, Theme.c.accent.b, 0.22)
        border.width: 1
        border.color: Theme.c.accent

        TimelineTransitionChevrons {
            anchors.fill: parent
            anchors.margins: 2
            visible: parent.width >= 16
            color: Qt.rgba(Theme.c.accent.r, Theme.c.accent.g, Theme.c.accent.b, 0.45)
        }

        Rectangle {
            anchors.centerIn: parent
            visible: parent.width >= 36
            width: crossfadeLabel.implicitWidth + 8
            height: crossfadeLabel.implicitHeight + 2
            radius: height / 2
            color: Qt.rgba(0, 0, 0, 0.6)

            Text {
                id: crossfadeLabel
                anchors.centerIn: parent
                text: outline.overlapInfo ? (Math.round(outline.overlapInfo.duration * 10) / 10).toFixed(1) + "s" : ""
                color: Theme.c.accent
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeTiny
                font.weight: Font.DemiBold
            }
        }
    }

    // The outline is held on screen after a file drop for as
    // long as the import it started is still probing, so it has
    // to say why it is sitting there empty.
    Text {
        anchors.centerIn: parent
        visible: panel.importDropPending
        text: qsTr("Importing…")
        color: Theme.c.timelineLandingOutline.foreground
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSizeTiny
        font.weight: Font.Medium

        SequentialAnimation on opacity {
            running: panel.importDropPending
            loops: Animation.Infinite
            NumberAnimation { to: 0.45; duration: Theme.durationSlow; easing.type: Theme.easing }
            NumberAnimation { to: 1.0; duration: Theme.durationSlow; easing.type: Theme.easing }
        }
    }
}
