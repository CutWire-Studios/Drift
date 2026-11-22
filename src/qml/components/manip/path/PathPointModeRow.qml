import QtQuick
import Drift

// What the selected points of the path being edited are: Corner, Mirrored, Aligned or Free handles,
// and a quick smooth/corner switch. Shown only while the path editor has points selected.
Column {
    id: row

    readonly property var session: EditorState.masking.pathEdit
    readonly property int mode: session.selectedMode

    width: parent ? parent.width : implicitWidth
    spacing: Theme.spacingXs
    visible: session.attached && session.selectionCount > 0

    ThemedLabel {
        text: qsTr("Point type")
    }

    Flow {
        width: parent.width
        spacing: Theme.spacingSm

        Repeater {
            model: [qsTr("Corner"), qsTr("Mirrored"), qsTr("Aligned"), qsTr("Free")]

            delegate: ThemedChip {
                required property int index
                required property string modelData
                text: modelData
                variant: "outline"
                selected: row.mode === index
                onClicked: row.session.setSelectedMode(index)
            }
        }

        ThemedChip {
            text: row.mode === 0 ? qsTr("Smooth") : qsTr("Corner")
            variant: "outline"
            onClicked: row.session.toggleSmoothSelected()
        }
    }
}
