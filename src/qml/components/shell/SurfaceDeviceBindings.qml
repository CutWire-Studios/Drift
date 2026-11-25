import QtQuick
import QtQuick.Layouts
import Drift

// One control surface's bindings: what each control drives, with invert and sensitivity, plus a
// way to add one by hand. Learning from the editor's own parameters is the quicker route.
Column {
    id: root

    required property var device
    readonly property var surfaces: EditorState.surfaces
    readonly property var bindingList: {
        void surfaces.devices
        return surfaces.bindings(device.id)
    }
    readonly property var targetList: surfaces.targets()
    readonly property var groups: {
        const seen = []
        for (const t of targetList) {
            if (seen.indexOf(t.group) < 0)
                seen.push(t.group)
        }
        return seen
    }
    property string group: groups.length > 0 ? groups[0] : ""
    readonly property var groupTargets: targetList.filter(t => t.group === group)

    width: parent.width
    spacing: Theme.spacingMd

    Text {
        width: parent.width
        visible: root.bindingList.length === 0
        text: qsTr("Nothing is bound yet. Turn on learn mode, click a parameter in the editor, then move a control.")
        color: Theme.c.settingsRow.foregroundMuted
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSizeXs
        wrapMode: Text.WordWrap
    }

    Repeater {
        model: root.bindingList

        delegate: RowLayout {
            id: row

            required property var modelData

            width: root.width
            spacing: Theme.spacingMd

            Text {
                Layout.preferredWidth: root.width * 0.28
                text: row.modelData.controlLabel
                color: Theme.c.settingsRow.foreground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeSm
                elide: Text.ElideRight
            }
            Text {
                Layout.fillWidth: true
                text: row.modelData.targetLabel
                color: Theme.c.settingsRow.foregroundMuted
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeSm
                elide: Text.ElideRight
            }
            ThemedComboBox {
                Layout.preferredWidth: 120
                visible: row.modelData.controlKind === "absolute"
                textRole: "label"
                valueRole: "id"
                model: [
                    { id: "", label: qsTr("Position") },
                    { id: "twos", label: qsTr("Counter, 2's complement") },
                    { id: "signmag", label: qsTr("Counter, sign and size") },
                    { id: "offset", label: qsTr("Counter around 64") }
                ]
                currentIndex: Math.max(0, indexOfValue(row.modelData.encoder))
                onActivated: root.surfaces.setBindingOption(root.device.id, row.modelData.control, "encoder", currentValue)
            }
            ThemedCheckBox {
                text: qsTr("Invert")
                checked: row.modelData.invert
                onToggled: root.surfaces.setBindingOption(root.device.id, row.modelData.control, "invert", checked)
            }
            ThemedTextField {
                Layout.preferredWidth: 64
                text: String(row.modelData.sensitivity)
                validator: DoubleValidator { bottom: 0.01; top: 100 }
                placeholderText: qsTr("Speed")
                onEditingFinished: root.surfaces.setBindingOption(root.device.id, row.modelData.control,
                                                                  "sensitivity", Number(text))
            }
            ThemedButton {
                variant: "ghost"
                text: qsTr("Remove")
                onClicked: root.surfaces.removeBinding(root.device.id, row.modelData.control)
            }
        }
    }

    RowLayout {
        width: parent.width
        spacing: Theme.spacingMd

        ThemedComboBox {
            id: groupCombo
            Layout.preferredWidth: root.width * 0.28
            model: root.groups
            onActivated: root.group = root.groups[currentIndex]
        }
        ThemedComboBox {
            id: targetCombo
            Layout.fillWidth: true
            textRole: "label"
            valueRole: "id"
            model: root.groupTargets
        }
        ThemedComboBox {
            id: controlCombo
            Layout.preferredWidth: root.width * 0.24
            textRole: "label"
            valueRole: "id"
            model: root.device.controls
        }
        ThemedButton {
            text: qsTr("Bind")
            enabled: targetCombo.currentIndex >= 0 && controlCombo.currentIndex >= 0
            onClicked: root.surfaces.bind(root.device.id, controlCombo.currentValue, targetCombo.currentValue,
                                          targetCombo.currentText)
        }
        ThemedButton {
            text: qsTr("Learn")
            enabled: root.device.connected && targetCombo.currentIndex >= 0
            onClicked: root.surfaces.armLearn(targetCombo.currentValue, targetCombo.currentText)
        }
    }

    Row {
        spacing: Theme.spacingMd

        ThemedButton {
            text: qsTr("Export…")
            onClicked: {
                const url = FileDialogs.saveFile(qsTr("Export bindings"), [qsTr("Bindings (*.json)")],
                                                 root.device.name, "json")
                if (url !== "")
                    root.surfaces.exportToFile(url, root.device.id)
            }
        }
        ThemedButton {
            text: qsTr("Import…")
            onClicked: {
                const url = FileDialogs.openFile(qsTr("Import bindings"), [qsTr("Bindings (*.json)")])
                if (url !== "" && root.surfaces.importFromFile(url, root.device.id) < 0)
                    Toasts.error(qsTr("That file is not a bindings file."))
            }
        }
        ThemedButton {
            variant: "ghost"
            text: qsTr("Clear")
            enabled: root.bindingList.length > 0
            onClicked: root.surfaces.clearBindings(root.device.id)
        }
        ThemedButton {
            variant: "ghost"
            text: qsTr("Forget device")
            visible: !root.device.connected
            onClicked: root.surfaces.forgetDevice(root.device.id)
        }
    }
}
