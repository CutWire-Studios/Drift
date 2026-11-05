import QtQuick
import Drift

// Transition chrome. Same geometry as the desktop panel, minus its DropArea —
// there is no drag-and-drop here, so a tap on a bare overlap creates the
// crossfade instead. No preventStealing: a pan across an overlap must still
// reach the Flickable.
Item {
    id: transitionRegion

    required property var modelData
    property int trackIndex: -1
    property real pxPerSecond: 1

    readonly property int leftClipIndex: modelData.leftClip
    readonly property bool hasTransition: modelData.hasTransition
    readonly property bool transitionSelected:
        EditorState.selectedTransitionTrack === transitionRegion.trackIndex
        && EditorState.selectedTransitionLeftClip === leftClipIndex

    z: 10
    x: modelData.start * transitionRegion.pxPerSecond
    // Floored to a fingertip rather than the desktop's
    // 8px, so a half-second crossfade is still tappable.
    width: Math.max(Theme.androidIconButtonSize,
                    (modelData.end - modelData.start) * transitionRegion.pxPerSecond)
    y: Theme.clipSelectionRingWidth
    height: (parent ? parent.height : 0) - Theme.clipSelectionRingWidth * 2

    Rectangle {
        anchors.fill: parent
        radius: Theme.c.androidTimelineTransitionMark.radius
        color: Qt.rgba(Theme.c.androidTimelineTransitionMark.background.r,
                       Theme.c.androidTimelineTransitionMark.background.g,
                       Theme.c.androidTimelineTransitionMark.background.b,
                       transitionRegion.transitionSelected ? 0.85
                       : (transitionRegion.hasTransition ? 0.65 : 0.35))
        border.width: transitionRegion.transitionSelected
                      ? Theme.c.androidTimelineTransitionMark.borderWidthSelected
                      : Theme.c.androidTimelineTransitionMark.borderWidth
        border.color: Theme.c.androidTimelineTransitionMark.border

        Canvas {
            anchors.fill: parent
            anchors.margins: 1
            opacity: 0.35
            onPaint: {
                const ctx = getContext("2d")
                ctx.clearRect(0, 0, width, height)
                ctx.strokeStyle = Theme.c.androidTimelineTransitionMark.hatch
                ctx.lineWidth = Theme.c.androidTimelineTransitionMark.hatchWidth
                for (var x = -height; x < width; x += 6) {
                    ctx.beginPath()
                    ctx.moveTo(x, height)
                    ctx.lineTo(x + height, 0)
                    ctx.stroke()
                }
            }
            onWidthChanged: requestPaint()
            onHeightChanged: requestPaint()
        }

        Text {
            anchors.centerIn: parent
            text: transitionRegion.hasTransition && transitionRegion.modelData.label
                  ? transitionRegion.modelData.label.charAt(0)
                  : "≫"
            color: Theme.c.androidTimelineTransitionMark.foreground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeTiny
            font.weight: Font.Bold
        }
    }

    // Only the glyph in the middle is a target. Covering the
    // whole overlap took the ends of both clips with it, and
    // those are where their trim handles are.
    MouseArea {
        anchors.centerIn: parent
        width: Math.min(parent.width, Theme.androidIconButtonSize)
        height: parent.height
        // A selected neighbour shows trim handles under this glyph; a tap meant for them
        // must not add a crossfade.
        enabled: !(EditorState.selectionRevision >= 0
                   && (EditorState.selectionContains(transitionRegion.trackIndex,
                                                 transitionRegion.leftClipIndex)
                   || EditorState.selectionContains(transitionRegion.trackIndex,
                                                    transitionRegion.modelData.rightClip)))
        onClicked: {
            if (transitionRegion.hasTransition) {
                Haptics.select()
                EditorState.selectTransition(transitionRegion.trackIndex,
                                             transitionRegion.leftClipIndex)
            } else {
                Haptics.confirm()
                EditorState.addTransition(transitionRegion.trackIndex,
                                          transitionRegion.leftClipIndex,
                                          "crossfade", EditorState.preferences.transitionDurationSeconds)
            }
        }
    }
}
