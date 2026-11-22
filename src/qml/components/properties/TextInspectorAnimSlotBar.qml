import QtQuick
import Drift

Row {
    id: root
    property var inspector
    spacing: Theme.spacingSm
    width: parent ? parent.width : implicitWidth

    Repeater {
        model: [
            { "id": "in", "label": qsTr("In") },
            { "id": "out", "label": qsTr("Out") },
            { "id": "loop", "label": qsTr("Loop") }
        ]
        delegate: ThemedToggleButton {
            required property var modelData
            width: Theme.touchInput && Theme.compact ? Math.floor((root.width - root.spacing * 2) / 3) : 64
            text: modelData.label
            checked: inspector.animSlot === modelData.id
            onClicked: {
                inspector.animSlot = modelData.id
                inspector.animCategory = ""
            }
        }
    }
}
