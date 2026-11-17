pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Drift

// Settings of the selected node: its inputs (with the option to turn one into a slider) and its
// options (dropdowns, switches, numbers, custom GLSL).
Item {
    id: root

    readonly property string nodeId: Forge.selection.length === 1 ? Forge.selection[0] : ""
    property var info: ({})
    property var options: []
    property string status: ""

    function refresh() {
        info = nodeId.length > 0 ? Forge.nodeInfo(nodeId) : ({})
        options = nodeId.length > 0 ? Forge.nodeOptions(nodeId) : []
    }

    function optionText(o) {
        return o.exposed !== undefined ? qsTr("Slider: %1").arg(o.exposed) : ""
    }

    onNodeIdChanged: refresh()
    Component.onCompleted: refresh()

    Connections {
        target: Forge
        function onCompiled() { root.refresh() }
        function onMessage(text) { root.status = text }
    }

    Text {
        anchors.centerIn: parent
        width: parent.width - 2 * Theme.spacing2xl
        visible: root.nodeId.length === 0
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.WordWrap
        text: qsTr("Select a single node in the graph to edit its settings.")
        color: Theme.c.forgePanel.foregroundMuted
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSizeSm
    }

    Flickable {
        anchors.fill: parent
        visible: root.nodeId.length > 0
        contentHeight: content.implicitHeight + 2 * Theme.spacingMd
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: AppScrollBar { }

        ColumnLayout {
            id: content
            x: Theme.spacingMd
            y: Theme.spacingMd
            width: parent.width - 2 * Theme.spacingMd
            spacing: Theme.spacingSm

            Text {
                Layout.fillWidth: true
                text: root.info.title ?? ""
                color: Theme.c.forgePanel.foreground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeBase
                font.weight: Font.Medium
            }
            Text {
                Layout.fillWidth: true
                text: root.info.description ?? ""
                wrapMode: Text.WordWrap
                color: Theme.c.forgePanel.foregroundMuted
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
            }

            Repeater {
                model: root.info.inputs ? root.info.inputs.length : 0

                RowLayout {
                    id: inputRow
                    required property int index
                    readonly property var port: root.info.inputs[index]
                    Layout.fillWidth: true
                    spacing: Theme.spacingSm

                    Text {
                        Layout.preferredWidth: content.width * 0.32
                        text: inputRow.port.label
                        elide: Text.ElideRight
                        color: Theme.c.forgePanel.foregroundMuted
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSizeXs
                    }
                    NodeInputControl {
                        Layout.fillWidth: true
                        Layout.preferredHeight: Theme.controlHeightSm
                        visible: !inputRow.port.connected && inputRow.port.exposed === undefined
                        port: inputRow.port
                        nodeId: root.nodeId
                        controller: Forge
                    }
                    Text {
                        Layout.fillWidth: true
                        visible: inputRow.port.connected || inputRow.port.exposed !== undefined
                        text: inputRow.port.connected ? qsTr("Connected")
                                                      : qsTr("Slider: %1").arg(inputRow.port.exposed)
                        elide: Text.ElideRight
                        color: Theme.c.forgePanel.foreground
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSizeXs
                    }
                    ThemedButton {
                        visible: !inputRow.port.connected
                        variant: "ghost"
                        text: inputRow.port.exposed === undefined ? qsTr("Expose") : qsTr("Unexpose")
                        onClicked: inputRow.port.exposed === undefined
                                   ? Forge.exposeInput(root.nodeId, inputRow.port.id)
                                   : Forge.unexposeInput(root.nodeId, inputRow.port.id)
                    }
                }
            }

            Repeater {
                model: root.options.length

                RowLayout {
                    id: optionRow
                    required property int index
                    readonly property var option: root.options[index]
                    Layout.fillWidth: true
                    spacing: Theme.spacingSm

                    Text {
                        Layout.preferredWidth: content.width * 0.32
                        Layout.alignment: Qt.AlignTop
                        text: optionRow.option.label
                        elide: Text.ElideRight
                        color: Theme.c.forgePanel.foregroundMuted
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSizeXs
                    }
                    Loader {
                        Layout.fillWidth: true
                        sourceComponent: {
                            if (optionRow.option.exposable)
                                return exposableOption
                            switch (optionRow.option.kind) {
                            case "select": return selectOption
                            case "toggle": return toggleOption
                            case "number": return numberOption
                            case "code": return codeOption
                            case "labels": return labelsOption
                            }
                            return null
                        }
                        onLoaded: item.option = Qt.binding(() => optionRow.option)
                    }
                }
            }

            Text {
                Layout.fillWidth: true
                visible: root.status.length > 0
                text: root.status
                wrapMode: Text.WordWrap
                color: Theme.c.forgePanel.error
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
            }
        }
    }

    Component {
        id: selectOption

        ThemedComboBox {
            property var option: ({})
            readonly property var choices: option.choices ?? []
            implicitHeight: Theme.controlHeightSm
            font.pixelSize: Theme.fontSizeXs
            model: choices.map(c => c.label)
            currentIndex: {
                for (let i = 0; i < choices.length; ++i)
                    if (choices[i].value === option.value)
                        return i
                return -1
            }
            onActivated: (i) => Forge.setNodeOption(root.nodeId, option.id, choices[i].value, false)
        }
    }

    Component {
        id: toggleOption

        ThemedSwitch {
            property var option: ({})
            checked: !!option.value
            onToggled: Forge.setNodeOption(root.nodeId, option.id, checked, false)
        }
    }

    Component {
        id: numberOption

        ThemedNumberField {
            property var option: ({})
            implicitHeight: Theme.controlHeightSm
            font.pixelSize: Theme.fontSizeXs
            from: option.min ?? 0
            to: option.max ?? 1
            step: option.step > 0 ? option.step : 1
            decimals: option.step > 0 && option.step < 1 ? 2 : 0
            value: Number(option.value ?? 0)
            onEdited: (v) => Forge.setNodeOption(root.nodeId, option.id, v, false)
        }
    }

    Component {
        id: labelsOption

        ThemedTextField {
            property var option: ({})
            implicitHeight: Theme.controlHeightSm
            font.pixelSize: Theme.fontSizeXs
            placeholderText: qsTr("Comma-separated choices")
            text: (option.value ?? []).join(", ")
            onEditingFinished: Forge.setNodeOption(root.nodeId, option.id,
                                                   text.split(",").map(s => s.trim()).filter(s => s.length > 0), false)
        }
    }

    Component {
        id: codeOption

        ThemedTextArea {
            id: area
            property var option: ({})
            implicitHeight: 160
            font.family: Theme.monoFontFamily
            font.pixelSize: Theme.fontSizeXs
            wrapMode: TextArea.NoWrap
            Binding on text {
                value: String(area.option.value ?? "")
                when: !area.activeFocus
            }
            onTextChanged: if (activeFocus) Forge.setNodeOption(root.nodeId, option.id, text, true)
        }
    }

    Component {
        id: exposableOption

        RowLayout {
            property var option: ({})
            spacing: Theme.spacingSm

            Text {
                Layout.fillWidth: true
                text: option.exposed !== undefined ? qsTr("Slider: %1").arg(option.exposed)
                                                   : qsTr("Not exposed")
                elide: Text.ElideRight
                color: Theme.c.forgePanel.foreground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
            }
            ThemedButton {
                variant: "ghost"
                text: option.exposed === undefined ? qsTr("Expose") : qsTr("Unexpose")
                onClicked: option.exposed === undefined
                           ? Forge.exposeOption(root.nodeId, option.id)
                           : Forge.unexposeOption(root.nodeId, option.id)
            }
        }
    }
}
