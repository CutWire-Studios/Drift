import QtQuick
import Drift

// One subtitle cue: drag the body to move, the edges to resize.
Rectangle {
    property var lane
    id: block
    required property int index
    required property real start
    required property real end
    required property string text

    readonly property bool isSelected: index === EditorState.selectedSubtitleCue
    readonly property bool isActive: index === lane.activeCueIndex
    readonly property real gripW: Math.min(8, Math.max(3, width / 4))
    // A finger cannot land on an 8px grip, so touch gets a 24px hit area centred
    // on the edge. Below three of them the cue has no body left to drag, so the
    // edges take the outer thirds, inside the block, and the middle stays the body.
    readonly property bool narrowTouch: Theme.touchInput && width < 72
    readonly property real edgeHitW: !Theme.touchInput ? gripW
                                     : (narrowTouch ? width / 3 : 24)
    readonly property real bodyInset: !Theme.touchInput ? gripW
                                      : (narrowTouch ? width / 3 : 12)

    property bool dragging: false
    property real dragX: 0
    property real dragW: 0
    property real editStart: 0
    property real editEnd: 0

    height: parent.height - 16
    anchors.verticalCenter: parent.verticalCenter
    x: dragging ? dragX : lane.xForSeconds(start)
    width: Math.max(3, dragging ? dragW
                                 : (end - start) * lane.pxPerSecond)
    radius: Theme.c.subtitleCueBlock.radius
    color: isSelected ? Theme.c.subtitleCueBlock.backgroundSelected
                      : (isActive ? Theme.c.subtitleCueBlock.backgroundActive : Theme.c.subtitleCueBlock.background)
    opacity: isSelected ? 1.0 : (isActive ? 0.85 : 0.5)
    border.width: isSelected ? Theme.c.subtitleCueBlock.borderWidth : 0
    border.color: Theme.c.subtitleCueBlock.border
    z: isSelected ? 20 : 10

    Behavior on opacity { NumberAnimation { duration: 120 } }

    function startEdit() {
        editStart = start
        editEnd = end
        dragX = lane.xForSeconds(editStart)
        dragW = (editEnd - editStart) * lane.pxPerSecond
        dragging = true
        lane.beginDrag()
    }
    function select() {
        EditorState.selectedSubtitleCue = index
        lane.cueTapped(index)
    }
    function moveBy(dx) {
        const d = editEnd - editStart
        const raw = editStart + dx / lane.pxPerSecond
        const snS = lane.snap(raw, index)
        const snE = lane.snap(raw + d, index) - d
        let ns = Math.abs(snS - raw) <= Math.abs(snE - raw) ? snS : snE
        ns = lane.clampVal(ns, lane.loBound(index), lane.hiBound(index) - d)
        dragX = lane.xForSeconds(ns)
        lane.commitCue(index, ns, ns + d)
    }
    function finishEdit() {
        dragging = false
        lane.endDrag()
        EditorState.commitPreviewDrag()
    }

    Text {
        anchors.fill: parent
        anchors.leftMargin: 6
        anchors.rightMargin: 6
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
        text: block.text.length ? block.text : qsTr("(empty)")
        color: Theme.c.subtitleCueBlock.foreground
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSizeXs
        font.italic: !block.text.length
    }

    // Body: click to select, drag to move (keeps duration).
    Item {
        anchors.fill: parent
        anchors.leftMargin: block.bodyInset
        anchors.rightMargin: block.bodyInset

        HoverHandler { cursorShape: Qt.OpenHandCursor }
        TapHandler {
            // Finger taps land on touchMove below, which takes the press.
            acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                             | PointerDevice.Stylus
            onTapped: block.select()
        }
        DragHandler {
            target: null
            acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                             | PointerDevice.Stylus
            xAxis.enabled: true
            yAxis.enabled: false
            onActiveChanged: {
                if (active)
                    block.startEdit()
                else
                    block.finishEdit()
            }
            onTranslationChanged: block.moveBy(translation.x)
        }

        // Touchscreen: a finger on the lane pans it, so a cue only
        // moves after a hold. Mouse presses are rejected for the DragHandler.
        MouseArea {
            id: touchMove
            anchors.fill: parent
            pressAndHoldInterval: Theme.touchLiftInterval
            preventStealing: armed
            property bool armed: false
            property real originX
            onPressed: (mouse) => {
                if (mouse.source === Qt.MouseEventNotSynthesized) {
                    mouse.accepted = false
                    return
                }
                armed = false
            }
            onPressAndHold: (mouse) => {
                originX = mapToItem(null, mouse.x, 0).x
                armed = true
                Haptics.pickUp()
                block.startEdit()
            }
            onPositionChanged: (mouse) => {
                if (armed)
                    block.moveBy(mapToItem(null, mouse.x, 0).x - originX)
            }
            onClicked: block.select()
            onReleased: finish()
            onCanceled: finish()
            function finish() {
                if (!armed)
                    return
                armed = false
                block.finishEdit()
            }
        }
    }

    SubtitleCueEdgeGrip {
        lane: block.lane
        cue: block
        edge: -1
    }
    SubtitleCueEdgeGrip {
        lane: block.lane
        cue: block
        edge: 1
    }
}
