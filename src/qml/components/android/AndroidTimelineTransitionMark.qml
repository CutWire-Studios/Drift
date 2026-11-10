import QtQuick
import QtQuick.Shapes
import Drift

// Transition chrome for one cut: a "+" on an empty cut, a toffee over an existing transition.
// No preventStealing outside the duration handles: a pan across a cut must still reach the Flickable.
Item {
    id: mark

    required property var modelData
    property var panel
    property int trackIndex: -1
    property real pxPerSecond: 1
    property real clipAreaTop: 0
    property real clipAreaHeight: 0

    readonly property var region: modelData
    readonly property int leftClipIndex: region.leftClip
    readonly property bool hasTransition: region.hasTransition
    readonly property bool selected:
        EditorState.selectedTransitionTrack === trackIndex
        && EditorState.selectedTransitionLeftClip === leftClipIndex

    readonly property real addW: Math.round(barH * 1.8)
    readonly property var clips: panel.tracks[trackIndex].clips
    readonly property var leftClip: clips[leftClipIndex]
    readonly property var rightClip: clips[region.rightClip]
    readonly property bool roomy: leftClip && rightClip
        && Math.min(leftClip.duration, rightClip.duration) * pxPerSecond
           >= Math.max(Theme.androidIconButtonSize, addW) * 1.5
    readonly property bool neighbourSelected: EditorState.selectionRevision >= 0
        && (EditorState.selectionContains(trackIndex, leftClipIndex)
            || EditorState.selectionContains(trackIndex, region.rightClip))
    readonly property bool addShown: !hasTransition && roomy && !neighbourSelected
        && panel.tracks[trackIndex].locked !== true

    readonly property real barH: Math.round(clipAreaHeight * 0.55)
    readonly property real bottomInset: 3
    readonly property real gripW: 10
    property real liveDuration: -1
    readonly property real shownStart: {
        if (liveDuration < 0)
            return region.start
        if (region.overlap)
            return region.end - liveDuration
        return leftClip.start + leftClip.duration - liveDuration / 2
    }
    readonly property real shownEnd:
        liveDuration < 0 || region.overlap ? region.end : shownStart + liveDuration
    readonly property real bodyW: Math.max(10, (shownEnd - shownStart) * pxPerSecond)
    readonly property color fill: Qt.rgba(Theme.c.androidTimelineTransitionMark.background.r,
                                          Theme.c.androidTimelineTransitionMark.background.g,
                                          Theme.c.androidTimelineTransitionMark.background.b,
                                          selected ? 1.0 : 0.9)

    z: 10
    y: clipAreaTop
    width: parent ? parent.width : 0
    height: clipAreaHeight

    Item {
        id: addButton
        visible: mark.addShown
        // The hit area is a fingertip; the pill inside sits on the same bottom line as the
        // transition pill it turns into.
        x: mark.region.cut * mark.pxPerSecond - width / 2
        y: mark.clipAreaHeight - height
        width: Math.max(Theme.androidIconButtonSize, mark.addW)
        height: Math.max(Theme.androidIconButtonSize, mark.barH + mark.bottomInset)

        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom
            anchors.bottomMargin: mark.bottomInset
            width: mark.addW
            height: mark.barH
            radius: height / 2
            color: Theme.c.androidTimelineTransitionMark.addBackground
            border.width: Theme.c.androidTimelineTransitionMark.addBorderWidth
            border.color: Theme.c.androidTimelineTransitionMark.addBorder
            scale: addMouse.pressed ? 0.92 : 1

            Rectangle {
                anchors.fill: parent
                anchors.margins: -1
                radius: height / 2
                color: "transparent"
                border.width: 1
                border.color: Theme.c.androidTimelineTransitionMark.outline
            }

            IconGlyph {
                anchors.centerIn: parent
                glyph: Theme.icons.plus
                iconSize: Math.round(mark.barH * 0.6)
                iconColor: Theme.c.androidTimelineTransitionMark.addForeground
            }
        }

        MouseArea {
            id: addMouse
            anchors.fill: parent
            enabled: addButton.visible
            onClicked: {
                Haptics.select()
                if (ShellBridge.main.editorPage)
                    ShellBridge.main.editorPage.openTransitionPicker(mark.trackIndex,
                                                                     mark.leftClipIndex, "", "")
            }
        }
    }

    // A capsule docked to the bottom of the clips, matching the desktop timeline.
    Item {
        id: toffee
        visible: mark.hasTransition
        x: mark.shownStart * mark.pxPerSecond
        y: mark.clipAreaHeight - mark.barH - mark.bottomInset
        width: mark.bodyW
        height: mark.barH

        // Dark hairline outside the light ring, so the bar reads on bright and dark footage alike.
        Rectangle {
            anchors.fill: parent
            anchors.margins: -1
            radius: height / 2
            color: "transparent"
            border.width: 1
            border.color: Theme.c.androidTimelineTransitionMark.outline
        }

        Rectangle {
            id: body
            anchors.fill: parent
            radius: height / 2
            color: mark.fill
            border.width: mark.selected ? Theme.c.androidTimelineTransitionMark.borderWidthSelected
                                        : Theme.c.androidTimelineTransitionMark.borderWidth
            border.color: Qt.rgba(Theme.c.androidTimelineTransitionMark.border.r,
                                  Theme.c.androidTimelineTransitionMark.border.g,
                                  Theme.c.androidTimelineTransitionMark.border.b,
                                  mark.selected ? 1.0 : 0.75)

            Row {
                anchors.centerIn: parent
                spacing: 4
                visible: parent.width >= 26

                Shape {
                    id: bowtie
                    width: Math.round(mark.barH * 0.55)
                    height: Math.round(mark.barH * 0.4)
                    anchors.verticalCenter: parent.verticalCenter
                    preferredRendererType: Shape.CurveRenderer
                    ShapePath {
                        fillColor: Theme.c.androidTimelineTransitionMark.foreground
                        strokeWidth: -1
                        startX: 0; startY: 0
                        PathLine { x: bowtie.width / 2; y: bowtie.height / 2 }
                        PathLine { x: 0; y: bowtie.height }
                        PathLine { x: 0; y: 0 }
                    }
                    ShapePath {
                        fillColor: Theme.c.androidTimelineTransitionMark.foreground
                        strokeWidth: -1
                        startX: bowtie.width; startY: 0
                        PathLine { x: bowtie.width / 2; y: bowtie.height / 2 }
                        PathLine { x: bowtie.width; y: bowtie.height }
                        PathLine { x: bowtie.width; y: 0 }
                    }
                }

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    width: Math.min(implicitWidth, body.width - bowtie.width - mark.gripW * 2 - 8)
                    visible: body.width >= 72
                    elide: Text.ElideRight
                    text: mark.region.label || mark.region.kind || ""
                    color: Theme.c.androidTimelineTransitionMark.foreground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeTiny
                    font.weight: Font.DemiBold
                }
            }

            Repeater {
                model: mark.selected ? (mark.region.overlap ? [-1] : [-1, 1]) : []
                delegate: Rectangle {
                    required property int modelData
                    visible: body.width >= 24
                    width: 2
                    height: Math.round(mark.barH * 0.45)
                    radius: 1
                    anchors.verticalCenter: parent.verticalCenter
                    x: modelData < 0 ? mark.gripW / 2 : body.width - mark.gripW / 2 - width
                    color: Theme.c.androidTimelineTransitionMark.grip
                }
            }
        }

        // Taller than the bar so it is a fingertip target; anchored to the bar's bottom so it
        // doesn't reach past the clip.
        MouseArea {
            anchors.bottom: parent.bottom
            anchors.bottomMargin: -mark.bottomInset
            width: parent.width
            height: Math.max(parent.height + mark.bottomInset, Theme.androidIconButtonSize * 0.75)
            enabled: toffee.visible
            onClicked: {
                Haptics.select()
                EditorState.selectTransition(mark.trackIndex, mark.leftClipIndex)
            }
        }

        // Only a selected transition grows handles, so a pan that starts on an unselected one
        // still scrolls. A window on a cut grows on both sides; an overlap only moves its start.
        Repeater {
            model: mark.selected && mark.hasTransition ? (mark.region.overlap ? [-1] : [-1, 1]) : []
            delegate: MouseArea {
                id: edgeHandle
                required property int modelData
                property real pressX: 0
                property real pressDuration: 0

                function durationAt(mouseX) {
                    const dx = (mapToItem(mark, mouseX, 0).x - pressX) / mark.pxPerSecond
                    const scale = mark.region.overlap ? 1 : 2
                    const d = pressDuration + modelData * dx * scale
                    return Math.max(0.1, Math.min(mark.region.maxDuration, d))
                }

                x: modelData < 0 ? -Theme.spacingMd : toffee.width - mark.gripW
                y: toffee.height + mark.bottomInset - height
                width: mark.gripW + Theme.spacingMd
                height: Theme.androidIconButtonSize * 0.75
                preventStealing: true
                onPressed: (mouse) => {
                    pressX = mapToItem(mark, mouse.x, 0).x
                    pressDuration = mark.region.end - mark.region.start
                    mark.liveDuration = pressDuration
                }
                onPositionChanged: (mouse) => {
                    if (pressed)
                        mark.liveDuration = durationAt(mouse.x)
                }
                onReleased: (mouse) => {
                    const d = durationAt(mouse.x)
                    mark.liveDuration = -1
                    if (Math.abs(d - pressDuration) > 0.001)
                        EditorState.setTransitionDuration(mark.trackIndex,
                                                          mark.region.transitionId, d)
                }
                onCanceled: mark.liveDuration = -1
            }
        }
    }
}
