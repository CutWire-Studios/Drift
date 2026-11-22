import QtQuick
import Drift

// The whole source in a thin strip: trim span, ranges, markers and the zoom window of the
// strip below. Drag the window to scroll; click elsewhere to bring it there.
Rectangle {
    id: root

    required property var panel

    readonly property var lab: EditorState.sourceLab
    readonly property real duration: Math.max(0.001, root.lab.durationSeconds)
    readonly property real pxPerSecond: root.width / root.duration
    readonly property real trimIn: root.lab.recipe.trimIn ?? 0
    readonly property real trimOut: (root.lab.recipe.trimOut ?? -1) < 0 ? root.duration : root.lab.recipe.trimOut

    color: Theme.c.sourceLabStrip.background
    border.width: Theme.c.sourceLabStrip.borderWidth
    border.color: Theme.c.sourceLabStrip.border
    radius: Theme.c.sourceLabStrip.radius
    clip: true

    Repeater {
        model: root.lab.ranges
        Rectangle {
            required property var modelData
            readonly property color tint: modelData.rating > 0 ? Theme.c.sourceLabStrip.rangeFavorite
                                          : modelData.rating < 0 ? Theme.c.sourceLabStrip.rangeReject
                                                                 : Theme.c.sourceLabStrip.rangeNeutral
            x: modelData.in * root.pxPerSecond
            width: Math.max(2, (modelData.out - modelData.in) * root.pxPerSecond)
            height: parent.height
            color: Qt.alpha(tint, Theme.c.sourceLabStrip.rangeOpacity)
        }
    }

    MediaPreviewTrimShade {
        leading: true
        edge: root.trimIn * root.pxPerSecond
        span: root.width
    }
    MediaPreviewTrimShade {
        leading: false
        edge: root.trimOut * root.pxPerSecond
        span: root.width
    }
    MediaPreviewTrimSpan {
        inX: root.trimIn * root.pxPerSecond
        outX: root.trimOut * root.pxPerSecond
    }

    Repeater {
        model: root.lab.markers
        Rectangle {
            required property var modelData
            x: modelData.time * root.pxPerSecond
            width: 1
            height: parent.height
            color: Theme.c.sourceLabStrip.marker
        }
    }

    MediaPreviewPlayheadLine {
        playX: EditorState.assetPreview.position * root.pxPerSecond
    }

    Rectangle {
        id: window
        x: root.panel.viewStart * root.pxPerSecond
        width: Math.max(4, root.panel.viewDur * root.pxPerSecond)
        height: parent.height
        color: Qt.alpha(Theme.c.sourceLabStrip.windowFill, Theme.c.sourceLabStrip.windowFillOpacity)
        border.width: Theme.c.sourceLabStrip.windowBorderWidth
        border.color: Theme.c.sourceLabStrip.windowBorder
        z: 2
    }

    MouseArea {
        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        z: 3
        property real grab: 0
        onPressed: (mouse) => {
            const t = mouse.x / root.pxPerSecond
            const inside = t >= root.panel.viewStart && t <= root.panel.viewStart + root.panel.viewDur
            grab = inside ? t - root.panel.viewStart : root.panel.viewDur / 2
            if (!inside)
                root.panel.scrollTo(t - grab)
        }
        onPositionChanged: (mouse) => {
            if (pressed)
                root.panel.scrollTo(mouse.x / root.pxPerSecond - grab)
        }
    }
}
