pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Drift

// The sliders the finished effect shows: edit each one's name, range and default, reorder or
// remove it, and try values on the live preview (those never change the document).
Item {
    id: root

    property var forge: Forge

    function qmlColor(h) {
        return h.length === 9 ? "#" + h.slice(7, 9) + h.slice(1, 7) : h
    }

    function hexColor(q, alpha) {
        if (q.length === 9)
            return alpha ? "#" + q.slice(3) + q.slice(1, 3) : "#" + q.slice(3)
        return q
    }

    Flickable {
        anchors.fill: parent
        contentHeight: column.implicitHeight + 2 * Theme.spacingMd
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: AppScrollBar { }

        ColumnLayout {
            id: column
            x: Theme.spacingMd
            y: Theme.spacingMd
            width: parent.width - 2 * Theme.spacingMd
            spacing: Theme.spacingMd

            Flow {
                Layout.fillWidth: true
                spacing: Theme.spacingSm

                ThemedButton {
                    text: qsTr("Reset values")
                    onClicked: root.forge.resetPreviewValues()
                }
                ThemedTextField {
                    id: presetName
                    width: 140
                    implicitHeight: Theme.controlHeightSm
                    font.family: Theme.fontFamily
                    placeholderText: qsTr("Preset name")
                }
                ThemedButton {
                    text: qsTr("Save preset")
                    enabled: presetName.text.trim().length > 0 && root.forge.params.length > 0
                    onClicked: {
                        root.forge.addPreset(presetName.text.trim())
                        presetName.clear()
                    }
                }
            }

            Flow {
                Layout.fillWidth: true
                spacing: Theme.spacingSm

                Repeater {
                    model: root.forge.presetNames

                    Row {
                        id: preset
                        required property string modelData
                        spacing: 0

                        ThemedChip {
                            text: preset.modelData
                            variant: "outline"
                            onClicked: root.forge.applyPreset(preset.modelData)
                        }
                        IconButton {
                            glyph: Theme.icons.x
                            tooltip: qsTr("Delete preset")
                            onClicked: root.forge.deletePreset(preset.modelData)
                        }
                    }
                }
            }

            Text {
                Layout.fillWidth: true
                visible: root.forge.params.length === 0
                wrapMode: Text.WordWrap
                text: qsTr("No sliders yet. Right-click a node and choose Expose as slider, or use Expose in the Node tab.")
                color: Theme.c.forgePanel.foregroundMuted
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeSm
            }

            Repeater {
                model: root.forge.params.length

                Rectangle {
                    id: card
                    required property int index
                    readonly property var p: root.forge.params[index]
                    readonly property string type: p ? p.type : ""
                    readonly property bool numeric: type === "float" || type === "int" || type === "seed"
                    readonly property var preview: p ? root.forge.previewValues[p.identifier] : undefined
                    Layout.fillWidth: true
                    implicitHeight: body.implicitHeight + 2 * Theme.spacingMd
                    radius: Theme.c.nodeDelegate.radius
                    color: Theme.c.forgePanel.rowBackground
                    visible: !!p

                    function patch(values, coalesce) {
                        root.forge.updateParam(p.identifier, values, coalesce === true)
                    }

                    ColumnLayout {
                        id: body
                        anchors.fill: parent
                        anchors.margins: Theme.spacingMd
                        spacing: Theme.spacingSm

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Theme.spacingXs

                            ThemedTextField {
                                id: nameField
                                Layout.fillWidth: true
                                implicitHeight: Theme.controlHeightSm
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontSizeXs
                                placeholderText: qsTr("Label")
                                Binding on text {
                                    value: card.p ? card.p.displayName : ""
                                    when: !nameField.activeFocus
                                }
                                onTextEdited: card.patch({ "displayName": text }, true)
                            }
                            IconButton {
                                glyph: Theme.icons.chevronUp
                                tooltip: qsTr("Move up")
                                enabled: card.index > 0
                                onClicked: root.forge.moveParam(card.p.identifier, -1)
                            }
                            IconButton {
                                glyph: Theme.icons.chevronDown
                                tooltip: qsTr("Move down")
                                enabled: card.index < root.forge.params.length - 1
                                onClicked: root.forge.moveParam(card.p.identifier, 1)
                            }
                            IconButton {
                                glyph: Theme.icons.trash
                                tooltip: qsTr("Remove slider")
                                onClicked: root.forge.removeParam(card.p.identifier)
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Theme.spacingSm

                            Text {
                                text: card.type
                                color: Theme.c.forgePanel.foregroundMuted
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontSizeXs
                            }
                            ThemedTextField {
                                id: idField
                                Layout.fillWidth: true
                                implicitHeight: Theme.controlHeightSm
                                font.pixelSize: Theme.fontSizeXs
                                placeholderText: qsTr("identifier")
                                Binding on text {
                                    value: card.p ? card.p.identifier : ""
                                    when: !idField.activeFocus
                                }
                                onEditingFinished: if (card.p && text !== card.p.identifier) card.patch({ "identifier": text })
                            }
                        }

                        RowLayout {
                            visible: card.numeric
                            Layout.fillWidth: true
                            spacing: Theme.spacingXs

                            Repeater {
                                model: [
                                    { "key": "min", "label": qsTr("Min") },
                                    { "key": "max", "label": qsTr("Max") },
                                    { "key": "default", "label": qsTr("Default") }
                                ]

                                ColumnLayout {
                                    id: numberCell
                                    required property var modelData
                                    Layout.fillWidth: true
                                    spacing: 0

                                    Text {
                                        text: numberCell.modelData.label
                                        color: Theme.c.forgePanel.foregroundMuted
                                        font.family: Theme.fontFamily
                                        font.pixelSize: Theme.fontSizeXs
                                    }
                                    ThemedNumberField {
                                        Layout.fillWidth: true
                                        implicitHeight: Theme.controlHeightSm
                                        font.pixelSize: Theme.fontSizeXs
                                        decimals: card.type === "float" ? 3 : 0
                                        step: card.type === "float" ? 0.01 : 1
                                        Binding on value {
                                            value: card.p ? Number(card.p[numberCell.modelData.key]) : 0
                                            when: !numberCell.activeFocus
                                        }
                                        onEdited: (v) => {
                                            const values = {}
                                            values[numberCell.modelData.key] = v
                                            card.patch(values)
                                        }
                                    }
                                }
                            }
                        }

                        RowLayout {
                            visible: card.type === "bool"
                            Layout.fillWidth: true

                            Text {
                                Layout.fillWidth: true
                                text: qsTr("On by default")
                                color: Theme.c.forgePanel.foregroundMuted
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontSizeXs
                            }
                            ThemedSwitch {
                                checked: !!(card.p && card.p["default"])
                                onToggled: card.patch({ "default": checked })
                            }
                        }

                        RowLayout {
                            visible: card.type === "color"
                            Layout.fillWidth: true

                            Text {
                                Layout.fillWidth: true
                                text: qsTr("Default color")
                                color: Theme.c.forgePanel.foregroundMuted
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontSizeXs
                            }
                            ColorSwatchField {
                                hex: card.p && card.type === "color" ? root.qmlColor(String(card.p["default"])) : "#ffffffff"
                                onEdited: (h) => card.patch({ "default": root.hexColor(h, !!card.p.alpha) })
                            }
                        }

                        RowLayout {
                            visible: card.type === "choice"
                            Layout.fillWidth: true

                            ThemedTextField {
                                id: optionsField
                                Layout.fillWidth: true
                                implicitHeight: Theme.controlHeightSm
                                font.pixelSize: Theme.fontSizeXs
                                placeholderText: qsTr("Comma-separated options")
                                Binding on text {
                                    value: card.p && card.p.options ? card.p.options.join(", ") : ""
                                    when: !optionsField.activeFocus
                                }
                                onEditingFinished: {
                                    const list = text.split(",").map(s => s.trim()).filter(s => s.length > 0)
                                    card.patch({ "options": list, "max": Math.max(list.length - 1, 1) })
                                }
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Theme.spacingSm

                            Text {
                                text: qsTr("Group")
                                color: Theme.c.forgePanel.foregroundMuted
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontSizeXs
                            }
                            ThemedTextField {
                                id: groupField
                                Layout.fillWidth: true
                                implicitHeight: Theme.controlHeightSm
                                font.pixelSize: Theme.fontSizeXs
                                Binding on text {
                                    value: card.p && card.p.group ? card.p.group : ""
                                    when: !groupField.activeFocus
                                }
                                onEditingFinished: card.patch({ "group": text.length > 0 ? text : null })
                            }
                        }

                        RowLayout {
                            visible: card.numeric || card.type === "bool" || card.type === "choice"
                                     || card.type === "color" || card.type === "point"
                            Layout.fillWidth: true
                            spacing: Theme.spacingSm

                            Text {
                                text: qsTr("Preview")
                                color: Theme.c.forgePanel.foregroundMuted
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontSizeXs
                            }
                            Loader {
                                id: previewLoader
                                Layout.fillWidth: true
                                sourceComponent: {
                                    if (card.numeric)
                                        return previewSlider
                                    switch (card.type) {
                                    case "bool": return previewSwitch
                                    case "choice": return previewChoice
                                    case "color": return previewColor
                                    case "point": return previewPoint
                                    }
                                    return null
                                }
                            }
                            Binding {
                                target: previewLoader.item
                                property: "card"
                                value: card
                                when: previewLoader.item !== null
                            }
                        }

                        Text {
                            Layout.fillWidth: true
                            visible: card.p && card.p.used === 0
                            text: qsTr("Nothing uses this slider.")
                            color: Theme.c.forgePanel.foregroundMuted
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSizeXs
                        }
                    }
                }
            }
        }
    }

    Component {
        id: previewSlider

        ThemedSlider {
            id: slider
            property var card: null
            from: card && card.p ? Number(card.p.min) : 0
            to: card && card.p ? Number(card.p.max) : 1
            stepSize: card && card.type !== "float" ? 1 : 0
            Binding on value {
                value: Number(card ? card.preview : 0)
                when: !slider.pressed
            }
            onMoved: root.forge.setPreviewValue(card.p.identifier, value)
        }
    }

    Component {
        id: previewSwitch

        ThemedSwitch {
            property var card: null
            checked: !!(card && card.preview)
            onToggled: root.forge.setPreviewValue(card.p.identifier, checked)
        }
    }

    Component {
        id: previewChoice

        ThemedComboBox {
            property var card: null
            implicitHeight: Theme.controlHeightSm
            font.pixelSize: Theme.fontSizeXs
            model: card && card.p && card.p.options ? card.p.options : []
            currentIndex: card ? Math.round(Number(card.preview)) : 0
            onActivated: (i) => root.forge.setPreviewValue(card.p.identifier, i)
        }
    }

    Component {
        id: previewColor

        ColorSwatchField {
            property var card: null
            hex: card && card.preview !== undefined ? root.qmlColor(String(card.preview)) : "#ffffffff"
            onEdited: (h) => root.forge.setPreviewValue(card.p.identifier, root.hexColor(h, !!card.p.alpha))
        }
    }

    Component {
        id: previewPoint

        RowLayout {
            property var card: null
            spacing: Theme.spacingXs

            Repeater {
                model: 2

                ThemedNumberField {
                    id: axis
                    required property int index
                    Layout.fillWidth: true
                    implicitHeight: Theme.controlHeightSm
                    font.pixelSize: Theme.fontSizeXs
                    decimals: 3
                    step: 0.01
                    Binding on value {
                        value: card && card.preview ? Number(card.preview[axis.index]) : 0
                        when: !axis.activeFocus
                    }
                    onEdited: (v) => {
                        const next = [Number(card.preview[0]), Number(card.preview[1])]
                        next[axis.index] = v
                        root.forge.setPreviewValue(card.p.identifier, next)
                    }
                }
            }
        }
    }
}
