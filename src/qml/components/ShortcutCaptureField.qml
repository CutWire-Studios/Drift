import QtQuick
import QtQuick.Controls.Basic
import Drift

// Click or focus and press Space/Enter to arm, then press a chord (modifiers + key).
// Esc cancels; Backspace clears.
//
// Built on AbstractButton rather than a bare Rectangle so it joins the tab chain and
// reports a role — as a plain Rectangle it was unreachable by keyboard, which made the
// keyboard-settings panel itself impossible to use from the keyboard.
AbstractButton {
    id: root

    property string actionId
    property string shortcut
    property bool capturing: false

    implicitWidth: 120
    implicitHeight: 26
    width: 120
    height: 26
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus

    Accessible.role: Accessible.Button
    Accessible.name: qsTr("Shortcut for %1").arg(root.actionId)
    Accessible.description: root.shortcut.length > 0 ? Theme.shortcutDisplay(root.shortcut) : qsTr("Not set")
    Accessible.onPressAction: root.arm()

    function arm() {
        root.capturing = true
        root.forceActiveFocus()
    }

    onClicked: root.arm()

    background: Rectangle {
        radius: Theme.radiusSm
        color: root.capturing ? Theme.panelSecondaryBg
                              : (root.hovered ? Theme.popoverHover : Theme.panelAccent)
        border.width: root.capturing || root.visualFocus ? Theme.borderWidthFocus : 1
        border.color: root.capturing ? Theme.primary
                                     : (root.visualFocus ? Theme.focusRing : Theme.panelBorder)

        Behavior on color {
            ColorAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
        }
        Behavior on border.color {
            ColorAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
        }
    }

    function keyName(key) {
        const named = ({
            0x01000000: "Escape",
            0x01000003: "Backspace",
            0x01000004: "Return",
            0x01000005: "Enter",
            0x01000006: "Insert",
            0x01000007: "Delete",
            0x01000010: "Home",
            0x01000011: "End",
            0x01000012: "Left",
            0x01000013: "Up",
            0x01000014: "Right",
            0x01000015: "Down",
            0x01000016: "PageUp",
            0x01000017: "PageDown",
            0x01000020: "Shift",
            0x01000021: "Control",
            0x01000023: "Alt",
            0x01000024: "Meta",
            0x20: "Space",
            0x09: "Tab"
        })
        if (named[key])
            return named[key]
        if (key >= Qt.Key_F1 && key <= Qt.Key_F12)
            return "F" + (key - Qt.Key_F1 + 1)
        if (key >= Qt.Key_0 && key <= Qt.Key_9)
            return String.fromCharCode(key)
        if (key >= Qt.Key_A && key <= Qt.Key_Z)
            return String.fromCharCode(key)
        return ""
    }

    function chordFromEvent(event) {
        const name = keyName(event.key)
        if (!name || name === "Shift" || name === "Control" || name === "Alt" || name === "Meta")
            return ""

        let parts = []

        // Qt already normalizes Apple modifiers:
        //
        // ControlModifier = Command on macOS
        // MetaModifier    = physical Control on macOS
        //
        // Store exactly Qt's canonical names. This keeps Shortcut {}
        // execution and the persisted shortcut map consistent.
        if (event.modifiers & Qt.ControlModifier)
            parts.push("Ctrl")
        if (event.modifiers & Qt.AltModifier)
            parts.push("Alt")
        if (event.modifiers & Qt.ShiftModifier)
            parts.push("Shift")
        if (event.modifiers & Qt.MetaModifier)
            parts.push("Meta")

        parts.push(name)
        return parts.join("+")
    }

    contentItem: Text {
        anchors.margins: 4
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
        text: root.capturing ? qsTr("Press keys…")
                             : (root.shortcut.length > 0
                                ? Theme.shortcutDisplay(root.shortcut)
                                : qsTr("Click to set"))
        color: root.capturing ? Theme.primary : Theme.panelForeground
        font.family: Theme.monoFontFamily
        font.pixelSize: Theme.fontSizeXs
    }

    Keys.onPressed: function(event) {
        // Space/Enter arm the field for keyboard users; AbstractButton would
        // otherwise consume them as an activation while already armed.
        if (!root.capturing) {
            if (event.key === Qt.Key_Space || event.key === Qt.Key_Return
                    || event.key === Qt.Key_Enter) {
                root.arm()
                event.accepted = true
            }
            return
        }

        if (event.key === Qt.Key_Escape) {
            root.capturing = false
            event.accepted = true
            return
        }
        if (event.key === Qt.Key_Backspace || event.key === Qt.Key_Delete) {
            EditorState.setShortcut(root.actionId, "")
            root.capturing = false
            event.accepted = true
            return
        }

        const chord = root.chordFromEvent(event)
        if (chord.length === 0)
            return

        // Non-empty means refused: the chord already belongs to another action, and
        // binding it twice would make Qt fire neither.
        const clash = EditorState.setShortcut(root.actionId, chord)
        if (clash.length > 0)
            Toasts.warning(qsTr("“%1” is already used by %2.")
                           .arg(Theme.shortcutDisplay(chord)).arg(clash))
        root.capturing = false
        event.accepted = true
    }

    onActiveFocusChanged: {
        if (!activeFocus && root.capturing)
            root.capturing = false
    }

    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.NoButton
        cursorShape: Qt.PointingHandCursor
        onWheel: (wheel) => { wheel.accepted = false }
    }
}
