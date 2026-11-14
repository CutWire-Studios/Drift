import QtQuick
import QtQuick.Shapes
import Drift

Item {
    id: toffee
    property var panel
    property int trackIndex: -1
    property real clipAreaHeight: 0
    required property var region

    // A capsule docked to the bottom of the clips: it shows the real duration without hiding
    // the thumbnails, and its ends are the duration handles.
    readonly property real barH: Math.round(clipAreaHeight * 0.4)
    readonly property real bottomInset: 3
    readonly property real gripW: 8
    readonly property int leftClipIndex: region.leftClip
    readonly property bool selected:
        EditorState.selectedTransitionTrack === trackIndex
        && EditorState.selectedTransitionLeftClip === leftClipIndex
    // Duration while an end is being dragged; < 0 otherwise.
    property real liveDuration: -1
    // An overlap window ends where the outgoing clip does and grows leftwards; a window on a
    // cut stays centred on it.
    readonly property real shownStart: {
        if (liveDuration < 0)
            return region.start
        if (region.overlap)
            return region.end - liveDuration
        const left = panel.tracks[trackIndex].clips[leftClipIndex]
        return left.start + left.duration - liveDuration / 2
    }
    readonly property real shownEnd:
        liveDuration < 0 || region.overlap ? region.end : shownStart + liveDuration
    readonly property real bodyW: Math.max(10, (shownEnd - shownStart) * panel.pxPerSecond)
    readonly property bool active: selected || toffeeHover.hovered || dragging
    readonly property bool dragging: liveDuration >= 0
    readonly property color fill: Qt.rgba(Theme.c.timelineTransitionToffee.background.r,
                                          Theme.c.timelineTransitionToffee.background.g,
                                          Theme.c.timelineTransitionToffee.background.b,
                                          selected ? 1.0 : 0.9)

    x: shownStart * panel.pxPerSecond
    y: clipAreaHeight - barH - bottomInset

    HoverHandler {
        id: toffeeHover
    }
    width: bodyW
    height: barH

    // Dark hairline outside the light ring, so the bar reads on bright and dark footage alike.
    Rectangle {
        anchors.fill: parent
        anchors.margins: -1
        radius: height / 2
        color: "transparent"
        border.width: 1
        border.color: Theme.c.timelineTransitionToffee.outline
    }

    Rectangle {
        id: body
        anchors.fill: parent
        radius: height / 2
        color: toffee.fill
        border.width: toffee.selected ? Theme.c.timelineTransitionToffee.borderWidthSelected
                                      : Theme.c.timelineTransitionToffee.borderWidth
        border.color: Qt.rgba(Theme.c.timelineTransitionToffee.border.r,
                              Theme.c.timelineTransitionToffee.border.g,
                              Theme.c.timelineTransitionToffee.border.b,
                              toffee.active ? 1.0 : 0.75)

        TimelineTransitionChevrons {
            anchors.fill: parent
            anchors.leftMargin: toffee.barH * 0.45
            anchors.rightMargin: toffee.barH * 0.45
            visible: width >= toffee.barH * 0.6
            color: Qt.alpha(Theme.c.timelineTransitionToffee.chevron, 0.3)
        }

        Rectangle {
            anchors.centerIn: content
            width: content.implicitWidth + 10
            height: content.implicitHeight + 2
            radius: height / 2
            color: toffee.fill
            visible: content.visible
        }

        Row {
            id: content
            anchors.centerIn: parent
            spacing: 4
            visible: parent.width >= 26

            Shape {
                id: bowtie
                width: Math.round(toffee.barH * 0.55)
                height: Math.round(toffee.barH * 0.4)
                anchors.verticalCenter: parent.verticalCenter
                preferredRendererType: Shape.CurveRenderer
                ShapePath {
                    fillColor: Theme.c.timelineTransitionToffee.foreground
                    strokeWidth: -1
                    startX: 0; startY: 0
                    PathLine { x: bowtie.width / 2; y: bowtie.height / 2 }
                    PathLine { x: 0; y: bowtie.height }
                    PathLine { x: 0; y: 0 }
                }
                ShapePath {
                    fillColor: Theme.c.timelineTransitionToffee.foreground
                    strokeWidth: -1
                    startX: bowtie.width; startY: 0
                    PathLine { x: bowtie.width / 2; y: bowtie.height / 2 }
                    PathLine { x: bowtie.width; y: bowtie.height }
                    PathLine { x: bowtie.width; y: 0 }
                }
            }

            Text {
                anchors.verticalCenter: parent.verticalCenter
                width: Math.min(implicitWidth, body.width - bowtie.width - toffee.gripW * 2 - 8)
                visible: body.width >= 72
                elide: Text.ElideRight
                text: toffee.region.label || toffee.region.kind || ""
                color: Theme.c.timelineTransitionToffee.foreground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeTiny
                font.weight: Font.DemiBold
            }
        }

        MouseArea {
            id: bodyMouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: EditorState.selectTransition(toffee.trackIndex, toffee.leftClipIndex)
            onDoubleClicked: toffee.panel.transitionPicker.openFor(
                                 toffee.trackIndex, toffee.leftClipIndex, toffee.region.transitionId,
                                 toffee.region.kind || "", body, body.width / 2, body.height)
        }

        Loader {
            active: bodyMouse.containsMouse
            sourceComponent: ThemedToolTip {
                visible: true
                delay: 400
                text: toffee.region.label || toffee.region.kind || ""
            }
        }
    }

    DropArea {
        anchors.fill: parent
        keys: ["application/x-drift-transition"]
        onEntered: (drop) => toffee.panel.updateTransitionDropPreview(toffee.trackIndex,
                                                                      toffee.x + drop.x)
        onPositionChanged: (drop) => toffee.panel.updateTransitionDropPreview(toffee.trackIndex,
                                                                              toffee.x + drop.x)
        onExited: toffee.panel.clearTransitionDropPreview()
        onDropped: (drop) => {
            drop.accept(Qt.CopyAction)
            toffee.panel.clearTransitionDropPreview()
            const kind = AssetDrag.payloadFromDrop(drop, "transition")
            if (kind.length > 0)
                EditorState.addTransition(toffee.trackIndex, toffee.leftClipIndex,
                                          toffee.panel.transitionKindForTrack(toffee.trackIndex, kind),
                                          EditorState.preferences.transitionDurationSeconds)
        }
    }

    // A window on a cut grows on both sides, so each pixel dragged counts twice; an overlap
    // can only move its start (the incoming clip).
    Repeater {
        model: toffee.region.overlap ? [-1] : [-1, 1]
        delegate: MouseArea {
            id: edgeHandle
            required property int modelData
            property real pressX: 0
            property real pressDuration: 0
            property real pointerSceneX: 0

            function pointerLocalX() {
                return mapFromItem(null, pointerSceneX, 0).x
            }

            Timer {
                interval: 16
                repeat: true
                running: edgeHandle.pressed
                         && typeof toffee.panel.dragEdgeScroll === "function"
                onTriggered: {
                    const panel = toffee.panel
                    const px = edgeHandle.mapToItem(toffee.parent, edgeHandle.pointerLocalX(), 0).x
                             - panel.timelineViewX
                    const edgeX = Math.min(48, panel.timelineViewW * 0.25)
                    let dx = 0
                    if (px < edgeX)
                        dx = -Math.min(24, 6 + (edgeX - px) * 0.4)
                    else if (px > panel.timelineViewW - edgeX)
                        dx = Math.min(24, 6 + (px - (panel.timelineViewW - edgeX)) * 0.4)
                    if (dx !== 0 && panel.dragEdgeScroll(dx, 0).x !== 0)
                        toffee.liveDuration = edgeHandle.durationAt(edgeHandle.pointerLocalX())
                }
            }

            function durationAt(mouseX) {
                const dx = (mapToItem(toffee.parent, mouseX, 0).x - pressX) / toffee.panel.pxPerSecond
                const scale = toffee.region.overlap ? 1 : 2
                const d = pressDuration + modelData * dx * scale
                return Math.max(0.1, Math.min(toffee.region.maxDuration, d))
            }

            x: modelData < 0 ? -(earItem.implicitWidth - earItem.overlap)
                             : toffee.width - toffee.gripW
            y: -3
            width: toffee.gripW + earItem.implicitWidth - earItem.overlap
            height: toffee.barH + 6
            hoverEnabled: true
            preventStealing: true
            TimelineTransitionEar {
                id: earItem
                x: edgeHandle.modelData < 0 ? 0 : toffee.gripW - overlap
                y: 3
                width: implicitWidth
                height: toffee.barH
                color: Theme.c.timelineTransitionToffee.handle
                gripColor: Theme.c.timelineTransitionToffee.handleGrip
                outlineColor: Theme.c.timelineTransitionToffee.outline
                shown: Theme.touchInput || toffee.active || edgeHandle.containsMouse
                       || edgeHandle.pressed
            }
            TimelineCursorRequest {
                kind: "transitionResize"
                active: edgeHandle.containsMouse || edgeHandle.pressed
            }
            onPressed: (mouse) => {
                pointerSceneX = mapToItem(null, mouse.x, 0).x
                pressX = mapToItem(toffee.parent, mouse.x, 0).x
                pressDuration = toffee.region.end - toffee.region.start
                toffee.liveDuration = pressDuration
            }
            onPositionChanged: (mouse) => {
                if (!pressed)
                    return
                pointerSceneX = mapToItem(null, mouse.x, 0).x
                toffee.liveDuration = durationAt(mouse.x)
            }
            onReleased: (mouse) => {
                const d = durationAt(pointerLocalX())
                toffee.liveDuration = -1
                if (Math.abs(d - pressDuration) > 0.001)
                    EditorState.setTransitionDuration(toffee.trackIndex,
                                                      toffee.region.transitionId, d)
            }
            onCanceled: toffee.liveDuration = -1
        }
    }
}
