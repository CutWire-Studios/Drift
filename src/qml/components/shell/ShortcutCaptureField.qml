import QtQuick
import QtQuick.Controls.Basic
import Drift

// The "+" that adds a binding. Click or focus and press Space/Enter to arm, then press a chord
// (modifiers + key). Esc cancels; every other key, Backspace and Delete included, is the chord.
//
// Built on AbstractButton rather than a bare Rectangle so it joins the tab chain and
// reports a role — as a plain Rectangle it was unreachable by keyboard, which made the
// keyboard-settings panel itself impossible to use from the keyboard.
AbstractButton {
    id: root

    property string actionId
    property string actionLabel
    property bool capturing: false

    implicitWidth: capturing ? Theme.c.shortcutCaptureField.capturingWidth
                             : Theme.c.shortcutCaptureField.idleWidth
    implicitHeight: Theme.c.shortcutCaptureField.height
    width: implicitWidth
    height: implicitHeight
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus

    Accessible.role: Accessible.Button
    Accessible.name: qsTr("Add shortcut for %1").arg(root.actionLabel)
    Accessible.onPressAction: root.arm()

    function arm() {
        root.capturing = true
        root.forceActiveFocus()
    }

    onClicked: root.arm()
    onCapturingChanged: Gamepad.capturing = capturing

    // A controller button is a chord too ("Pad+RB").
    Connections {
        target: Gamepad
        enabled: root.capturing

        function onButtonPressed(chord) {
            const clash = EditorState.keymap.addBinding(root.actionId, chord)
            if (clash.length > 0)
                Toasts.warning(qsTr("“%1” is already used by %2.")
                               .arg(EditorState.keymap.displayText(chord)).arg(clash))
            root.capturing = false
        }
    }

    background: Rectangle {
        radius: Theme.c.shortcutCaptureField.radius
        color: root.capturing ? Theme.c.shortcutCaptureField.backgroundCapturing
                              : (root.hovered ? Theme.c.shortcutCaptureField.backgroundHover
                                             : Theme.c.shortcutCaptureField.background)
        border.width: root.capturing || root.visualFocus ? Theme.c.shortcutCaptureField.borderWidthFocus
                                                         : Theme.c.shortcutCaptureField.borderWidth
        border.color: root.capturing ? Theme.c.shortcutCaptureField.borderCapturing
                                     : (root.visualFocus ? Theme.c.shortcutCaptureField.borderFocus
                                                         : Theme.c.shortcutCaptureField.border)

        Behavior on color {
            ColorAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
        }
        Behavior on border.color {
            ColorAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
        }
    }

    function chordFromEvent(event) {
        return EditorState.keymap.chordFromKey(event.key, event.modifiers, event.nativeScanCode, event.text)
    }

    // Without this an app-wide shortcut such as Delete or Ctrl+E would fire instead of being captured.
    Keys.onShortcutOverride: function(event) {
        if (root.capturing)
            event.accepted = true
    }

    contentItem: Text {
        anchors.margins: 4
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
        text: root.capturing ? (Gamepad.connected ? qsTr("Press keys or a button…") : qsTr("Press keys…")) : "+"
        color: root.capturing ? Theme.c.shortcutCaptureField.foregroundCapturing
                              : Theme.c.shortcutCaptureField.foreground
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
        const chord = root.chordFromEvent(event)
        if (chord.length === 0)
            return

        // Non-empty means refused: the chord already belongs to another action in this scope.
        const clash = EditorState.keymap.addBinding(root.actionId, chord)
        if (clash.length > 0)
            Toasts.warning(qsTr("“%1” is already used by %2.")
                           .arg(EditorState.keymap.displayText(chord)).arg(clash))
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
