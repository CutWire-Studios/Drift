import QtQuick
import Drift
// Picking a text style adds a styled text clip at the playhead; picking another replaces it.
AndroidAddPage {
    id: page

    propertiesComponent: Component {
        Column {
            spacing: Theme.spacingXl

            ThemedButton {
                width: parent.width
                text: qsTr("Edit text")
                glyph: Theme.icons.type
                variant: "secondary"
                onClicked: ShellBridge.main.editorPage.startInlineTextEdit(
                               EditorState.selectedTrack, EditorState.selectedClip, false)
            }

            TextInspector {
                width: parent.width
            }
        }
    }

    TextAssetsTab {
        anchors.fill: parent
        onAdded: page.adopt()
    }
}
