pragma ComponentBehavior: Bound
import QtQuick
import Drift

SettingsPage {
    id: page

    pageId: "keymap"
    title: qsTr("Keyboard shortcuts")

    readonly property var keymap: EditorState.keymap
    readonly property var entries: {
        const cats = page.keymap.categories
        const actions = page.keymap.actions
        const ordered = cats.filter(c => c.scope === "global").concat(cats.filter(c => c.scope !== "global"))
        const out = []
        for (const cat of ordered) {
            let first = true
            for (const action of actions) {
                if (action.category !== cat.id)
                    continue
                out.push({ id: action.id, label: action.label, category: cat.label,
                           header: first ? (cat.scope === "global"
                                            ? cat.label
                                            : qsTr("%1 — only while it is in focus").arg(cat.label)) : "",
                           shortcuts: action.shortcuts, modified: action.modified })
                first = false
            }
        }
        return out
    }
    readonly property int presetIndex: {
        const presets = page.keymap.presets
        for (let i = 0; i < presets.length; ++i) {
            if (presets[i].id === page.keymap.presetId)
                return i
        }
        return 0
    }

    function requestPreset(presetId) {
        if (presetId === page.keymap.presetId)
            return
        if (page.keymap.hasOverrides) {
            presetDialog.pendingPreset = presetId
            presetDialog.open()
        } else {
            page.keymap.setPreset(presetId, false)
        }
    }

    Text {
        width: parent.width
        visible: page.query.length === 0
        wrapMode: Text.WordWrap
        text: qsTr("Click + to add a shortcut, × to remove. Esc cancels.")
        color: Theme.c.settingsRow.foregroundMuted
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSizeXs
    }

    SettingsRow {
        title: qsTr("Shortcut preset")
        searchId: "keymap-preset"
        description: qsTr("The set of keys your changes are applied on top of.")
        keywords: "keymap preset layout scheme premiere resolve final cut"
        query: page.query

        ThemedComboBox {
            width: Theme.c.settingsRow.controlWidth
            textRole: "name"
            valueRole: "id"
            model: page.keymap.presets
            currentIndex: page.presetIndex
            onActivated: {
                page.requestPreset(model[currentIndex].id)
                currentIndex = Qt.binding(() => page.presetIndex)
            }
        }
    }

    SettingsRow {
        title: qsTr("Reset to preset")
        searchId: "keymap-reset-preset"
        description: qsTr("Removes every change you made and restores the preset's keys.")
        keywords: "restore original default shortcut"
        query: page.query

        ThemedButton {
            variant: "secondary"
            text: qsTr("Reset to preset")
            glyph: Theme.icons.undo
            enabled: page.keymap.hasOverrides
            onClicked: {
                page.keymap.resetAll()
                Toasts.success(qsTr("Shortcuts reset to the preset."))
            }
        }
    }

    Repeater {
        model: page.entries

        Column {
            id: entry

            required property var modelData

            readonly property bool matches: row.matches

            width: parent.width
            visible: row.visible

            SettingsGroupHeader {
                available: entry.modelData.header.length > 0
                text: entry.modelData.header
                query: page.query
            }

            SettingsRow {
                id: row

                title: entry.modelData.label
                keywords: entry.modelData.id + " " + entry.modelData.category + " "
                          + entry.modelData.shortcuts.join(" ") + " "
                          + entry.modelData.shortcuts.map(c => page.keymap.displayText(c)).join(" ")
                          + " shortcut key binding hotkey"
                query: page.query

                Row {
                    spacing: Theme.spacingSm

                    Rectangle {
                        anchors.verticalCenter: parent.verticalCenter
                        visible: entry.modelData.modified
                        width: Theme.c.settingsKeymapPage.modifiedMarkerSize
                        height: width
                        radius: Theme.radiusPill
                        color: Theme.c.settingsKeymapPage.modifiedMarker
                        Accessible.role: Accessible.StaticText
                        Accessible.name: qsTr("Modified")
                    }

                    Repeater {
                        model: entry.modelData.shortcuts

                        ShortcutChip {
                            required property string modelData

                            anchors.verticalCenter: parent.verticalCenter
                            chord: modelData
                            actionLabel: entry.modelData.label
                            onRemoveRequested: page.keymap.removeBinding(entry.modelData.id, modelData)
                        }
                    }

                    ShortcutCaptureField {
                        anchors.verticalCenter: parent.verticalCenter
                        actionId: entry.modelData.id
                        actionLabel: entry.modelData.label
                    }

                    IconButton {
                        anchors.verticalCenter: parent.verticalCenter
                        visible: entry.modelData.modified
                        glyph: Theme.icons.undo
                        tooltip: qsTr("Reset this shortcut")
                        Accessible.name: qsTr("Reset shortcut for %1").arg(entry.modelData.label)
                        onClicked: page.keymap.resetAction(entry.modelData.id)
                    }
                }
            }
        }
    }

    ThemedDialog {
        id: presetDialog

        property string pendingPreset: ""

        title: qsTr("Switch preset")
        showFooter: false
        preferredWidth: Theme.dialogWidthMd

        contentItem: Column {
            spacing: Theme.spacing2xl

            Text {
                width: parent.width
                wrapMode: Text.WordWrap
                text: qsTr("You have changed some shortcuts. Keep those changes on top of the new preset, or discard them?")
                color: Theme.c.settingsRow.foreground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeSm
            }

            Row {
                anchors.right: parent.right
                spacing: Theme.spacingLg

                ThemedButton {
                    text: qsTr("Cancel")
                    variant: "secondary"
                    onClicked: presetDialog.close()
                }
                ThemedButton {
                    text: qsTr("Discard my changes")
                    variant: "destructive"
                    onClicked: {
                        page.keymap.setPreset(presetDialog.pendingPreset, false)
                        presetDialog.close()
                    }
                }
                ThemedButton {
                    text: qsTr("Keep my changes")
                    variant: "primary"
                    onClicked: {
                        page.keymap.setPreset(presetDialog.pendingPreset, true)
                        presetDialog.close()
                    }
                }
            }
        }
    }
}
