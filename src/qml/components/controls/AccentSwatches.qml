import QtQuick
import Drift

Flow {
    id: root

    readonly property var swatches: [
        { id: "amber", color: Theme.c.settingsAccentSwatch.amber, label: qsTr("Amber") },
        { id: "orange", color: Theme.c.settingsAccentSwatch.orange, label: qsTr("Orange") },
        { id: "red", color: Theme.c.settingsAccentSwatch.red, label: qsTr("Red") },
        { id: "pink", color: Theme.c.settingsAccentSwatch.pink, label: qsTr("Pink") },
        { id: "purple", color: Theme.c.settingsAccentSwatch.purple, label: qsTr("Purple") },
        { id: "blue", color: Theme.c.settingsAccentSwatch.blue, label: qsTr("Blue") },
        { id: "teal", color: Theme.c.settingsAccentSwatch.teal, label: qsTr("Teal") },
        { id: "green", color: Theme.c.settingsAccentSwatch.green, label: qsTr("Green") }
    ]

    spacing: Theme.spacingMd

    ThemedChip {
        text: qsTr("Theme default")
        selected: ThemeManager.accentOverride === ""
        onClicked: ThemeManager.accentOverride = ""
    }

    Repeater {
        model: root.swatches

        Rectangle {
            id: swatch

            required property var modelData
            readonly property bool selected: ThemeManager.accentOverride.toLowerCase() === String(modelData.color).toLowerCase()

            width: Theme.controlHeightSm
            height: Theme.controlHeightSm
            radius: Theme.radiusPill
            color: modelData.color
            border.width: selected ? Theme.c.settingsAccentSwatch.ringWidth : 0
            border.color: Theme.c.settingsAccentSwatch.ring

            Accessible.role: Accessible.RadioButton
            Accessible.name: modelData.label
            Accessible.checked: selected
            Accessible.onPressAction: ThemeManager.accentOverride = String(modelData.color)

            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: ThemeManager.accentOverride = String(swatch.modelData.color)
            }
        }
    }
}
