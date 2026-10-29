import QtQuick
import Drift

Row {
    property var inspector
    visible: inspector.hasSelection
    width: parent.width
    spacing: Theme.spacingSm

    ThemedButton {
        text: qsTr("Paste effects")
        glyph: Theme.icons.clipboardPaste
        variant: "secondary"
        onClicked: EditorState.pasteEffectsFromClipboard(
                       EditorState.selectedTrack, EditorState.selectedClip)
    }
    SearchAnchor {
        searchId: "effects-save-preset"
        title: qsTr("Save as preset…")
        keywords: "stack"
        target: effectsSavePresetButton
    }
    ThemedButton {
        id: effectsSavePresetButton
        text: qsTr("Save as preset…")
        glyph: Theme.icons.save
        variant: "secondary"
        visible: inspector.selectedEffects.length > 0
        onClicked: inspector.saveEffectPresetRequested(-1)
    }
}
