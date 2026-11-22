import QtQuick
import QtQuick.Controls.Basic
import Drift

// Numeric inspector field: drag sideways to scrub, click to type; arrow up/down
// and the wheel nudge while focused. Commits on focus loss or Enter. Avoid binding `text` to the model — set `value` from refresh logic
// while the field is not focused.
//
// Out-of-range and unparseable input used to be corrected silently, so a user's
// typing simply vanished with no explanation. Both cases now flash the field and
// say what happened, so the limit is learnable.
ThemedTextField {
    id: root

    colorTokens: Theme.c.themedNumberField
    backgroundSlot: "ThemedNumberFieldBackground"

    property real value: 0
    property real from: -1e9
    property real to: 1e9
    property real step: 1
    property int decimals: 0
    // Appended to the range hint, e.g. "px", "s", "%".
    property string unit: ""

    signal edited(real value)

    inputMethodHints: Qt.ImhFormattedNumbersOnly
    font.family: Theme.touchInput && Theme.compact ? Theme.fontFamily : Theme.monoFontFamily

    readonly property bool _bounded: from > -1e9 || to < 1e9
    readonly property string rangeHint: _bounded
        ? qsTr("Allowed range: %1 – %2%3").arg(format(from)).arg(format(to))
                                          .arg(unit.length > 0 ? " " + unit : "")
        : ""

    function clamp(v) {
        return Math.min(to, Math.max(from, v))
    }

    function format(v) {
        if (decimals > 0)
            return Number(v).toFixed(decimals)
        return String(Math.round(v))
    }

    function applyValue(v) {
        const clamped = clamp(v)
        // Surface the correction rather than performing it silently.
        if (Math.abs(clamped - v) > 1e-9) {
            _flash(rangeHint.length > 0 ? rangeHint
                                        : qsTr("Value clamped to %1").arg(format(clamped)))
            Haptics.boundary()
        }
        const changed = decimals === 0
                ? (Math.round(clamped) !== Math.round(value))
                : (Math.abs(clamped - value) > 1e-9)
        value = clamped
        text = format(clamped)
        if (changed)
            edited(clamped)
    }

    function parseAndCommit() {
        const v = parseFloat(text)
        if (isNaN(v)) {
            _flash(qsTr("Enter a number"))
            Haptics.error()
            text = format(value)
            return
        }
        applyValue(v)
    }

    // Scrubs clamp quietly: flashing the range hint on every move past the end is noise.
    property real _scrubStart: 0

    function _scrubTo(v) {
        let next = clamp(v)
        next = decimals > 0 ? Number(next.toFixed(decimals)) : Math.round(next)
        if (Math.abs(next - value) < 1e-9)
            return
        value = next
        text = format(next)
        edited(next)
    }

    ScrubArea {
        anchors.fill: parent
        enabled: !root.readOnly
        editing: root.activeFocus
        onScrubStarted: {
            root._scrubStart = root.value
            EditorState.beginEditGroup()
        }
        onScrubbed: steps => root._scrubTo(root._scrubStart + steps * root.step)
        onScrubFinished: EditorState.endEditGroup(qsTr("Edit value"))
        onActivated: {
            root.forceActiveFocus()
            root.selectAll()
        }
        onWheelStepped: steps => root.applyValue(root.value + steps * root.step)
    }

    // --- Correction feedback ---------------------------------------------------
    property string _flashMessage: ""

    function _flash(message) {
        _flashMessage = message
        flashTimer.restart()
    }

    Timer {
        id: flashTimer
        interval: 2200
        onTriggered: root._flashMessage = ""
    }

    // Reuses the inherited error chrome for a moment, then clears itself.
    errorText: _flashMessage

    // Static range hint on hover/focus, so the bound is discoverable *before*
    // the user hits it.
    ThemedToolTip {
        text: root.rangeHint
        visible: root.rangeHint.length > 0 && root._flashMessage.length === 0
                 && (root.hovered || root.activeFocus)
    }

    Keys.onUpPressed: function(event) {
        applyValue(value + step)
        event.accepted = true
    }

    Keys.onDownPressed: function(event) {
        applyValue(value - step)
        event.accepted = true
    }

    Keys.onPressed: function(event) {
        if (event.modifiers & Qt.ShiftModifier) {
            if (event.key === Qt.Key_Up) {
                applyValue(value + step * 10)
                event.accepted = true
            } else if (event.key === Qt.Key_Down) {
                applyValue(value - step * 10)
                event.accepted = true
            }
        }
    }

    onActiveFocusChanged: if (!activeFocus)
        parseAndCommit()
    onEditingFinished: parseAndCommit()

    onValueChanged: if (!activeFocus)
        text = format(value)

    Component.onCompleted: text = format(value)
}
