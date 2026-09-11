import QtQuick
import Drift
import ".."

// Generic editor for a typed parameter list: one control per spec, picked by `type`.
//
// `specs` is the schema ([{id, label?, type, unit?, min?, max?, step?, default?, values?}]) and
// `values` the current plain values keyed by id. Scalars with a finite range become a slider whose
// drags are reported through previewChanged between dragStarted/dragEnded so the caller can
// coalesce them into one undo step; everything else commits through changed().
Column {
    id: root

    property var specs: []
    property var values: ({})
    // Show a Reset chip on rows whose value is set; the caller decides what a reset means.
    property bool showReset: false

    signal changed(string id, var value)
    signal previewChanged(string id, var value)
    signal reset(string id)
    signal dragStarted()
    signal dragEnded()

    spacing: Theme.spacingMd

    function valueOf(spec) {
        const v = root.values ? root.values[spec.id] : undefined
        if (v !== undefined && v !== null)
            return v
        return spec["default"]
    }

    function isSet(spec) {
        return !!root.values && root.values[spec.id] !== undefined && root.values[spec.id] !== null
    }

    function labelOf(spec) {
        if (spec.label && spec.label.length > 0)
            return spec.label
        return spec.id + " · " + spec.type
    }

    Repeater {
        model: root.specs
        delegate: Column {
            id: slotRow
            required property var modelData
            readonly property var spec: modelData
            readonly property var current: root.valueOf(spec)
            readonly property bool ranged: spec.type === "scalar"
                                           && spec.min !== undefined && spec.max !== undefined
                                           && isFinite(Number(spec.min)) && isFinite(Number(spec.max))
                                           && Number(spec.max) > Number(spec.min)
            width: parent.width
            spacing: 4

            Row {
                width: parent.width
                spacing: 6

                Text {
                    text: root.labelOf(slotRow.spec)
                    color: Theme.mutedForeground
                    font.pixelSize: Theme.fontSizeXs
                    font.family: Theme.fontFamily
                    anchors.verticalCenter: parent.verticalCenter
                }
                Text {
                    visible: slotRow.ranged
                    text: {
                        const v = Number(slotRow.current)
                        const step = Number(slotRow.spec.step) || 0
                        const decimals = step > 0 && step < 1 ? Math.min(3, Math.ceil(-Math.log10(step))) : 0
                        return v.toFixed(decimals) + (slotRow.spec.unit || "")
                    }
                    color: Theme.mutedForeground
                    font.pixelSize: Theme.fontSizeXs
                    font.family: Theme.monoFontFamily
                    anchors.verticalCenter: parent.verticalCenter
                }
                ThemedChip {
                    visible: root.showReset && root.isSet(slotRow.spec)
                    text: qsTr("Reset")
                    onClicked: root.reset(slotRow.spec.id)
                }
            }

            ThemedSlider {
                id: scalarSlider
                visible: slotRow.ranged
                width: parent.width
                label: root.labelOf(slotRow.spec)
                from: Number(slotRow.spec.min)
                to: Number(slotRow.spec.max)
                stepSize: Number(slotRow.spec.step) || 0
                valueFormatter: function (v) {
                    const step = Number(slotRow.spec.step) || 0
                    const decimals = step > 0 && step < 1 ? Math.min(3, Math.ceil(-Math.log10(step))) : 0
                    return Number(v).toFixed(decimals) + (slotRow.spec.unit || "")
                }
                Binding on value {
                    when: !scalarSlider.pressed
                    value: Number(slotRow.current)
                }
                onMoved: {
                    if (pressed)
                        root.previewChanged(slotRow.spec.id, value)
                    else
                        root.changed(slotRow.spec.id, value)
                }
                onPressedChanged: {
                    if (pressed)
                        root.dragStarted()
                    else
                        root.dragEnded()
                }
            }

            ThemedNumberField {
                id: scalarField
                visible: slotRow.spec.type === "scalar" && !slotRow.ranged
                width: parent.width
                decimals: 2
                step: Number(slotRow.spec.step) || 0.1
                unit: slotRow.spec.unit || ""
                Binding on value {
                    when: !scalarField.activeFocus
                    value: Number(slotRow.current) || 0
                }
                onEdited: v => root.changed(slotRow.spec.id, v)
            }

            ColorSwatchField {
                visible: slotRow.spec.type === "color"
                hex: slotRow.current !== undefined ? String(slotRow.current) : "#ffffffff"
                tooltip: root.labelOf(slotRow.spec)
                onEdited: value => root.changed(slotRow.spec.id, value)
            }

            ThemedTextField {
                visible: slotRow.spec.type === "text"
                width: parent.width
                text: slotRow.current !== undefined ? String(slotRow.current) : ""
                placeholderText: qsTr("Text for this slot")
                onEditingFinished: root.changed(slotRow.spec.id, text)
            }

            Row {
                visible: slotRow.spec.type === "vec2"
                width: parent.width
                spacing: 8
                ThemedNumberField {
                    id: vecX
                    width: (parent.width - parent.spacing) / 2
                    decimals: 2
                    step: 1
                    Binding on value {
                        when: !vecX.activeFocus
                        value: slotRow.current !== undefined ? Number(slotRow.current[0]) : 0
                    }
                    onEdited: v => root.changed(slotRow.spec.id, [v, vecY.value])
                }
                ThemedNumberField {
                    id: vecY
                    width: (parent.width - parent.spacing) / 2
                    decimals: 2
                    step: 1
                    Binding on value {
                        when: !vecY.activeFocus
                        value: slotRow.current !== undefined ? Number(slotRow.current[1]) : 0
                    }
                    onEdited: v => root.changed(slotRow.spec.id, [vecX.value, v])
                }
            }

            ThemedComboBox {
                visible: slotRow.spec.type === "enum"
                width: parent.width
                readonly property var ids: slotRow.spec.values || []
                model: ids
                currentIndex: Math.max(0, ids.indexOf(String(slotRow.current)))
                onActivated: root.changed(slotRow.spec.id, ids[currentIndex])
            }

            ThemedToggleButton {
                visible: slotRow.spec.type === "bool"
                text: root.labelOf(slotRow.spec)
                checked: slotRow.current === true || slotRow.current === "true"
                onClicked: root.changed(slotRow.spec.id, !(slotRow.current === true || slotRow.current === "true"))
            }

            ThemedButton {
                visible: slotRow.spec.type === "image"
                text: root.isSet(slotRow.spec) ? qsTr("Change image…") : qsTr("Choose image…")
                onClicked: {
                    const url = FileDialogs.openFile(qsTr("Slot Image"),
                                                     [qsTr("Images (*.png *.jpg *.jpeg *.webp)")])
                    if (url != "")
                        root.changed(slotRow.spec.id, url.toString().replace(/^file:\/\//, ""))
                }
            }
        }
    }
}
