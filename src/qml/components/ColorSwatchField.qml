import QtQuick
import Drift

// Colour swatch that opens a picker, paired with a hex field for exact values.
Row {
    id: root

    // #RRGGBB or #AARRGGBB.
    property string hex: "#ffffffff"
    property string tooltip: qsTr("Choose colour")
    // Emitted once the user commits, never on every keystroke.
    signal edited(string value)
    // With livePreview set, the open picker's colour is sent through previewed() inside one
    // preview drag, so the caller can apply it without an undo step: OK commits the final colour
    // through edited(), closing any other way reverts.
    property bool livePreview: false
    signal previewed(string value)
    signal eyedropperStarted()
    signal eyedropperEnded()

    spacing: 6

    function _toHex(c) {
        const pad = function (v) {
            const h = Math.round(v * 255).toString(16)
            return h.length === 1 ? "0" + h : h
        }
        return "#" + pad(c.a) + pad(c.r) + pad(c.g) + pad(c.b)
    }

    ColorSwatchButton {
        hex: root.hex
        tip: root.tooltip
        onActivated: {
            Haptics.press()
            const dialog = colorDialogLoader.ensure()
            dialog.selectedColor = root.hex
            dialog.open()
        }
    }

    ThemedTextField {
        id: hexField
        width: 92
        text: root.hex
        color: Theme.c.colorSwatchField.foreground
        font.family: Theme.monoFontFamily
        font.pixelSize: Theme.fontSizeSm
        // Rejects malformed input instead of silently applying a typo.
        readonly property bool validHex: /^#([0-9a-fA-F]{6}|[0-9a-fA-F]{8})$/.test(text)
        errorText: validHex || text.length === 0 ? "" : qsTr("Enter a color like #FF0000")
        onEditingFinished: if (validHex) root.edited(text)
    }

    // The field is only rebound when it is not being typed into, so a project signal cannot wipe
    // a half-entered value.
    Connections {
        target: root
        function onHexChanged() {
            if (!hexField.activeFocus)
                hexField.text = root.hex
        }
    }

    LazyLoader {
        id: colorDialogLoader
        sourceComponent: Component {
            ThemedColorDialog {
                title: qsTr("Select Color")
                property bool _live: false
                property bool _previewing: false
                function _endPreview() {
                    _live = false
                    if (_previewing) {
                        _previewing = false
                        EditorState.cancelPreviewDrag()
                    }
                }
                onOpened: _live = root.livePreview
                onSelectedColorChanged: {
                    if (!_live)
                        return
                    if (!_previewing) {
                        _previewing = true
                        EditorState.beginPreviewDrag(qsTr("Edit colour"))
                    }
                    root.previewed(root._toHex(selectedColor))
                }
                onClosed: _endPreview()
                onAccepted: {
                    _endPreview()
                    root.edited(root._toHex(selectedColor))
                }
                onEyedropperStarted: root.eyedropperStarted()
                onEyedropperEnded: root.eyedropperEnded()
            }
        }
    }
}
