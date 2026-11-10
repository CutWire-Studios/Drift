import QtQuick
import Drift

// "+" that appears on a cut between two clips while the pointer is over either neighbour.
Item {
    id: button
    property var panel
    property int trackIndex: -1
    property real clipAreaHeight: 0
    // Pointer position in the track row while it is hovered; < 0 otherwise.
    property real hoverX: -1
    property real hoverY: -1
    required property var region

    // Same height and bottom line as the transition pill it turns into.
    readonly property real pillH: Math.round(clipAreaHeight * 0.4)
    readonly property real pillW: Math.round(pillH * 1.8)
    readonly property real bottomInset: 3
    readonly property var clips: panel.tracks[trackIndex].clips
    readonly property var leftClip: clips[region.leftClip]
    readonly property var rightClip: clips[region.rightClip]
    readonly property bool roomy: leftClip && rightClip
        && Math.min(leftClip.duration, rightClip.duration) * panel.pxPerSecond >= pillW * 2
    readonly property bool pickerOpen: panel.transitionPicker.visible
        && panel.transitionPicker.trackIndex === trackIndex
        && panel.transitionPicker.leftClipIndex === region.leftClip
    readonly property bool overNeighbour: {
        if (hoverX < 0 || hoverY > clipAreaHeight || !leftClip || !rightClip)
            return false
        const s = hoverX / panel.pxPerSecond
        return (s >= leftClip.start && s <= leftClip.start + leftClip.duration)
            || (s >= rightClip.start && s <= rightClip.start + rightClip.duration)
    }
    readonly property bool shown: roomy && panel.tracks[trackIndex].locked !== true
        && (overNeighbour || buttonHover.hovered || pickerOpen)

    x: region.cut * panel.pxPerSecond - pillW / 2
    y: clipAreaHeight - pillH - bottomInset
    width: pillW
    height: pillH
    opacity: shown ? 1 : 0
    visible: opacity > 0

    Behavior on opacity {
        NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
    }

    HoverHandler { id: buttonHover }

    Rectangle {
        anchors.fill: parent
        radius: height / 2
        color: Theme.c.timelineCutButton.background
        border.width: Theme.c.timelineCutButton.borderWidth
        border.color: Theme.c.timelineCutButton.border
        scale: buttonMouse.pressed ? 0.92 : (buttonHover.hovered ? 1.1 : 1)

        Rectangle {
            anchors.fill: parent
            anchors.margins: -1
            radius: height / 2
            color: "transparent"
            border.width: 1
            border.color: Theme.c.timelineCutButton.outline
        }

        Behavior on scale {
            NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
        }

        IconGlyph {
            anchors.centerIn: parent
            glyph: Theme.icons.plus
            iconSize: Math.round(button.pillH * 0.65)
            iconColor: Theme.c.timelineCutButton.foreground
        }
    }

    MouseArea {
        id: buttonMouse
        anchors.fill: parent
        enabled: button.shown
        cursorShape: Qt.PointingHandCursor
        onClicked: button.panel.transitionPicker.openFor(button.trackIndex, button.region.leftClip,
                                                        "", "", button, button.width / 2, button.height)
    }

    ThemedToolTip {
        visible: buttonHover.hovered && !button.pickerOpen
        delay: 400
        text: qsTr("Add transition")
    }
}
