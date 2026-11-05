import QtQuick
import Drift

// One transition overlap: body, hatch, label, drop target, and edge handles.
Item {
    id: transitionRegion
    property var panel
    property int trackIndex: -1
    required property var modelData
    readonly property int leftClipIndex: modelData.leftClip
    readonly property bool hasTransition: modelData.hasTransition
    readonly property bool transitionSelected:
        EditorState.selectedTransitionTrack === trackIndex
        && EditorState.selectedTransitionLeftClip === leftClipIndex
    // Duration while an edge is being dragged; < 0 otherwise.
    property real liveDuration: -1
    // An overlap window ends where the outgoing clip does and
    // grows leftwards; a window on a cut stays centred on it.
    readonly property real shownStart: {
        if (liveDuration < 0)
            return modelData.start
        if (modelData.overlap)
            return modelData.end - liveDuration
        const left = panel.tracks[trackIndex].clips[leftClipIndex]
        return left.start + left.duration - liveDuration / 2
    }
    readonly property real shownEnd:
        liveDuration < 0 || modelData.overlap ? modelData.end
                                              : shownStart + liveDuration

    z: 10
    x: shownStart * panel.pxPerSecond
    width: Math.max(8, (shownEnd - shownStart) * panel.pxPerSecond)
    height: parent.height - Theme.clipSelectionRingWidth * 2
    y: Theme.clipSelectionRingWidth

    Rectangle {
        anchors.fill: parent
        radius: Theme.c.timelineTransitionRegion.radius
        color: Qt.rgba(Theme.c.timelineTransitionRegion.background.r,
                       Theme.c.timelineTransitionRegion.background.g,
                       Theme.c.timelineTransitionRegion.background.b,
                       transitionRegion.transitionSelected ? 0.85
                       : (transitionRegion.hasTransition ? 0.65 : 0.35))
        border.width: transitionRegion.transitionSelected
                      ? Theme.c.timelineTransitionRegion.borderWidthSelected
                      : Theme.c.timelineTransitionRegion.borderWidth
        border.color: Theme.c.timelineTransitionRegion.border

        // Diagonal hatch so overlaps read as a blend zone.
        TimelineTransitionHatch {
        }

        TimelineTransitionLabel {
            hasTransition: transitionRegion.hasTransition
            labelText: transitionRegion.modelData.label || ""
        }

        Loader {
            active: transitionMouse.containsMouse
                    && transitionRegion.hasTransition
            sourceComponent: ThemedToolTip {
                visible: true
                delay: 400
                text: transitionRegion.modelData.label
                      || transitionRegion.modelData.kind
                      || ""
            }
        }
    }

    DropArea {
        anchors.fill: parent
        keys: ["application/x-drift-transition"]
        onEntered: (drop) => panel.updateTransitionDropPreview(trackIndex,
                                                              transitionRegion.x + drop.x)
        onPositionChanged: (drop) => panel.updateTransitionDropPreview(trackIndex,
                                                                      transitionRegion.x + drop.x)
        onExited: panel.clearTransitionDropPreview()
        onDropped: (drop) => {
            drop.accept(Qt.CopyAction)
            panel.clearTransitionDropPreview()
            const kind = AssetDrag.payloadFromDrop(drop, "transition")
            if (kind.length > 0)
                EditorState.addTransition(trackIndex,
                                          transitionRegion.leftClipIndex,
                                          panel.transitionKindForTrack(trackIndex, kind),
                                          EditorState.preferences.transitionDurationSeconds)
        }
    }

    MouseArea {
        id: transitionMouse
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: {
            if (transitionRegion.hasTransition) {
                EditorState.selectTransition(trackIndex,
                                             transitionRegion.leftClipIndex)
            } else {
                EditorState.addTransition(trackIndex,
                                          transitionRegion.leftClipIndex,
                                          "crossfade", EditorState.preferences.transitionDurationSeconds)
            }
        }
    }

    // Edge handles stretch the transition. A window on a cut
    // grows on both sides, so each pixel dragged counts twice;
    // an overlap can only move its start (the incoming clip).
    Repeater {
        model: !transitionRegion.hasTransition ? []
               : transitionRegion.modelData.overlap ? [-1] : [-1, 1]
        delegate: MouseArea {
            id: edgeHandle
            required property int modelData
            property real pressX: 0
            property real pressDuration: 0

            function durationAt(mouseX) {
                const dx = (mapToItem(transitionRegion.parent, mouseX, 0).x - pressX)
                           / panel.pxPerSecond
                const scale = transitionRegion.modelData.overlap ? 1 : 2
                const d = pressDuration + modelData * dx * scale
                return Math.max(0.1, Math.min(transitionRegion.modelData.maxDuration, d))
            }

            // Straddles the edge so it is easy to hit, but leaves the
            // middle of a narrow region clickable.
            readonly property real inside: Math.min(8, parent.width / 4)
            width: 8 + inside
            height: parent.height
            x: modelData < 0 ? -8 : parent.width - inside
            z: 1
            hoverEnabled: true
            preventStealing: true
            cursorShape: Qt.SizeHorCursor
            onPressed: (mouse) => {
                pressX = mapToItem(transitionRegion.parent, mouse.x, 0).x
                pressDuration = transitionRegion.modelData.end
                                - transitionRegion.modelData.start
                transitionRegion.liveDuration = pressDuration
            }
            onPositionChanged: (mouse) => {
                if (pressed)
                    transitionRegion.liveDuration = durationAt(mouse.x)
            }
            onReleased: (mouse) => {
                const d = durationAt(mouse.x)
                transitionRegion.liveDuration = -1
                if (Math.abs(d - pressDuration) > 0.001)
                    EditorState.setTransitionDuration(trackIndex,
                                                      transitionRegion.modelData.transitionId, d)
            }
            onCanceled: transitionRegion.liveDuration = -1

            HoverHandler { cursorShape: Qt.SizeHorCursor }
        }
    }
}
