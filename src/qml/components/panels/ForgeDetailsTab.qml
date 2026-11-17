pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Drift

// Name, identifier, category and description of the effect, transition or animation being built.
Item {
    id: root

    property string idError: ""

    function commit(key, text) {
        const error = Forge.setMeta(key, text)
        if (key === "id")
            idError = error
    }

    function directionList() {
        return Forge.meta.directions ?? []
    }

    function toggleDirection(name, on) {
        const next = directionList().filter(d => d !== name)
        if (on)
            next.push(name)
        Forge.setMeta("directions", next)
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
            spacing: Theme.spacingSm

            component FieldLabel: Text {
                color: Theme.c.forgePanel.foregroundMuted
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
            }

            component MetaField: ThemedTextField {
                id: field
                required property string key
                Layout.fillWidth: true
                implicitHeight: Theme.controlHeightSm
                font.pixelSize: Theme.fontSizeXs
                Binding on text {
                    value: String(Forge.meta[field.key] ?? "")
                    when: !field.activeFocus
                }
                errorText: key === "id" ? root.idError : ""
                onTextEdited: root.commit(key, text)
            }

            FieldLabel { text: qsTr("Name") }
            MetaField { key: "displayName" }

            FieldLabel { text: qsTr("Identifier") }
            MetaField { key: "id" }

            FieldLabel { text: qsTr("Category") }
            ThemedComboBox {
                Layout.fillWidth: true
                implicitHeight: Theme.controlHeightSm
                font.pixelSize: Theme.fontSizeXs
                model: Forge.categories
                currentIndex: Forge.categories.indexOf(Forge.meta.category)
                onActivated: (i) => Forge.setMeta("category", Forge.categories[i])
            }

            FieldLabel { text: qsTr("Description") }
            ThemedTextArea {
                id: descriptionField
                Layout.fillWidth: true
                implicitHeight: 90
                font.pixelSize: Theme.fontSizeXs
                Binding on text {
                    value: String(Forge.meta.description ?? "")
                    when: !descriptionField.activeFocus
                }
                onTextChanged: if (activeFocus) Forge.setMeta("description", text)
            }

            FieldLabel { text: qsTr("Author") }
            MetaField { key: "author" }

            FieldLabel { text: qsTr("Version") }
            MetaField { key: "version" }

            FieldLabel {
                visible: Forge.kind === "animation"
                text: qsTr("Plays as")
            }
            Row {
                visible: Forge.kind === "animation"
                spacing: Theme.spacingSm

                Repeater {
                    model: [
                        { "id": "in", "label": qsTr("In") },
                        { "id": "out", "label": qsTr("Out") }
                    ]

                    ThemedChip {
                        required property var modelData
                        text: modelData.label
                        variant: "outline"
                        selected: root.directionList().indexOf(modelData.id) >= 0
                        onClicked: root.toggleDirection(modelData.id, !selected)
                    }
                }
            }

            FieldLabel {
                visible: Forge.kind === "animation"
                text: qsTr("Default duration")
            }
            ThemedNumberField {
                visible: Forge.kind === "animation"
                Layout.fillWidth: true
                implicitHeight: Theme.controlHeightSm
                font.pixelSize: Theme.fontSizeXs
                from: 0
                to: 10000
                step: 50
                unit: qsTr("ms")
                value: Number(Forge.meta.defaultDurationMs ?? 0)
                onEdited: (v) => Forge.setMeta("defaultDurationMs", v)
            }
        }
    }
}
