pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Drift

// Settings of the selected pedal, split or modulator: its knobs (each can become a slider), the
// impulse response of a convolution reverb, lane gains of a split, the steps of a sequencer and
// the modulation routes that reach it.
Item {
    id: root

    readonly property string nodeId: AudioForge.selection.length === 1 ? AudioForge.selection[0] : ""
    property var info: ({})
    property string status: ""
    readonly property var controls: (info.inputs ?? []).filter(p => p.type !== "audio")

    function refresh() {
        info = nodeId.length > 0 ? AudioForge.nodeInfo(nodeId) : ({})
    }

    onNodeIdChanged: {
        status = ""
        refresh()
    }
    Component.onCompleted: refresh()

    Connections {
        target: AudioForge
        function onHistoryChanged() { root.refresh() }
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

            component Caption: Text {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: Theme.c.forgePanel.foregroundMuted
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingSm

                Text {
                    Layout.fillWidth: true
                    text: root.info.title ?? ""
                    elide: Text.ElideRight
                    color: Theme.c.forgePanel.foreground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeBase
                    font.weight: Font.Medium
                }
                ThemedButton {
                    visible: root.info.deletable === true
                    variant: "ghost"
                    text: qsTr("Delete")
                    onClicked: AudioForge.deleteSelection()
                }
            }

            Caption {
                visible: root.info.kind === "input" || root.info.kind === "output" || root.info.kind === "merge"
                text: root.info.kind === "input" ? qsTr("The clip's sound enters the board here.")
                      : root.info.kind === "output" ? qsTr("Whatever reaches this node is what you hear.")
                      : qsTr("Mixes the lanes of its split back into one sound. It follows the split.")
            }

            RowLayout {
                visible: root.info.kind === "pedal"
                Layout.fillWidth: true
                spacing: Theme.spacingSm

                Caption {
                    Layout.preferredWidth: content.width * 0.32
                    text: qsTr("Bypass")
                }
                ThemedSwitch {
                    visible: root.info.bypass !== undefined && root.info.bypass.exposed === undefined
                    checked: !!(root.info.bypass ?? {}).value
                    onToggled: AudioForge.setBypass(root.nodeId, checked)
                }
                Caption {
                    visible: root.info.bypass !== undefined && root.info.bypass.exposed !== undefined
                    Layout.fillWidth: true
                    text: qsTr("Slider: %1").arg((root.info.bypass ?? {}).exposed ?? "")
                }
                Item { Layout.fillWidth: true; visible: !(root.info.bypass ?? {}).exposed }
                ThemedButton {
                    variant: "ghost"
                    text: (root.info.bypass ?? {}).exposed === undefined ? qsTr("Expose") : qsTr("Unexpose")
                    onClicked: (root.info.bypass ?? {}).exposed === undefined
                               ? AudioForge.exposeInput(root.nodeId, "bypass")
                               : AudioForge.unexposeInput(root.nodeId, "bypass")
                }
            }

            Repeater {
                model: root.controls.length

                RowLayout {
                    id: inputRow
                    required property int index
                    readonly property var port: root.controls[index]
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
                        controller: AudioForge
                    }
                    Text {
                        Layout.fillWidth: true
                        visible: inputRow.port.connected || inputRow.port.exposed !== undefined
                        text: inputRow.port.connected ? qsTr("Modulated")
                                                      : qsTr("Slider: %1").arg(inputRow.port.exposed)
                        elide: Text.ElideRight
                        color: Theme.c.forgePanel.foreground
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSizeXs
                    }
                    ThemedButton {
                        visible: inputRow.port.exposable === true && !inputRow.port.connected
                        variant: "ghost"
                        text: inputRow.port.exposed === undefined ? qsTr("Expose") : qsTr("Unexpose")
                        onClicked: inputRow.port.exposed === undefined
                                   ? AudioForge.exposeInput(root.nodeId, inputRow.port.id)
                                   : AudioForge.unexposeInput(root.nodeId, inputRow.port.id)
                    }
                }
            }

            ThemedButton {
                visible: root.info.isConvolution === true
                text: qsTr("Import recording…")
                onClicked: Qt.callLater(() => {
                    const url = FileDialogs.openFile(qsTr("Import impulse response"), [qsTr("WAV audio (*.wav)")])
                    if (url.toString().length > 0)
                        root.status = AudioForge.importIr(root.nodeId, url)
                })
            }
            Caption {
                visible: root.info.isConvolution === true
                text: qsTr("A short WAV recording of a room, spring or plate. Its length sets the reverb tail.")
            }

            ColumnLayout {
                visible: root.info.kind === "split"
                Layout.fillWidth: true
                spacing: Theme.spacingSm

                Caption { text: qsTr("Lane levels") }
                Repeater {
                    model: root.info.lanes ?? []

                    RowLayout {
                        id: laneRow
                        required property var modelData
                        Layout.fillWidth: true
                        spacing: Theme.spacingSm

                        Text {
                            Layout.preferredWidth: content.width * 0.32
                            text: laneRow.modelData.label
                            color: Theme.c.forgePanel.foregroundMuted
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSizeXs
                        }
                        ThemedSlider {
                            id: laneGain
                            Layout.fillWidth: true
                            from: 0
                            to: 2
                            valueFormatter: (v) => Number(v).toFixed(2)
                            Binding on value {
                                value: Number(laneRow.modelData.gain)
                                when: !laneGain.pressed
                            }
                            onMoved: AudioForge.setLaneGain(root.nodeId, laneRow.modelData.id, value, true)
                        }
                    }
                }
                Flow {
                    Layout.fillWidth: true
                    spacing: Theme.spacingSm

                    ThemedButton {
                        text: qsTr("Add lane")
                        enabled: root.info.canAddLane === true
                        onClicked: AudioForge.addLane(root.nodeId)
                    }
                    ThemedButton {
                        text: qsTr("Remove last lane")
                        enabled: root.info.canRemoveLane === true
                        onClicked: {
                            const lanes = root.info.lanes
                            AudioForge.removeLane(root.nodeId, lanes[lanes.length - 1].id)
                        }
                    }
                }
            }

            ColumnLayout {
                visible: root.info.steps !== undefined
                Layout.fillWidth: true
                spacing: Theme.spacingSm

                Caption { text: qsTr("Steps") }
                Item {
                    id: stepEditor
                    Layout.fillWidth: true
                    Layout.preferredHeight: 96

                    readonly property var steps: root.info.steps ?? []

                    function setAt(x, y) {
                        const n = steps.length
                        if (n === 0)
                            return
                        const i = Math.max(0, Math.min(n - 1, Math.floor(x / (width / n))))
                        const next = steps.slice()
                        next[i] = Math.max(0, Math.min(1, 1 - y / height))
                        AudioForge.setSteps(root.nodeId, next, true)
                    }

                    Rectangle {
                        anchors.fill: parent
                        radius: Theme.radiusSm
                        color: Theme.c.forgePanel.rowBackground
                    }
                    Row {
                        anchors.fill: parent
                        spacing: 0

                        Repeater {
                            model: stepEditor.steps.length

                            Item {
                                id: stepBar
                                required property int index
                                width: stepEditor.width / Math.max(1, stepEditor.steps.length)
                                height: stepEditor.height

                                Rectangle {
                                    anchors.bottom: parent.bottom
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    width: Math.max(2, parent.width - 3)
                                    height: Math.max(2, stepEditor.steps[stepBar.index] * parent.height)
                                    radius: 2
                                    color: Theme.c.forgePanel.selection
                                }
                            }
                        }
                    }
                    MouseArea {
                        anchors.fill: parent
                        onPressed: (m) => stepEditor.setAt(m.x, m.y)
                        onPositionChanged: (m) => { if (pressed) stepEditor.setAt(m.x, m.y) }
                    }
                }
                Flow {
                    Layout.fillWidth: true
                    spacing: Theme.spacingSm

                    ThemedButton {
                        text: qsTr("Fewer steps")
                        enabled: stepEditor.steps.length > 1
                        onClicked: AudioForge.setSteps(root.nodeId, stepEditor.steps.slice(0, -1), false)
                    }
                    ThemedButton {
                        text: qsTr("More steps")
                        enabled: stepEditor.steps.length < (root.info.maxSteps ?? 16)
                        onClicked: AudioForge.setSteps(root.nodeId, stepEditor.steps.concat([0.5]), false)
                    }
                }
            }

            ColumnLayout {
                visible: (root.info.routes ?? []).length > 0
                Layout.fillWidth: true
                spacing: Theme.spacingSm

                Caption {
                    text: root.info.kind === "modulator" ? qsTr("Moves") : qsTr("Moved by")
                }
                Repeater {
                    model: root.info.routes ?? []

                    ColumnLayout {
                        id: routeRow
                        required property var modelData
                        Layout.fillWidth: true
                        spacing: 2

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Theme.spacingSm

                            Text {
                                Layout.fillWidth: true
                                text: routeRow.modelData.label + " · " + routeRow.modelData.knobLabel
                                elide: Text.ElideRight
                                color: Theme.c.forgePanel.foreground
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontSizeXs
                            }
                            ThemedButton {
                                variant: "ghost"
                                text: qsTr("Remove")
                                onClicked: AudioForge.removeRoute(routeRow.modelData.id)
                            }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Theme.spacingSm

                            Text {
                                Layout.preferredWidth: content.width * 0.32
                                text: qsTr("Depth")
                                color: Theme.c.forgePanel.foregroundMuted
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontSizeXs
                            }
                            ThemedSlider {
                                id: depth
                                Layout.fillWidth: true
                                visible: routeRow.modelData.exposed === undefined
                                from: -1
                                to: 1
                                valueFormatter: (v) => Number(v).toFixed(2)
                                Binding on value {
                                    value: Number(routeRow.modelData.depth)
                                    when: !depth.pressed
                                }
                                onMoved: AudioForge.setRouteDepth(routeRow.modelData.id, value, true)
                            }
                            Text {
                                Layout.fillWidth: true
                                visible: routeRow.modelData.exposed !== undefined
                                text: qsTr("Slider: %1").arg(routeRow.modelData.exposed ?? "")
                                elide: Text.ElideRight
                                color: Theme.c.forgePanel.foreground
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontSizeXs
                            }
                        }
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
}
