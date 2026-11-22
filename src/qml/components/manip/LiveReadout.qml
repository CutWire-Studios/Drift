import QtQuick
import Drift

// A value next to the pointer while something is dragged. On touch it floats above the finger
// instead of under it. With `editable` set, typing a digit opens an entry: Enter applies, Esc
// closes it without applying. ManipOverlay forwards the first key; a tool can also call beginEntry().
Rectangle {
    id: readout

    property string text
    // Where the pointer is, in the parent's coordinates.
    property point pointer
    property bool touch: EditorState.drag.device !== DragSession.Mouse
    property bool editable: false
    property string unit
    // What the entry starts with; empty starts blank.
    property string entrySeed

    readonly property bool entering: entry.visible

    signal applied(string text, real value)
    signal entryFinished()
    // Tab while typing: a tool with two values to type (time and value) switches the field.
    signal tabPressed()

    function beginEntry(seed) {
        if (!editable)
            return
        entry.text = seed !== undefined ? seed : entrySeed
        entry.visible = true
        entry.forceActiveFocus()
        entry.cursorPosition = entry.text.length
    }

    function cancelEntry() {
        if (!entry.visible)
            return
        entry.visible = false
        entryFinished()
    }

    readonly property real liftPx: Theme.androidMinTouchTarget * 1.25
    x: Math.max(0, Math.min(parent ? parent.width - width : 0, touch ? pointer.x - width / 2 : pointer.x + Theme.spacingLg))
    y: {
        const maxY = parent ? parent.height - height : 0
        if (!touch)
            return Math.max(0, Math.min(maxY, pointer.y + Theme.spacingLg))
        const above = pointer.y - liftPx - height
        return Math.max(0, Math.min(maxY, above >= 0 ? above : pointer.y + liftPx * 0.6))
    }
    width: Math.max(label.implicitWidth, entry.visible ? entry.implicitWidth + unitLabel.implicitWidth : 0)
           + Theme.spacingMd * 2
    height: Math.max(label.implicitHeight, entry.implicitHeight) + Theme.spacingSm * 2
    radius: Theme.c.manipHud.radius
    color: Theme.c.manipHud.background
    border.width: Theme.c.manipHud.borderWidth
    border.color: entry.visible ? Theme.c.manipHud.inputBorder : Theme.c.manipHud.border
    z: 100

    // A finger has no digit keys: tapping the value (with another finger, while one is dragging) opens the entry.
    TapHandler {
        enabled: readout.editable && readout.touch && !readout.entering
        onTapped: readout.beginEntry()
    }

    Text {
        id: label
        anchors.centerIn: parent
        visible: !entry.visible
        text: readout.text
        color: Theme.c.manipHud.foreground
        font.family: Theme.monoFontFamily
        font.pixelSize: Theme.fontSizeXs
    }

    Row {
        anchors.centerIn: parent
        visible: entry.visible

        TextInput {
            id: entry
            visible: false
            color: Theme.c.manipHud.foreground
            selectionColor: Theme.c.manipHud.inputBorder
            font.family: Theme.monoFontFamily
            font.pixelSize: Theme.fontSizeXs
            inputMethodHints: Qt.ImhFormattedNumbersOnly
            validator: RegularExpressionValidator { regularExpression: /^-?\d*\.?\d*$/ }
            onAccepted: {
                const value = parseFloat(text)
                visible = false
                if (!isNaN(value))
                    readout.applied(text, value)
                readout.entryFinished()
            }
            Keys.onEscapePressed: (event) => {
                readout.cancelEntry()
                event.accepted = true
            }
            Keys.onTabPressed: (event) => {
                readout.tabPressed()
                event.accepted = true
            }
        }

        Text {
            id: unitLabel
            text: readout.unit
            color: Theme.c.manipHud.foregroundMuted
            font.family: Theme.monoFontFamily
            font.pixelSize: Theme.fontSizeXs
        }
    }
}
