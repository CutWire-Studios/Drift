import QtQuick
import Drift

SettingsPage {
    id: page

    pageId: "keymap"
    title: qsTr("Keyboard shortcuts")

    Text {
        width: parent.width
        visible: page.query.length === 0
        wrapMode: Text.WordWrap
        text: qsTr("Click a shortcut, then press the keys. Esc cancels, Backspace clears.")
        color: Theme.c.settingsRow.foregroundMuted
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSizeXs
    }

    SettingsRow {
        title: qsTr("Reset to defaults")
        searchId: "keymap-reset-defaults"
        description: qsTr("Restores every shortcut to its original key.")
        keywords: "restore original default shortcut"
        query: page.query

        ThemedButton {
            variant: "secondary"
            text: qsTr("Reset to defaults")
            glyph: Theme.icons.undo
            onClicked: {
                EditorState.resetShortcuts()
                Toasts.success(qsTr("Shortcuts reset to defaults."))
            }
        }
    }

    Repeater {
        model: EditorState.actions || []

        SettingsRow {
            required property var modelData

            title: modelData.label
            keywords: modelData.id + " " + (modelData.shortcut || "") + " shortcut key binding hotkey"
            query: page.query

            ShortcutCaptureField {
                width: 160
                actionId: modelData.id
                shortcut: modelData.shortcut
            }
        }
    }
}
