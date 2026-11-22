import QtQuick
import Drift

// Red gradient on the doomed side of the hovered clip: a fixed band attached to the
// cut line, reddest at the pointer and fading away from it, clamped to the clip.
Rectangle {
    id: doomGradient

    property var host
    property var panel
    readonly property real bandLength: 80
    visible: host.trimMode && host.hoverClip !== null
             && panel.cutHoverSeconds >= 0
    readonly property real clipStartX: host.hoverClip
        ? host.hoverClip.start * panel.pxPerSecond : 0
    readonly property real clipEndX: host.hoverClip
        ? (host.hoverClip.start + host.hoverClip.duration) * panel.pxPerSecond : 0
    readonly property real cutX: Math.max(clipStartX,
        Math.min(clipEndX, panel.cutHoverSeconds * panel.pxPerSecond))
    readonly property color solid: Qt.alpha(Theme.c.timelineCutDoomGradient.foreground, Theme.c.timelineCutDoomGradient.foregroundOpacity)
    readonly property color clear: Qt.rgba(Theme.c.timelineCutDoomGradient.foreground.r, Theme.c.timelineCutDoomGradient.foreground.g, Theme.c.timelineCutDoomGradient.foreground.b, 0.0)

    x: panel.timelineTool === "trimStart"
       ? Math.max(clipStartX, cutX - bandLength)
       : cutX
    width: panel.timelineTool === "trimStart"
           ? cutX - x
           : Math.min(clipEndX, cutX + bandLength) - cutX
    y: panel.trackOffsetY(panel.cutHoverTrack)
    height: host.hoverClip
        ? panel.trackHeight(panel.cutHoverTrack) : 0
    radius: Theme.c.timelineCutDoomGradient.radius

    gradient: Gradient {
        orientation: Gradient.Horizontal
        // Red toward the cut line (the pointer), fading away.
        GradientStop {
            position: 0.0
            color: panel.timelineTool === "trimStart"
                   ? doomGradient.clear : doomGradient.solid
        }
        GradientStop {
            position: 1.0
            color: panel.timelineTool === "trimStart"
                   ? doomGradient.solid : doomGradient.clear
        }
    }
}
