pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import Drift

// The inline editor for one unconnected input, picked by its widget (or its socket type when no
// widget is named). Values go straight to the controller; continuous drags coalesce into one
// undo step. The controls follow the model through Bindings so a half-finished edit is not
// overwritten and an external change still lands.
Item {
    id: root

    required property var port
    required property string nodeId
    required property var controller

    readonly property var value: port.value
    readonly property real from: port.min !== undefined && port.min !== null ? Number(port.min) : 0
    readonly property real to: port.max !== undefined && port.max !== null ? Number(port.max) : 1
    readonly property string kind: {
        if (port.widget)
            return port.widget
        switch (port.type) {
        case "float": return "slider"
        case "bool": return "toggle"
        case "color": return "swatch"
        case "vec2": return "point"
        }
        return ""
    }

    property bool _dragging: false

    function send(v, drag) {
        controller.setInputValue(nodeId, port.id, v, drag && _dragging)
        _dragging = drag
    }

    function endDrag() {
        _dragging = false
    }

    // Lists nested in a model role arrive as sequence wrappers, which Array.isArray rejects.
    function isList(v) {
        return v !== null && typeof v === "object" && typeof v.length === "number"
    }

    function pointValue(i) {
        const v = value
        if (isList(v))
            return Number(v[i] ?? 0)
        return v ? Number(i === 0 ? v.x : v.y) || 0 : 0
    }

    function colorHex(v) {
        if (typeof v === "string")
            return v
        if (isList(v)) {
            const h = (x) => Math.round(Math.max(0, Math.min(1, x)) * 255).toString(16).padStart(2, "0")
            return "#" + h(v.length > 3 ? v[3] : 1) + h(v[0] ?? 0) + h(v[1] ?? 0) + h(v[2] ?? 0)
        }
        return "#ffffffff"
    }

    function optionValue(o) {
        return o !== null && typeof o === "object" ? o.value : o
    }

    function optionLabel(o) {
        return o !== null && typeof o === "object" ? (o.label ?? String(o.value)) : String(o)
    }

    Loader {
        anchors.fill: parent
        sourceComponent: {
            switch (root.kind) {
            case "slider": return sliderControl
            case "angle": return angleControl
            case "point": return pointControl
            case "toggle": return toggleControl
            case "swatch": return swatchControl
            case "seed": return seedControl
            case "choice": return choiceControl
            }
            return null
        }
    }

    Component {
        id: sliderControl

        ThemedSlider {
            id: slider
            from: root.from
            to: root.to
            stepSize: root.port.step ? Number(root.port.step) : 0
            valueFormatter: (v) => Number(v).toFixed(stepSize >= 1 ? 0 : 2)
            Binding on value {
                value: Number(root.value ?? 0)
                when: !slider.pressed
            }
            onMoved: root.send(value, true)
            onPressedChanged: if (!pressed) root.endDrag()
        }
    }

    Component {
        id: angleControl

        Row {
            spacing: Theme.spacingSm

            NodeAngleDial {
                id: dial
                height: parent.height
                width: height
                from: root.port.min !== undefined && root.port.min !== null ? root.from : 0
                to: root.port.max !== undefined && root.port.max !== null ? root.to : 360
                Binding on value {
                    value: Number(root.value ?? 0)
                    when: !dial.pressed
                }
                onMoved: (v) => root.send(Math.round(v), true)
                onPressedChanged: if (!pressed) root.endDrag()
            }

            ThemedNumberField {
                id: field
                width: parent.width - dial.width - parent.spacing
                height: parent.height
                from: dial.from
                to: dial.to
                decimals: 0
                font.pixelSize: Theme.fontSizeXs
                topPadding: 0
                bottomPadding: 0
                verticalAlignment: TextInput.AlignVCenter
                unit: "°"
                Binding on value {
                    value: Number(root.value ?? 0)
                }
                onEdited: (v) => root.send(v, false)
            }
        }
    }

    Component {
        id: pointControl

        Row {
            spacing: Theme.spacingXs

            Repeater {
                model: 2

                ThemedNumberField {
                    id: axis
                    required property int index
                    width: (parent.width - parent.spacing) / 2
                    height: parent.height
                    decimals: 2
                    step: root.port.step ? Number(root.port.step) : 0.01
                    font.pixelSize: Theme.fontSizeXs
                    topPadding: 0
                    bottomPadding: 0
                    verticalAlignment: TextInput.AlignVCenter
                    Binding on value {
                        value: root.pointValue(axis.index)
                    }
                    onEdited: (v) => {
                        const next = [root.pointValue(0), root.pointValue(1)]
                        next[axis.index] = v
                        root.send(next, false)
                    }
                }
            }
        }
    }

    Component {
        id: toggleControl

        ThemedSwitch {
            id: toggle
            Binding on checked {
                value: !!root.value
            }
            onToggled: root.send(checked, false)
        }
    }

    Component {
        id: swatchControl

        ColorSwatchField {
            hex: root.colorHex(root.value)
            onEdited: (h) => {
                if (root.isList(root.value)) {
                    const c = Qt.color(h)
                    root.send([c.r, c.g, c.b, c.a], false)
                } else {
                    root.send(h, false)
                }
            }
        }
    }

    Component {
        id: seedControl

        Row {
            spacing: Theme.spacingXs

            ThemedNumberField {
                id: seedField
                width: parent.width - diceButton.width - parent.spacing
                height: parent.height
                from: 0
                to: 2147483647
                decimals: 0
                font.pixelSize: Theme.fontSizeXs
                topPadding: 0
                bottomPadding: 0
                verticalAlignment: TextInput.AlignVCenter
                Binding on value {
                    value: Number(root.value ?? 0)
                }
                onEdited: (v) => root.send(v, false)
            }

            IconButton {
                id: diceButton
                glyph: Theme.icons.dices
                buttonSize: parent.height
                iconSize: Theme.iconSizeMd
                tooltip: qsTr("Randomize")
                onClicked: root.send(Math.floor(Math.random() * 1000000), false)
            }
        }
    }

    Component {
        id: choiceControl

        ThemedComboBox {
            id: combo
            readonly property var items: root.port.options ?? []
            readonly property int currentFor: {
                for (let i = 0; i < items.length; ++i)
                    if (root.optionValue(items[i]) === root.value)
                        return i
                return -1
            }
            implicitHeight: parent.height
            font.pixelSize: Theme.fontSizeXs
            model: items.map(root.optionLabel)
            Binding on currentIndex {
                value: combo.currentFor
            }
            onActivated: (i) => root.send(root.optionValue(items[i]), false)
        }
    }
}
