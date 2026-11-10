import QtQuick
import Drift

Column {
    id: root

    property var row
    property int effectIndex: -1
    spacing: 4
    width: parent ? parent.width : implicitWidth
    visible: row && row.paramData.type === "lut"

    readonly property var looks: EditorState.color.looks
    readonly property int currentIndex: {
        const id = row ? (row.paramData.value || "") : ""
        for (let i = 0; i < looks.length; ++i) {
            if (looks[i].id === id)
                return i
        }
        return -1
    }

    Text {
        width: parent.width
        elide: Text.ElideRight
        text: root.row ? root.row.paramData.label : ""
        color: Theme.mutedForeground
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSizeXs
    }

    Row {
        width: parent.width
        spacing: 8

        ThemedComboBox {
            width: parent.width - importButton.width - parent.spacing
            model: root.looks.map(l => l.label)
            currentIndex: root.currentIndex
            displayText: root.currentIndex >= 0 ? currentText
                         : (root.row && root.row.paramData.lookLabel) || qsTr("None")
            onActivated: index => EditorState.setEffectTextParam(
                             EditorState.selectedTrack, EditorState.selectedClip,
                             root.effectIndex, root.row.paramData.key, root.looks[index].id)
        }

        ThemedButton {
            id: importButton
            text: qsTr("Import…")
            onClicked: {
                const urls = FileDialogs.openFiles(qsTr("Import LUTs"), [qsTr("LUT files (*.cube)")])
                if (urls && urls.length > 0)
                    EditorState.color.importLooks(urls)
            }
        }
    }

    ThemedChip {
        visible: !!(root.row && root.row.paramData.missing)
        selected: false
        variant: "outline"
        text: qsTr("Missing LUT")
    }
}
