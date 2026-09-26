import QtQuick
import Drift

// Themed colour picker: saturation/value field, hue strip and a hex field. Replaces the Qt
// Quick Dialogs ColorDialog, whose non-native fallback ignored the app theme and drew no
// background at all on Android. Set selectedColor, call open(), read selectedColor on accepted.
ThemedDialog {
    id: root

    property color selectedColor: "white"

    // Around an eyedropper pick, so a caller can bypass whatever would hide the colour being
    // picked (a chroma key samples its own keyed-out result otherwise).
    signal eyedropperStarted()
    signal eyedropperEnded()

    // Hue is held apart from the colour: a grey or black has no hue, and deriving it back
    // would snap the hue strip to red whenever saturation or value reached zero.
    property real _hue: 0
    property real _sat: 0
    property real _val: 1
    property real _alpha: 1
    property color _initial: "white"

    preferredWidth: Theme.dialogWidthSm

    function _hex(c) {
        const pad = function (v) {
            const h = Math.round(v * 255).toString(16)
            return h.length === 1 ? "0" + h : h
        }
        return "#" + (c.a < 1 ? pad(c.a) : "") + pad(c.r) + pad(c.g) + pad(c.b)
    }

    function _load(c) {
        if (c.hsvHue >= 0)
            _hue = c.hsvHue
        _sat = c.hsvSaturation
        _val = c.hsvValue
        _alpha = c.a
        _apply()
    }

    function _apply() {
        selectedColor = Qt.hsva(_hue, _sat, _val, _alpha)
        if (!hexField.activeFocus)
            hexField.text = _hex(selectedColor)
    }

    function _startEyedropper() {
        hexField.focus = false
        // Faded rather than closed: a LazyLoader-held dialog is destroyed once it hides.
        root.opacity = 0
        root.dim = false
        root.eyedropperStarted()
        eyedropper.open()
    }

    onAboutToShow: {
        _initial = selectedColor
        _load(selectedColor)
    }

    contentItem: Column {
        spacing: Theme.spacingXl

        Item {
            id: field
            width: parent.width
            height: Math.round(width * 0.62)

            Rectangle {
                anchors.fill: parent
                radius: Theme.radiusSm
                color: Qt.hsva(root._hue, 1, 1, 1)
            }
            Rectangle {
                anchors.fill: parent
                radius: Theme.radiusSm
                gradient: Gradient {
                    orientation: Gradient.Horizontal
                    GradientStop { position: 0; color: "#ffffffff" }
                    GradientStop { position: 1; color: "#00ffffff" }
                }
            }
            Rectangle {
                anchors.fill: parent
                radius: Theme.radiusSm
                border.width: Theme.borderWidth
                border.color: Qt.rgba(0, 0, 0, 0.12)
                gradient: Gradient {
                    GradientStop { position: 0; color: "#00000000" }
                    GradientStop { position: 1; color: "#ff000000" }
                }
            }

            ColorThumb {
                x: root._sat * field.width - width / 2
                y: (1 - root._val) * field.height - height / 2
                fill: Qt.hsva(root._hue, root._sat, root._val, 1)
                active: fieldMouse.pressed
            }

            MouseArea {
                id: fieldMouse
                anchors.fill: parent
                preventStealing: true
                cursorShape: Qt.CrossCursor
                function pick(mouse) {
                    root._sat = Math.max(0, Math.min(1, mouse.x / width))
                    root._val = 1 - Math.max(0, Math.min(1, mouse.y / height))
                    root._apply()
                }
                onPressed: (mouse) => { hexField.focus = false; pick(mouse) }
                onPositionChanged: (mouse) => pick(mouse)
            }
        }

        Item {
            id: hueStrip
            width: parent.width
            height: Theme.touchUi ? 28 : 20

            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                width: parent.width
                height: 12
                radius: height / 2
                gradient: Gradient {
                    orientation: Gradient.Horizontal
                    GradientStop { position: 0 / 6; color: "#ff0000" }
                    GradientStop { position: 1 / 6; color: "#ffff00" }
                    GradientStop { position: 2 / 6; color: "#00ff00" }
                    GradientStop { position: 3 / 6; color: "#00ffff" }
                    GradientStop { position: 4 / 6; color: "#0000ff" }
                    GradientStop { position: 5 / 6; color: "#ff00ff" }
                    GradientStop { position: 6 / 6; color: "#ff0000" }
                }
            }

            ColorThumb {
                x: root._hue * (hueStrip.width - width)
                anchors.verticalCenter: parent.verticalCenter
                fill: Qt.hsva(root._hue, 1, 1, 1)
                active: hueMouse.pressed
            }

            MouseArea {
                id: hueMouse
                anchors.fill: parent
                preventStealing: true
                function pick(mouse) {
                    root._hue = Math.max(0, Math.min(1, mouse.x / width))
                    root._apply()
                }
                onPressed: (mouse) => { hexField.focus = false; pick(mouse) }
                onPositionChanged: (mouse) => pick(mouse)
            }
        }

        Row {
            width: parent.width
            spacing: Theme.spacingLg

            // Before/after pair; tapping the original half restores it.
            Rectangle {
                id: compare
                width: Theme.controlHeight * 2
                height: Theme.controlHeight
                radius: Theme.radiusSm
                clip: true
                color: "transparent"
                border.width: Theme.borderWidth
                border.color: Theme.panelBorder

                Row {
                    anchors.fill: parent
                    anchors.margins: Theme.borderWidth
                    Rectangle {
                        width: parent.width / 2
                        height: parent.height
                        color: root._initial
                        topLeftRadius: Theme.radiusSm - 1
                        bottomLeftRadius: Theme.radiusSm - 1

                        ThemedToolTip {
                            text: qsTr("Original colour")
                            visible: initialMouse.containsMouse
                        }
                        MouseArea {
                            id: initialMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                Haptics.press()
                                root._load(root._initial)
                            }
                        }
                    }
                    Rectangle {
                        width: parent.width / 2
                        height: parent.height
                        color: root.selectedColor
                        topRightRadius: Theme.radiusSm - 1
                        bottomRightRadius: Theme.radiusSm - 1
                    }
                }
            }

            ThemedTextField {
                id: hexField
                width: parent.width - compare.width - pickButton.width - parent.spacing * 2
                readonly property bool validHex: /^#?([0-9a-fA-F]{6}|[0-9a-fA-F]{8})$/.test(text.trim())
                errorText: validHex || text.length === 0 ? "" : qsTr("Enter a color like #FF0000")
                inputMethodHints: Qt.ImhNoPredictiveText | Qt.ImhNoAutoUppercase
                Accessible.name: qsTr("Hex colour")
                onTextEdited: {
                    if (!validHex)
                        return
                    const t = text.trim()
                    root._load(Qt.color(t.startsWith("#") ? t : "#" + t))
                }
                onEditingFinished: text = root._hex(root.selectedColor)
            }

            IconButton {
                id: pickButton
                glyph: Theme.icons.pipette
                variant: "ghost"
                buttonSize: Theme.controlHeight
                tooltip: qsTr("Pick a colour from the window")
                onClicked: root._startEyedropper()
            }
        }
    }

    ColorEyedropper {
        id: eyedropper
        onPicked: (value) => {
            const a = root._alpha
            root._load(value)
            root._alpha = a
            root._apply()
        }
        onClosed: {
            root.eyedropperEnded()
            root.dim = true
            root.opacity = 1
        }
    }

    component ColorThumb: Rectangle {
        property color fill
        property bool active: false

        width: Theme.touchUi ? 24 : 16
        height: width
        radius: width / 2
        color: fill
        border.width: 2
        border.color: "white"
        scale: active ? 1.15 : 1

        Behavior on scale {
            NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing }
        }

        // Outer hairline so the white ring still reads on white and pale colours.
        Rectangle {
            anchors.fill: parent
            anchors.margins: -1
            radius: width / 2
            color: "transparent"
            border.width: 1
            border.color: Qt.rgba(0, 0, 0, 0.35)
        }
    }
}
