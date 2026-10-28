import QtQuick
import Drift

Item {
    id: root

    required property var timeView

    readonly property real fps: {
        void EditorState.tracksRevision
        return Math.max(1, EditorState.projectFile.projectFps())
    }
    readonly property real tickStepSeconds: {
        const needed = 66 / timeView.pxPerSecond
        const steps = []
        for (const n of [1, 2, 5, 10, 15]) {
            if (n / fps < 1)
                steps.push(n / fps)
        }
        Array.prototype.push.apply(steps, [1, 2, 5, 10, 15, 30,
                                           60, 120, 300, 600, 900, 1800, 3600, 7200, 10800, 14400, 21600, 43200])
        for (let i = 0; i < steps.length; ++i) {
            if (steps[i] >= needed)
                return steps[i]
        }
        return steps[steps.length - 1]
    }
    readonly property real tickStepPx: tickStepSeconds * timeView.pxPerSecond
    readonly property int firstTick: Math.max(0, Math.floor(timeView.contentX / Math.max(1, tickStepPx)) - 1)
    readonly property int tickCount: Math.ceil(width / Math.max(1, tickStepPx)) + 3
    readonly property color outsideColor: Qt.rgba(Theme.c.keyframesRuler.outside.r, Theme.c.keyframesRuler.outside.g,
                                                  Theme.c.keyframesRuler.outside.b, Theme.c.keyframesRuler.opacityOutside)

    function formatTick(seconds) {
        const frames = Math.round(Math.max(0, seconds) * fps)
        const whole = Math.floor(frames / fps)
        const pad = (n) => (n < 10 ? "0" : "") + n
        let out = pad(Math.floor(whole / 3600)) + ":" + pad(Math.floor(whole / 60) % 60) + ":" + pad(whole % 60)
        if (tickStepSeconds < 1)
            out += ":" + pad(frames - whole * Math.round(fps))
        return out
    }

    function scrubTo(x) {
        EditorState.playheadSeconds = EditorState.snapTime(Math.max(0, timeView.secondsFor(x)))
    }

    height: Theme.timelineRulerHeight
    clip: true

    Rectangle {
        anchors.fill: parent
        color: Theme.c.keyframesRuler.background
    }

    Rectangle {
        x: 0
        width: Math.max(0, Math.min(root.width, root.timeView.xFor(root.timeView.clipStart)))
        height: parent.height
        color: root.outsideColor
    }

    Rectangle {
        readonly property real edge: Math.max(0, root.timeView.xFor(root.timeView.clipStart + root.timeView.clipDuration))
        x: edge
        width: Math.max(0, root.width - edge)
        height: parent.height
        color: root.outsideColor
    }

    Item {
        x: -root.timeView.contentX
        height: parent.height

        Repeater {
            model: root.tickCount
            delegate: TimelineRulerTick {
                firstTick: root.firstTick
                stepSeconds: root.tickStepSeconds
                pxPerSecond: root.timeView.pxPerSecond
                formatTick: root.formatTick
                rulerHeight: root.height
            }
        }
    }

    Rectangle {
        anchors.bottom: parent.bottom
        width: parent.width
        height: Theme.c.keyframesRuler.borderWidth
        color: Theme.c.keyframesRuler.border
    }

    MouseArea {
        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        preventStealing: true
        onPressed: (mouse) => root.scrubTo(mouse.x)
        onPositionChanged: (mouse) => {
            if (pressed)
                root.scrubTo(mouse.x)
        }
    }
}
