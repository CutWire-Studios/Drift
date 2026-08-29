import QtQuick
import QtQuick.Controls.Basic
import Drift
import ".."

// Shortcuts tab. Split out of Settings, which had grown long
// enough that the binding list was permanently below the fold.
Item {
    id: root

    readonly property string query: search.text.trim().toLowerCase()
    readonly property var visibleActions: {
        const actions = EditorState.actions || []
        const q = root.query
        if (q.length === 0)
            return actions
        return actions.filter(function(action) {
            const label = (action.label || "").toLowerCase()
            const id = (action.id || "").toLowerCase()
            const shortcut = (action.shortcut || "").toLowerCase()
            return label.indexOf(q) >= 0 || id.indexOf(q) >= 0 || shortcut.indexOf(q) >= 0
        })
    }

    ThemedTextField {
        id: search
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: Theme.pagePadding
        placeholderText: qsTr("Search shortcuts")
        font.family: Theme.fontFamily
    }

    Flickable {
        anchors.top: search.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.topMargin: Theme.spacingMd
        contentHeight: shortcutColumn.height + Theme.spacing3xl
        clip: true
        ScrollBar.vertical: AppScrollBar { }

        Column {
            id: shortcutColumn
            x: Theme.pagePadding
            width: parent.width - Theme.pagePadding * 2
            spacing: Theme.spacingMd
            topPadding: Theme.pagePadding

            Text {
                width: parent.width
                wrapMode: Text.WordWrap
                text: qsTr("Click a shortcut, then press the keys. Esc cancels, Backspace clears.")
                color: Theme.mutedForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
                bottomPadding: Theme.spacingSm
            }

            EmptyState {
                visible: !EditorState.actions || EditorState.actions.length === 0
                width: parent.width
                compact: true
                glyph: Theme.icons.keyboard
                title: qsTr("No shortcuts available")
            }

            EmptyState {
                visible: EditorState.actions && EditorState.actions.length > 0
                         && root.visibleActions.length === 0
                width: parent.width
                compact: true
                glyph: Theme.icons.search
                title: qsTr("No shortcuts match “%1”").arg(search.text.trim())
                hint: qsTr("Try a different name or key.")
            }

            Repeater {
                model: root.visibleActions
                delegate: Row {
                    required property var modelData
                    width: shortcutColumn.width
                    spacing: Theme.spacingLg

                    Text {
                        width: Math.max(90, shortcutColumn.width - 128)
                        text: modelData.label
                        color: Theme.panelForeground
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSizeXs
                        wrapMode: Text.WordWrap
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    ShortcutCaptureField {
                        width: 120
                        actionId: modelData.id
                        shortcut: modelData.shortcut
                    }
                }
            }

            // Clearing a binding persists the empty string, so without this there was
            // no way back from having cleared one.
            ThemedButton {
                text: qsTr("Reset to defaults")
                variant: "secondary"
                glyph: Theme.icons.undo
                topPadding: Theme.spacingMd + 1
                onClicked: {
                    EditorState.resetShortcuts()
                    Toasts.success(qsTr("Shortcuts reset to defaults."))
                }
            }
        }
    }
}
