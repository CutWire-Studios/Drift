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

    Rectangle {
        width: Theme.spacing3xl
        height: Theme.spacing3xl
        radius: Theme.radiusSm
        anchors.verticalCenter: parent.verticalCenter
        color: root.hex
        border.width: swatchMouse.containsMouse ? Theme.borderWidthFocus : Theme.borderWidth
        border.color: swatchMouse.containsMouse ? Theme.primary : Theme.panelBorder

        Behavior on border.color {
            ColorAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
        }

        ThemedToolTip {
            text: root.tooltip
            visible: swatchMouse.containsMouse
        }

        MouseArea {
            id: swatchMouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: {
                Haptics.press()
                const dialog = colorDialogLoader.ensure()
                dialog.selectedColor = root.hex
                dialog.open()
            }
            onWheel: (wheel) => { wheel.accepted = false }
        }
    }

    ThemedTextField {
        id: hexField
        width: 92
        text: root.hex
        color: Theme.panelForeground
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
                onAccepted: root.edited(root._toHex(selectedColor))
                onEyedropperStarted: root.eyedropperStarted()
                onEyedropperEnded: root.eyedropperEnded()
            }
        }
    }
}
