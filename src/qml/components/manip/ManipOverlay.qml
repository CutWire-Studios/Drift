import QtQuick
import Drift

// The base of an on-canvas tool: the clip frame mapper, one policy for when to re-read the
// project, the Esc / Enter / arrow keys, and the places the HUD, snap lines, hint and touch
// bar go. Put the tool's own items inside it.
//
// Keys: Esc reverts the drag in flight through EditorState.drag, or closes the HUD's typed
// entry first; Enter commits it; arrows emit `nudged` in 1 unit, Shift = 10, each burst one undo
// step. Typing a digit while dragging opens the HUD entry when it is editable.
Item {
    id: overlay

    readonly property alias frame: frameMapper

    // "settled" refreshes on every edit but waits for a scrub, a playback run or a drag to end,
    // "live" refreshes through them as well, "none" leaves refreshing to the tool.
    property string refreshPolicy: "settled"
    // clipPropertiesPreviewed only refreshes for these keys; a trailing "*" matches a prefix.
    // Empty means any key.
    property var previewKeys: []
    property bool refreshOnClipData: false
    signal refreshRequested()

    readonly property bool interacting: EditorState.drag.interacting
    readonly property bool touchDrag: EditorState.drag.device !== DragSession.Mouse

    // Where the pointer is, in this item's coordinates. Hover keeps it for a mouse; a tool
    // writes it while a finger drags.
    property point pointer

    property string hint: ""
    property bool hintVisible: hint.length > 0
    property bool hudEditable: false
    property string hudUnit: ""

    // Viewport px of the snap lines to draw, set by the tool while a drag is snapped.
    property var snapLinesX: []
    property var snapLinesY: []

    property bool nudgeEnabled: false
    property string nudgeText: ""

    // The touch chips' state, or null. See modifiersFor().
    property var chipModifiers: null

    // Slots. Each is replaceable; null removes it.
    property Component hud: Component {
        LiveReadout {
            visible: EditorState.drag.interacting && text.length > 0
            text: EditorState.drag.hudText
            pointer: overlay.pointer
            editable: overlay.hudEditable
            unit: overlay.hudUnit
            onApplied: (text, value) => overlay.hudApplied(text, value)
            onEntryFinished: overlay.forceActiveFocus()
        }
    }
    property Component touchBar: null
    readonly property var hudItem: hudLoader.item
    readonly property var touchBarItem: touchBarLoader.item

    signal nudged(real dx, real dy)
    signal confirmed()
    signal escaped()
    signal hudApplied(string text, real value)

    // The held keys merged with the chips; never reads keyboard state outside the event.
    function modifiersFor(qtFlags) {
        const k = Manip.fromQtFlags(qtFlags)
        const c = chipModifiers
        return {
            constrain: k.constrain || (c && c.constrain) || false,
            fromCenter: k.fromCenter || (c && c.fromCenter) || false,
            breakTangent: k.breakTangent || (c && c.breakTangent) || false,
            noSnap: k.noSnap || (c && c.noSnap) || false,
            exclude: k.exclude || (c && c.exclude) || false
        }
    }

    function matchesKeys(keys) {
        if (previewKeys.length === 0)
            return true
        for (const k of keys) {
            for (const want of previewKeys) {
                if (want.endsWith("*") ? k.startsWith(want.slice(0, -1)) : k === want)
                    return true
            }
        }
        return false
    }

    ClipFrameMapper {
        id: frameMapper
    }

    Connections {
        target: EditorState
        enabled: overlay.refreshPolicy !== "none"
        function onTracksChanged() { overlay.refreshRequested() }
        function onClipPropertiesPreviewed(trackIndex, clipIndex, keys) {
            if (overlay.matchesKeys(keys))
                overlay.refreshRequested()
        }
        function onSelectionChanged() { overlay.refreshRequested() }
        function onSelectedClipDataChanged() {
            if (overlay.refreshOnClipData)
                overlay.refreshRequested()
        }
        // A scrub moves the playhead per scroll event; settled catches up once when it ends.
        function onPlayheadSecondsChanged() {
            if (overlay.refreshPolicy === "live" || !EditorState.scrubbing)
                overlay.refreshRequested()
        }
        function onScrubbingChanged() {
            if (overlay.refreshPolicy === "live" || !EditorState.scrubbing)
                overlay.refreshRequested()
        }
        function onPlayingChanged() {
            if (overlay.refreshPolicy === "live" || !EditorState.playing)
                overlay.refreshRequested()
        }
    }

    HoverHandler {
        acceptedDevices: PointerDevice.Mouse | PointerDevice.Stylus
        onPointChanged: overlay.pointer = point.position
    }

    Repeater {
        model: overlay.snapLinesX

        Rectangle {
            required property real modelData
            x: modelData - width / 2
            y: 0
            width: Theme.c.manipSnapLine.strokeWidth
            height: overlay.height
            color: Theme.c.manipSnapLine.background
        }
    }

    Repeater {
        model: overlay.snapLinesY

        Rectangle {
            required property real modelData
            x: 0
            y: modelData - height / 2
            width: overlay.width
            height: Theme.c.manipSnapLine.strokeWidth
            color: Theme.c.manipSnapLine.background
        }
    }

    Loader {
        id: hudLoader
        anchors.fill: parent
        sourceComponent: overlay.hud
    }

    Rectangle {
        id: hintPill
        visible: overlay.hintVisible && !overlay.interacting
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        anchors.topMargin: Theme.spacingLg
        width: Math.min(parent.width * 0.8, hintText.implicitWidth + Theme.spacingLg * 2)
        height: hintText.implicitHeight + Theme.spacingSm * 2
        radius: Theme.c.manipHint.radius
        color: Theme.c.manipHint.background
        border.width: Theme.c.manipHint.borderWidth
        border.color: Theme.c.manipHint.border
        z: 90

        Text {
            id: hintText
            anchors.fill: parent
            anchors.margins: Theme.spacingSm
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
            text: overlay.hint
            color: Theme.c.manipHint.foreground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeXs
        }
    }

    Loader {
        id: touchBarLoader
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: Theme.spacingLg
        z: 95
        sourceComponent: overlay.touchBar
    }

    Keys.onPressed: (event) => {
        const drag = EditorState.drag
        if (event.key === Qt.Key_Escape) {
            if (hudItem && hudItem.entering) {
                hudItem.cancelEntry()
            } else if (drag.interacting) {
                drag.cancel()
            } else {
                overlay.escaped()
                return
            }
            event.accepted = true
        } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
            if (drag.interacting)
                drag.commit()
            overlay.confirmed()
            event.accepted = true
        } else if (overlay.nudgeEnabled && !(drag.interacting && drag.kind !== "nudge")
                   && (event.key === Qt.Key_Left || event.key === Qt.Key_Right
                       || event.key === Qt.Key_Up || event.key === Qt.Key_Down)) {
            const step = (event.modifiers & Qt.ShiftModifier) ? 10 : 1
            const dx = event.key === Qt.Key_Left ? -step : event.key === Qt.Key_Right ? step : 0
            const dy = event.key === Qt.Key_Up ? -step : event.key === Qt.Key_Down ? step : 0
            drag.nudge(overlay.nudgeText)
            overlay.nudged(dx, dy)
            event.accepted = true
        } else if (drag.interacting && hudItem && hudItem.editable && !hudItem.entering
                   && event.text.length === 1 && "0123456789-.".indexOf(event.text) >= 0) {
            hudItem.beginEntry(event.text)
            event.accepted = true
        }
    }
}
